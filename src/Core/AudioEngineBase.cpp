#include "Core/AudioEngineBase.h"

#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace aengine::detail
{
    namespace
    {
        Error AnnotateSource(Error error, const AudioSource& source)
        {
            if (source.Kind == AudioSourceKind::Memory && !source.Name.empty())
            {
                error.Message.append(" [source: ");
                error.Message.append(source.Name);
                error.Message.push_back(']');
            }

            return error;
        }

    }

    AudioEngineBase::PlaybackSlot::PlaybackSlot()
        : Generation(1U)
        , Playback(nullptr)
        , IsRetired(false)
    {
    }

    AudioEngineBase::PlaybackSlot::PlaybackSlot(PlaybackSlot&& other) noexcept = default;

    AudioEngineBase::PlaybackSlot& AudioEngineBase::PlaybackSlot::operator=(PlaybackSlot&& other) noexcept = default;

    AudioEngineBase::AudioEngineBase(BackendId backendId)
        : BackendValue(backendId)
        , DiagnosticSink()
        , PlaybackSlots()
        , StateValue(LifecycleState::Uninitialized)
    {
    }

    AudioEngineBase::~AudioEngineBase() = default;

    BackendId AudioEngineBase::Backend() const
    {
        return this->BackendValue;
    }

    Result AudioEngineBase::Initialize(const AudioEngineCreateInfo& createInfo)
    {
        if (this->StateValue == LifecycleState::Initializing || this->StateValue == LifecycleState::ShuttingDown)
        {
            return std::unexpected(Error(
                ErrorCode::InitializationFailed,
                "The audio engine is already performing a lifecycle transition."));
        }

        auto createInfoValidation = this->ValidateCreateInfo(createInfo);
        if (!createInfoValidation)
        {
            return createInfoValidation;
        }

        if (this->StateValue == LifecycleState::Initialized)
        {
            this->Shutdown();
        }

        this->DiagnosticSink = createInfo.DiagnosticSink;
        this->StateValue = LifecycleState::Initializing;

        try
        {
            auto initialization = this->OnInitialize(createInfo);
            if (!initialization)
            {
                this->RollbackInitialization();
                return initialization;
            }
        }
        catch (const std::exception& exception)
        {
            this->RollbackInitialization();
            return std::unexpected(Error(
                ErrorCode::InitializationFailed,
                std::string("The audio backend threw during initialization: ") + exception.what()));
        }
        catch (...)
        {
            this->RollbackInitialization();
            return std::unexpected(Error(
                ErrorCode::InitializationFailed,
                "The audio backend threw during initialization."));
        }

        this->StateValue = LifecycleState::Initialized;
        return {};
    }

    void AudioEngineBase::Shutdown()
    {
        if (this->StateValue != LifecycleState::Initialized)
        {
            return;
        }

        this->StateValue = LifecycleState::ShuttingDown;
        for (auto& slot : this->PlaybackSlots)
        {
            if (slot.Playback != nullptr)
            {
                try
                {
                    static_cast<void>(slot.Playback->Stop());
                }
                catch (...)
                {
                }
                this->ReleaseSlot(slot);
            }
        }

        try
        {
            this->OnShutdown();
        }
        catch (...)
        {
        }

        this->StateValue = LifecycleState::Uninitialized;
        this->DiagnosticSink = DiagnosticCallback();
    }

    Expected<PlaybackHandle> AudioEngineBase::Play(const PlaybackDescription& description)
    {
        if (this->StateValue != LifecycleState::Initialized)
        {
            return std::unexpected(Error(ErrorCode::InitializationFailed, "The audio engine is not initialized."));
        }

        auto validation = this->ValidateDescription(description);
        if (!validation)
        {
            return std::unexpected(AnnotateSource(std::move(validation.error()), description.Source));
        }

        auto playback = this->CreatePlayback(description);
        if (!playback)
        {
            return std::unexpected(AnnotateSource(std::move(playback.error()), description.Source));
        }

        for (auto index = std::size_t { 0U }; index < this->PlaybackSlots.size(); ++index)
        {
            auto& slot = this->PlaybackSlots[index];
            if (slot.Playback == nullptr && !slot.IsRetired)
            {
                slot.Playback = std::move(*playback);
                return this->MakePlaybackHandle(static_cast<std::uint32_t>(index), slot.Generation);
            }
        }

        if (this->PlaybackSlots.size() > std::numeric_limits<std::uint32_t>::max())
        {
            return std::unexpected(Error(ErrorCode::PlaybackFailed, "No playback handle indices remain."));
        }

        auto slot = PlaybackSlot();
        slot.Playback = std::move(*playback);
        this->PlaybackSlots.emplace_back(std::move(slot));
        auto index = static_cast<std::uint32_t>(this->PlaybackSlots.size() - 1U);
        return this->MakePlaybackHandle(index, this->PlaybackSlots.back().Generation);
    }

    Result AudioEngineBase::Stop(PlaybackHandle handle)
    {
        auto slot = this->ResolveSlot(handle);
        if (!slot)
        {
            return std::unexpected(std::move(slot.error()));
        }

        return (*slot)->Playback->Stop();
    }

    Result AudioEngineBase::SetVolume(PlaybackHandle handle, float volume)
    {
        if (this->StateValue != LifecycleState::Initialized)
        {
            return std::unexpected(Error(ErrorCode::InitializationFailed, "The audio engine is not initialized."));
        }

        auto volumeValidation = this->ValidateVolume(volume);
        if (!volumeValidation)
        {
            return volumeValidation;
        }

        auto slot = this->ResolveSlot(handle);
        if (!slot)
        {
            return std::unexpected(std::move(slot.error()));
        }

        return (*slot)->Playback->SetVolume(volume);
    }

    Expected<PlaybackState> AudioEngineBase::State(PlaybackHandle handle)
    {
        auto slot = this->ResolveSlot(handle);
        if (!slot)
        {
            return std::unexpected(std::move(slot.error()));
        }

        return (*slot)->Playback->State();
    }

    Result AudioEngineBase::DestroyPlayback(PlaybackHandle handle)
    {
        auto slot = this->ResolveSlot(handle);
        if (!slot)
        {
            return std::unexpected(std::move(slot.error()));
        }

        auto stop = (*slot)->Playback->Stop();
        if (!stop)
        {
            return stop;
        }

        this->ReleaseSlot(**slot);
        return {};
    }

    Result AudioEngineBase::StopAll()
    {
        if (this->StateValue != LifecycleState::Initialized)
        {
            return std::unexpected(Error(ErrorCode::InitializationFailed, "The audio engine is not initialized."));
        }

        for (auto& slot : this->PlaybackSlots)
        {
            if (slot.Playback == nullptr)
            {
                continue;
            }

            auto stop = slot.Playback->Stop();
            if (!stop)
            {
                return stop;
            }
        }

        return {};
    }

    void AudioEngineBase::Report(DiagnosticSeverity severity, std::string category, std::string message) const
    {
        if (this->DiagnosticSink)
        {
            try
            {
                this->DiagnosticSink(Diagnostic(severity, std::move(category), std::move(message)));
            }
            catch (...)
            {
            }
        }
    }

    Result AudioEngineBase::ValidateCreateInfo(const AudioEngineCreateInfo& createInfo) const
    {
        for (const auto& searchPath : createInfo.RuntimeLibrarySearchPaths)
        {
            if (searchPath.find('\0') != std::string::npos)
            {
                return std::unexpected(Error(
                    ErrorCode::InvalidArgument,
                    "Runtime library search paths cannot contain embedded null characters."));
            }
        }

        return {};
    }

    Result AudioEngineBase::ValidateDescription(const PlaybackDescription& description) const
    {
        auto volumeValidation = this->ValidateVolume(description.Volume);
        if (!volumeValidation)
        {
            return volumeValidation;
        }

        switch (description.Source.Kind)
        {
        case AudioSourceKind::File:
            if (description.Source.FilePath.empty())
            {
                return std::unexpected(Error(ErrorCode::InvalidArgument, "A file audio source requires a non-empty path."));
            }
            if (description.Source.FilePath.find('\0') != std::string::npos)
            {
                return std::unexpected(Error(
                    ErrorCode::InvalidArgument,
                    "A file audio source path cannot contain embedded null characters."));
            }
            if (!description.Source.EncodedBytes.empty())
            {
                return std::unexpected(Error(ErrorCode::InvalidArgument, "A file audio source cannot also contain encoded bytes."));
            }
            return {};
        case AudioSourceKind::Memory:
            if (description.Source.EncodedBytes.empty())
            {
                return std::unexpected(Error(ErrorCode::InvalidArgument, "A memory audio source requires encoded bytes."));
            }
            if (!description.Source.FilePath.empty())
            {
                return std::unexpected(Error(ErrorCode::InvalidArgument, "A memory audio source cannot also contain a file path."));
            }
            return {};
        default:
            return std::unexpected(Error(ErrorCode::InvalidArgument, "The audio source kind is invalid."));
        }
    }

    Result AudioEngineBase::ValidateVolume(float volume) const
    {
        if (!std::isfinite(volume) || volume < 0.0f || volume > 1.0f)
        {
            return std::unexpected(Error(ErrorCode::InvalidArgument, "Playback volume must be finite and between 0 and 1."));
        }

        return {};
    }

    Expected<AudioEngineBase::PlaybackSlot*> AudioEngineBase::ResolveSlot(PlaybackHandle handle)
    {
        if (this->StateValue != LifecycleState::Initialized)
        {
            return std::unexpected(Error(ErrorCode::InitializationFailed, "The audio engine is not initialized."));
        }

        if (!handle.IsValid() || handle.Index() >= this->PlaybackSlots.size())
        {
            return std::unexpected(Error(ErrorCode::InvalidHandle, "The playback handle is invalid."));
        }

        auto& slot = this->PlaybackSlots[handle.Index()];
        if (
            !this->OwnsPlaybackHandle(handle)
            || slot.Generation != handle.Generation()
            || slot.Playback == nullptr)
        {
            return std::unexpected(Error(ErrorCode::InvalidHandle, "The playback handle is stale."));
        }

        return &slot;
    }

    void AudioEngineBase::RollbackInitialization() noexcept
    {
        this->StateValue = LifecycleState::ShuttingDown;
        try
        {
            this->OnShutdown();
        }
        catch (...)
        {
        }

        this->StateValue = LifecycleState::Uninitialized;
        this->DiagnosticSink = nullptr;
    }

    void AudioEngineBase::ReleaseSlot(PlaybackSlot& slot)
    {
        slot.Playback.reset();
        if (slot.Generation == std::numeric_limits<std::uint64_t>::max())
        {
            slot.IsRetired = true;
            return;
        }

        ++slot.Generation;
        if (slot.Generation == std::numeric_limits<std::uint64_t>::max())
        {
            slot.IsRetired = true;
        }
    }
}
