#include "Backends/OpenAL/OpenALAudioEngine.h"

#include "Platform/Windows/MediaFoundationDecoder.h"

#include <AL/al.h>
#include <AL/alc.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mfapi.h>
#include <mutex>
#include <objbase.h>
#include <string>
#include <string_view>
#include <utility>

namespace aengine::detail
{
    namespace
    {
        std::recursive_mutex& OpenALContextMutex()
        {
            static std::recursive_mutex contextMutex;
            return contextMutex;
        }

        template<typename T>
        bool LoadOpenALFunction(const DynamicLibrary& library, const char* functionName, T& functionPointer)
        {
            return library.LoadSymbol(functionName, functionPointer);
        }
    }

    struct OpenALAudioEngine::OpenALApi final
    {
        OpenALApi()
            : OpenDevice(nullptr)
            , CreateContext(nullptr)
            , MakeContextCurrent(nullptr)
            , GetCurrentContext(nullptr)
            , DestroyContext(nullptr)
            , CloseDevice(nullptr)
            , GetAlcError(nullptr)
            , GenBuffers(nullptr)
            , DeleteBuffers(nullptr)
            , BufferData(nullptr)
            , GenSources(nullptr)
            , DeleteSources(nullptr)
            , Sourcei(nullptr)
            , Sourcef(nullptr)
            , SourcePlay(nullptr)
            , SourceStop(nullptr)
            , GetSourcei(nullptr)
            , GetAlError(nullptr)
        {
        }

        ALCdevice* (ALC_APIENTRY* OpenDevice)(const ALCchar* deviceName);
        ALCcontext* (ALC_APIENTRY* CreateContext)(ALCdevice* device, const ALCint* attributes);
        ALCboolean (ALC_APIENTRY* MakeContextCurrent)(ALCcontext* context);
        ALCcontext* (ALC_APIENTRY* GetCurrentContext)();
        void (ALC_APIENTRY* DestroyContext)(ALCcontext* context);
        ALCboolean (ALC_APIENTRY* CloseDevice)(ALCdevice* device);
        ALCenum (ALC_APIENTRY* GetAlcError)(ALCdevice* device);
        void (AL_APIENTRY* GenBuffers)(ALsizei count, ALuint* buffers);
        void (AL_APIENTRY* DeleteBuffers)(ALsizei count, const ALuint* buffers);
        void (AL_APIENTRY* BufferData)(ALuint buffer, ALenum format, const ALvoid* data, ALsizei size, ALsizei frequency);
        void (AL_APIENTRY* GenSources)(ALsizei count, ALuint* sources);
        void (AL_APIENTRY* DeleteSources)(ALsizei count, const ALuint* sources);
        void (AL_APIENTRY* Sourcei)(ALuint source, ALenum parameter, ALint value);
        void (AL_APIENTRY* Sourcef)(ALuint source, ALenum parameter, ALfloat value);
        void (AL_APIENTRY* SourcePlay)(ALuint source);
        void (AL_APIENTRY* SourceStop)(ALuint source);
        void (AL_APIENTRY* GetSourcei)(ALuint source, ALenum parameter, ALint* value);
        ALenum (AL_APIENTRY* GetAlError)();
    };

    class OpenALAudioEngine::CurrentContextScope final
    {
    public:
        enum class RestorationStatus
        {
            Restored,
            DetachedFallback,
            Failed
        };

        CurrentContextScope(
            OpenALApi& api,
            ALCcontext* context,
            std::recursive_mutex& contextMutex,
            bool& didUseDetachFallback,
            bool& mustAbandonOpenALResources)
            : Api(api)
            , Context(context)
            , Lock(contextMutex)
            , PreviousContext(api.GetCurrentContext())
            , DidUseDetachFallback(didUseDetachFallback)
            , MustAbandonOpenALResources(mustAbandonOpenALResources)
            , Status(RestorationStatus::Restored)
            , IsContextBound(false)
            , DidRestore(false)
        {
            if (this->PreviousContext == this->Context)
            {
                this->IsContextBound = true;
                return;
            }

            auto makeCurrentResult = this->Api.MakeContextCurrent(this->Context);
            this->IsContextBound = makeCurrentResult != ALC_FALSE
                && this->Api.GetCurrentContext() == this->Context;
        }

