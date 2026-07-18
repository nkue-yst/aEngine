#pragma once

#include <aengine/aengine.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace aengine::detail
{
    class BackendPlayback
    {
    public:
        virtual ~BackendPlayback() = default;

        virtual Result Stop() = 0;
        virtual Result SetVolume(float volume) = 0;
        virtual Expected<PlaybackState> State() = 0;
    };

    class AudioEngineBase : public IAudioEngine
    {
    public:
        AENGINE_CORE_API explicit AudioEngineBase(BackendId backendId);
        AENGINE_CORE_API ~AudioEngineBase() override;

        [[nodiscard]] AENGINE_CORE_API BackendId Backend() const final;
        AENGINE_CORE_API Result Initialize(const AudioEngineCreateInfo& createInfo) final;
        AENGINE_CORE_API void Shutdown() final;
        AENGINE_CORE_API Expected<PlaybackHandle> Play(const PlaybackDescription& description) final;
        AENGINE_CORE_API Result Stop(PlaybackHandle handle) final;
        AENGINE_CORE_API Result SetVolume(PlaybackHandle handle, float volume) final;
        AENGINE_CORE_API Expected<PlaybackState> State(PlaybackHandle handle) final;
        AENGINE_CORE_API Result DestroyPlayback(PlaybackHandle handle) final;
        AENGINE_CORE_API Result StopAll() final;

    protected:
        virtual Result OnInitialize(const AudioEngineCreateInfo& createInfo) = 0;
        virtual void OnShutdown() = 0;
        virtual Expected<std::unique_ptr<BackendPlayback>> CreatePlayback(const PlaybackDescription& description) = 0;
        AENGINE_CORE_API void Report(DiagnosticSeverity severity, std::string category, std::string message) const;

    private:
        enum class LifecycleState
        {
            Uninitialized,
            Initializing,
            Initialized,
            ShuttingDown
        };

        struct PlaybackSlot final
        {
            PlaybackSlot();
            PlaybackSlot(PlaybackSlot&& other) noexcept;
            PlaybackSlot& operator=(PlaybackSlot&& other) noexcept;
            PlaybackSlot(const PlaybackSlot&) = delete;
            PlaybackSlot& operator=(const PlaybackSlot&) = delete;

            std::uint64_t Version;
            std::unique_ptr<BackendPlayback> Playback;
            bool IsRetired;
        };

        [[nodiscard]] Result ValidateCreateInfo(const AudioEngineCreateInfo& createInfo) const;
        [[nodiscard]] Result ValidateDescription(const PlaybackDescription& description) const;
        [[nodiscard]] Result ValidateVolume(float volume) const;
        [[nodiscard]] Expected<PlaybackSlot*> ResolveSlot(PlaybackHandle handle);
        void RollbackInitialization() noexcept;
        void ReleaseSlot(PlaybackSlot& slot);

        BackendId BackendValue;
        DiagnosticCallback DiagnosticSink;
        std::vector<PlaybackSlot> PlaybackSlots;
        LifecycleState StateValue;
    };
}