        ~CurrentContextScope()
        {
            static_cast<void>(this->Restore());
        }

        [[nodiscard]] bool IsBound() const
        {
            return this->IsContextBound;
        }

        [[nodiscard]] RestorationStatus Restore()
        {
            if (this->DidRestore)
            {
                return this->Status;
            }
            this->DidRestore = true;

            if (this->Api.GetCurrentContext() == this->PreviousContext)
            {
                this->Status = RestorationStatus::Restored;
                return this->Status;
            }

            if (this->Api.MakeContextCurrent(this->PreviousContext) != ALC_FALSE
                && this->Api.GetCurrentContext() == this->PreviousContext)
            {
                this->Status = RestorationStatus::Restored;
                return this->Status;
            }

            if (this->Api.MakeContextCurrent(nullptr) != ALC_FALSE
                && this->Api.GetCurrentContext() == nullptr)
            {
                if (this->PreviousContext == nullptr)
                {
                    this->Status = RestorationStatus::Restored;
                    return this->Status;
                }

                this->DidUseDetachFallback = true;
                this->Status = RestorationStatus::DetachedFallback;
                return this->Status;
            }

            this->MustAbandonOpenALResources = true;
            this->Status = RestorationStatus::Failed;
            return this->Status;
        }

    private:
        OpenALApi& Api;
        ALCcontext* Context;
        std::unique_lock<std::recursive_mutex> Lock;
        ALCcontext* PreviousContext;
        bool& DidUseDetachFallback;
        bool& MustAbandonOpenALResources;
        RestorationStatus Status;
        bool IsContextBound;
        bool DidRestore;
    };

    class OpenALAudioEngine::OpenALPlayback final : public BackendPlayback
    {
    public:
        OpenALPlayback(
            OpenALApi& api,
            ALCcontext* context,
            std::recursive_mutex& contextMutex,
            bool& didUseContextDetachFallback,
            bool& mustAbandonOpenALResources,
            ALuint source,
            ALuint buffer)
            : Api(api)
            , Context(context)
            , ContextMutex(contextMutex)
            , DidUseContextDetachFallback(didUseContextDetachFallback)
            , MustAbandonOpenALResources(mustAbandonOpenALResources)
            , Source(source)
            , Buffer(buffer)
            , WasStopped(false)
        {
        }

        ~OpenALPlayback() override
        {
            this->ReleaseResources();
        }

        Result Stop() override
        {
            if (this->Source == 0U || this->WasStopped)
            {
                this->WasStopped = true;
                return {};
            }

            auto contextScope = CurrentContextScope(
                this->Api,
                this->Context,
                this->ContextMutex,
                this->DidUseContextDetachFallback,
                this->MustAbandonOpenALResources);
            if (!contextScope.IsBound())
            {
                return std::unexpected(Error(ErrorCode::PlaybackFailed, "OpenAL could not temporarily bind the playback context."));
            }

            static_cast<void>(this->Api.GetAlError());
            this->Api.SourceStop(this->Source);
            if (this->Api.GetAlError() != AL_NO_ERROR)
            {
                return std::unexpected(Error(ErrorCode::PlaybackFailed, "OpenAL could not stop the playback source."));
            }

            this->WasStopped = true;
            auto restoration = contextScope.Restore();
            if (restoration != CurrentContextScope::RestorationStatus::Restored)
            {
                return std::unexpected(Error(ErrorCode::PlaybackFailed, "OpenAL could not restore the caller's current context after stopping playback."));
            }

            return {};
        }

        Result SetVolume(float volume) override
        {
            if (this->Source == 0U)
            {
                return std::unexpected(Error(ErrorCode::PlaybackFailed, "The OpenAL playback source has already been released."));
            }

            auto contextScope = CurrentContextScope(
                this->Api,
                this->Context,
                this->ContextMutex,
                this->DidUseContextDetachFallback,
                this->MustAbandonOpenALResources);
            if (!contextScope.IsBound())
            {
                return std::unexpected(Error(ErrorCode::PlaybackFailed, "OpenAL could not temporarily bind the playback context."));
            }

            static_cast<void>(this->Api.GetAlError());
            this->Api.Sourcef(this->Source, AL_GAIN, volume);
            if (this->Api.GetAlError() != AL_NO_ERROR)
            {
                return std::unexpected(Error(ErrorCode::PlaybackFailed, "OpenAL could not update the playback volume."));
            }

            auto restoration = contextScope.Restore();
            if (restoration != CurrentContextScope::RestorationStatus::Restored)
            {
                return std::unexpected(Error(ErrorCode::PlaybackFailed, "OpenAL could not restore the caller's current context after updating playback volume."));
            }

            return {};
        }

        Expected<PlaybackState> State() override
        {
            if (this->WasStopped)
            {
                return PlaybackState::Stopped;
            }

            if (this->Source == 0U)
            {
                return PlaybackState::Finished;
            }

            auto contextScope = CurrentContextScope(
                this->Api,
                this->Context,
                this->ContextMutex,
                this->DidUseContextDetachFallback,
                this->MustAbandonOpenALResources);
            if (!contextScope.IsBound())
            {
                return std::unexpected(Error(ErrorCode::PlaybackFailed, "OpenAL could not temporarily bind the playback context."));
            }

            auto sourceState = ALint();
            static_cast<void>(this->Api.GetAlError());
            this->Api.GetSourcei(this->Source, AL_SOURCE_STATE, &sourceState);
            if (this->Api.GetAlError() != AL_NO_ERROR)
            {
                return std::unexpected(Error(ErrorCode::PlaybackFailed, "OpenAL could not query the playback state."));
            }

            auto restoration = contextScope.Restore();
            if (restoration != CurrentContextScope::RestorationStatus::Restored)
            {
                return std::unexpected(Error(ErrorCode::PlaybackFailed, "OpenAL could not restore the caller's current context after querying playback state."));
            }

            if (sourceState == AL_PLAYING || sourceState == AL_PAUSED || sourceState == AL_INITIAL)
            {
                return PlaybackState::Playing;
            }

            if (sourceState == AL_STOPPED)
            {
                return PlaybackState::Finished;
            }

            return std::unexpected(Error(ErrorCode::PlaybackFailed, "OpenAL returned an unknown playback state."));
        }

    private:
        void ReleaseResources()
        {
            if (this->Source == 0U && this->Buffer == 0U)
            {
                return;
            }

            auto contextScope = CurrentContextScope(
                this->Api,
                this->Context,
                this->ContextMutex,
                this->DidUseContextDetachFallback,
                this->MustAbandonOpenALResources);
            if (!contextScope.IsBound())
            {
                this->MustAbandonOpenALResources = true;
                this->Source = 0U;
                this->Buffer = 0U;
                return;
            }

            if (this->Source != 0U)
            {
                this->Api.SourceStop(this->Source);
                this->Api.Sourcei(this->Source, AL_BUFFER, 0);
                this->Api.DeleteSources(1, &this->Source);
                this->Source = 0U;
            }

            if (this->Buffer != 0U)
            {
                this->Api.DeleteBuffers(1, &this->Buffer);
                this->Buffer = 0U;
            }

            static_cast<void>(contextScope.Restore());
        }

        OpenALApi& Api;
        ALCcontext* Context;
        std::recursive_mutex& ContextMutex;
        bool& DidUseContextDetachFallback;
        bool& MustAbandonOpenALResources;
        ALuint Source;
        ALuint Buffer;
        bool WasStopped;
    };

    OpenALAudioEngine::OpenALAudioEngine()
        : AudioEngineBase(BackendId::OpenAL)
        , Api(nullptr)
        , OpenALLibrary()
        , Device(nullptr)
        , Context(nullptr)
        , DidInitializeCom(false)
        , DidStartMediaFoundation(false)
        , DidUseContextDetachFallback(false)
        , MustAbandonOpenALResources(false)
    {
    }

    OpenALAudioEngine::~OpenALAudioEngine()
    {
        this->Shutdown();
    }

    Result OpenALAudioEngine::OnInitialize(const AudioEngineCreateInfo& createInfo)
    {
        auto libraryLoad = this->LoadOpenALLibrary(createInfo);
        if (!libraryLoad)
        {
            this->ReleaseBackendResources();
            return libraryLoad;
        }

        auto comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
        {
            this->ReleaseBackendResources();
            return std::unexpected(Error(ErrorCode::InitializationFailed, "COM initialization failed for the OpenAL backend."));
        }

        this->DidInitializeCom = SUCCEEDED(comResult);
        if (comResult == RPC_E_CHANGED_MODE)
        {
            this->Report(
                DiagnosticSeverity::Warning,
                "OpenAL",
                "The calling thread already uses a different COM apartment; the existing apartment will be used.");
        }

        if (FAILED(MFStartup(MF_VERSION)))
        {
            this->ReleaseBackendResources();
            return std::unexpected(Error(ErrorCode::InitializationFailed, "Media Foundation initialization failed for the OpenAL backend."));
        }
        this->DidStartMediaFoundation = true;

        this->Device = this->Api->OpenDevice(nullptr);
        if (this->Device == nullptr)
        {
            this->ReleaseBackendResources();
            return std::unexpected(Error(ErrorCode::DeviceUnavailable, "OpenAL could not open the default audio device."));
        }

        this->Context = this->Api->CreateContext(this->Device, nullptr);
        if (this->Context == nullptr)
        {
            this->ReleaseBackendResources();
            return std::unexpected(Error(ErrorCode::InitializationFailed, "OpenAL could not create an audio context."));
        }

        auto wasContextBound = false;
        auto restoration = CurrentContextScope::RestorationStatus::Restored;
        {
            auto contextScope = CurrentContextScope(
                *this->Api,
                this->Context,
                OpenALContextMutex(),
                this->DidUseContextDetachFallback,
                this->MustAbandonOpenALResources);
            wasContextBound = contextScope.IsBound();
            restoration = contextScope.Restore();
        }

        if (!wasContextBound)
        {
            this->ReleaseBackendResources();
            return std::unexpected(Error(ErrorCode::InitializationFailed, "OpenAL could not make the audio context current."));
        }

        if (restoration != CurrentContextScope::RestorationStatus::Restored)
        {
            this->ReleaseBackendResources();
            return std::unexpected(Error(ErrorCode::InitializationFailed, "OpenAL could not restore the caller's current context after initialization."));
        }

        return {};
    }

    void OpenALAudioEngine::OnShutdown()
    {
        this->ReleaseBackendResources();
    }

    Expected<std::unique_ptr<BackendPlayback>> OpenALAudioEngine::CreatePlayback(const PlaybackDescription& description)
    {
        if (this->Api == nullptr || this->Context == nullptr)
        {
            return std::unexpected(Error(ErrorCode::InitializationFailed, "The OpenAL backend is not initialized."));
        }

        auto decoded = DecodeWithMediaFoundation(description.Source, PcmSampleFormat::Signed16);
        if (!decoded)
        {
            return std::unexpected(std::move(decoded.error()));
        }

        if (decoded->Samples.empty() || decoded->BitsPerSample != 16U)
        {
            return std::unexpected(Error(ErrorCode::DecodeFailed, "OpenAL requires non-empty signed 16-bit PCM samples."));
        }

        auto format = ALenum();
        if (decoded->Channels == 1U)
        {
            format = AL_FORMAT_MONO16;
        }
        else if (decoded->Channels == 2U)
        {
            format = AL_FORMAT_STEREO16;
        }
        else
        {
            return std::unexpected(Error(ErrorCode::Unsupported, "OpenAL basic playback supports mono or stereo PCM audio."));
        }

        if (decoded->Samples.size() > static_cast<std::size_t>((std::numeric_limits<ALsizei>::max)())
            || decoded->SampleRate == 0U
            || decoded->SampleRate > static_cast<std::uint32_t>((std::numeric_limits<ALsizei>::max)()))
        {
            return std::unexpected(Error(ErrorCode::DecodeFailed, "The decoded audio is too large or has an invalid sample rate for OpenAL."));
        }

        auto playback = std::unique_ptr<OpenALPlayback>();
        auto restoration = CurrentContextScope::RestorationStatus::Restored;
        {
            auto contextScope = CurrentContextScope(
                *this->Api,
                this->Context,
                OpenALContextMutex(),
                this->DidUseContextDetachFallback,
                this->MustAbandonOpenALResources);
            if (!contextScope.IsBound())
            {
                return std::unexpected(Error(ErrorCode::PlaybackFailed, "OpenAL could not temporarily bind the playback context."));
            }

            auto buffer = ALuint();
            static_cast<void>(this->Api->GetAlError());
            this->Api->GenBuffers(1, &buffer);
            if (buffer == 0U || this->Api->GetAlError() != AL_NO_ERROR)
            {
                return std::unexpected(Error(ErrorCode::PlaybackFailed, "OpenAL could not create a playback buffer."));
            }

            this->Api->BufferData(
                buffer,
                format,
                decoded->Samples.data(),
                static_cast<ALsizei>(decoded->Samples.size()),
                static_cast<ALsizei>(decoded->SampleRate));
            if (this->Api->GetAlError() != AL_NO_ERROR)
            {
                this->Api->DeleteBuffers(1, &buffer);
                return std::unexpected(Error(ErrorCode::PlaybackFailed, "OpenAL could not upload decoded samples."));
            }

            auto source = ALuint();
            this->Api->GenSources(1, &source);
            if (source == 0U || this->Api->GetAlError() != AL_NO_ERROR)
            {
                this->Api->DeleteBuffers(1, &buffer);
                return std::unexpected(Error(ErrorCode::PlaybackFailed, "OpenAL could not create a playback source."));
            }

            this->Api->Sourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
            this->Api->Sourcef(source, AL_GAIN, description.Volume);
            this->Api->SourcePlay(source);
            if (this->Api->GetAlError() != AL_NO_ERROR)
            {
                this->Api->SourceStop(source);
                this->Api->Sourcei(source, AL_BUFFER, 0);
                this->Api->DeleteSources(1, &source);
                this->Api->DeleteBuffers(1, &buffer);
                return std::unexpected(Error(ErrorCode::PlaybackFailed, "OpenAL could not start audio playback."));
            }

            playback = std::make_unique<OpenALPlayback>(
                *this->Api,
                this->Context,
                OpenALContextMutex(),
                this->DidUseContextDetachFallback,
                this->MustAbandonOpenALResources,
                source,
                buffer);
            restoration = contextScope.Restore();
        }

        if (restoration != CurrentContextScope::RestorationStatus::Restored)
        {
            playback.reset();
            return std::unexpected(Error(ErrorCode::PlaybackFailed, "OpenAL could not restore the caller's current context after creating playback."));
        }

        return std::unique_ptr<BackendPlayback>(std::move(playback));
    }

    Result OpenALAudioEngine::LoadOpenALLibrary(const AudioEngineCreateInfo& createInfo)
    {
        this->OpenALLibrary = DynamicLibrary::LoadFromRuntimeSearchPath(
            { L"OpenAL32.dll" },
            createInfo.RuntimeLibrarySearchPaths);
        if (!this->OpenALLibrary.IsLoaded())
        {
            return std::unexpected(Error(ErrorCode::BackendUnavailable, "OpenAL32.dll was not found in the runtime library search paths."));
        }

        this->Api = std::make_unique<OpenALApi>();
        auto loaded = true;
        loaded = loaded && LoadOpenALFunction(this->OpenALLibrary, "alcOpenDevice", this->Api->OpenDevice);
        loaded = loaded && LoadOpenALFunction(this->OpenALLibrary, "alcCreateContext", this->Api->CreateContext);
        loaded = loaded && LoadOpenALFunction(this->OpenALLibrary, "alcMakeContextCurrent", this->Api->MakeContextCurrent);
        loaded = loaded && LoadOpenALFunction(this->OpenALLibrary, "alcGetCurrentContext", this->Api->GetCurrentContext);
        loaded = loaded && LoadOpenALFunction(this->OpenALLibrary, "alcDestroyContext", this->Api->DestroyContext);
        loaded = loaded && LoadOpenALFunction(this->OpenALLibrary, "alcCloseDevice", this->Api->CloseDevice);
        loaded = loaded && LoadOpenALFunction(this->OpenALLibrary, "alcGetError", this->Api->GetAlcError);
        loaded = loaded && LoadOpenALFunction(this->OpenALLibrary, "alGenBuffers", this->Api->GenBuffers);
        loaded = loaded && LoadOpenALFunction(this->OpenALLibrary, "alDeleteBuffers", this->Api->DeleteBuffers);
        loaded = loaded && LoadOpenALFunction(this->OpenALLibrary, "alBufferData", this->Api->BufferData);
        loaded = loaded && LoadOpenALFunction(this->OpenALLibrary, "alGenSources", this->Api->GenSources);
        loaded = loaded && LoadOpenALFunction(this->OpenALLibrary, "alDeleteSources", this->Api->DeleteSources);
        loaded = loaded && LoadOpenALFunction(this->OpenALLibrary, "alSourcei", this->Api->Sourcei);
        loaded = loaded && LoadOpenALFunction(this->OpenALLibrary, "alSourcef", this->Api->Sourcef);
        loaded = loaded && LoadOpenALFunction(this->OpenALLibrary, "alSourcePlay", this->Api->SourcePlay);
        loaded = loaded && LoadOpenALFunction(this->OpenALLibrary, "alSourceStop", this->Api->SourceStop);
        loaded = loaded && LoadOpenALFunction(this->OpenALLibrary, "alGetSourcei", this->Api->GetSourcei);
        loaded = loaded && LoadOpenALFunction(this->OpenALLibrary, "alGetError", this->Api->GetAlError);
        if (!loaded)
        {
            this->Api.reset();
            this->OpenALLibrary.Reset();
            return std::unexpected(Error(ErrorCode::InitializationFailed, "OpenAL32.dll does not export every required OpenAL function."));
        }

        return {};
    }

    void OpenALAudioEngine::ReleaseBackendResources()
    {
        if (this->Api != nullptr)
        {
            auto contextLock = std::scoped_lock(OpenALContextMutex());
            if (this->DidUseContextDetachFallback)
            {
                this->Report(
                    DiagnosticSeverity::Warning,
                    "OpenAL",
                    "OpenAL could not restore a caller context and detached the current context as a safety fallback.");
            }

            auto canReleaseOpenALResources = !this->MustAbandonOpenALResources;
            if (canReleaseOpenALResources
                && this->Context != nullptr
                && this->Api->GetCurrentContext() == this->Context)
            {
                if (this->Api->MakeContextCurrent(nullptr) == ALC_FALSE
                    || this->Api->GetCurrentContext() != nullptr)
                {
                    this->Report(
                        DiagnosticSeverity::Error,
                        "OpenAL",
                        "OpenAL could not detach the aEngine context; its context, device, and library will remain allocated for process safety.");
                    canReleaseOpenALResources = false;
                }
            }

            if (this->Context != nullptr && canReleaseOpenALResources)
            {
                this->Api->DestroyContext(this->Context);
                this->Context = nullptr;
            }

            if (this->Device != nullptr && canReleaseOpenALResources)
            {
                if (this->Api->CloseDevice(this->Device) == ALC_FALSE)
                {
                    this->Report(
                        DiagnosticSeverity::Error,
                        "OpenAL",
                        "OpenAL could not close the audio device; the runtime library will remain allocated for process safety.");
                    canReleaseOpenALResources = false;
                }
                this->Device = nullptr;
            }

            if (!canReleaseOpenALResources)
            {
                this->Context = nullptr;
                this->Device = nullptr;
                this->OpenALLibrary.Abandon();
            }
        }

        if (this->DidStartMediaFoundation)
        {
            static_cast<void>(MFShutdown());
            this->DidStartMediaFoundation = false;
        }

        if (this->DidInitializeCom)
        {
            CoUninitialize();
            this->DidInitializeCom = false;
        }

        this->Api.reset();
        this->OpenALLibrary.Reset();
        this->DidUseContextDetachFallback = false;
        this->MustAbandonOpenALResources = false;
    }
}
