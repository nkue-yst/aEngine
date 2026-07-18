#include "Backends/XAudio2/XAudio2Backend.h"

#include "Platform/Windows/MediaFoundationDecoder.h"

#include <atomic>
#include <audioclient.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mfapi.h>
#include <objbase.h>
#include <string>
#include <utility>
#include <windows.h>
#include <xaudio2.h>

namespace aengine::detail
{
    namespace
    {
        using XAudio2CreateFunction = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);

        Error MakeXAudio2Error(ErrorCode code, std::string operation, HRESULT result)
        {
            operation.append(" failed with HRESULT ");
            operation.append(std::to_string(static_cast<long>(result)));
            operation.push_back('.');
            return Error(code, std::move(operation));
        }

        bool IsAudioDeviceUnavailable(HRESULT result)
        {
            return result == AUDCLNT_E_DEVICE_INVALIDATED
                || result == AUDCLNT_E_DEVICE_IN_USE
                || result == AUDCLNT_E_ENDPOINT_CREATE_FAILED
                || result == AUDCLNT_E_SERVICE_NOT_RUNNING
                || result == AUDCLNT_E_RESOURCES_INVALIDATED
                || result == XAUDIO2_E_DEVICE_INVALIDATED
                || result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }

        class VoiceCallback final : public IXAudio2VoiceCallback
        {
        public:
            VoiceCallback()
                : StateValue(PlaybackState::Playing)
                , VoiceError(S_OK)
            {
            }

            void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override
            {
            }

            void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override
            {
            }

            void STDMETHODCALLTYPE OnStreamEnd() override
            {
                this->MarkFinished();
            }

            void STDMETHODCALLTYPE OnBufferStart(void*) override
            {
            }

            void STDMETHODCALLTYPE OnBufferEnd(void*) override
            {
                this->MarkFinished();
            }

            void STDMETHODCALLTYPE OnLoopEnd(void*) override
            {
            }

            void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT error) override
            {
                this->VoiceError.store(error, std::memory_order_release);
                this->MarkFinished();
            }

            void MarkStopped()
            {
                this->StateValue.store(PlaybackState::Stopped, std::memory_order_release);
            }

            [[nodiscard]] PlaybackState State() const
            {
                return this->StateValue.load(std::memory_order_acquire);
            }

            [[nodiscard]] HRESULT ErrorResult() const
            {
                return this->VoiceError.load(std::memory_order_acquire);
            }

        private:
            void MarkFinished()
            {
                auto expected = PlaybackState::Playing;
                this->StateValue.compare_exchange_strong(
                    expected,
                    PlaybackState::Finished,
                    std::memory_order_release,
                    std::memory_order_relaxed);
            }

            std::atomic<PlaybackState> StateValue;
            std::atomic<HRESULT> VoiceError;
        };

        class XAudio2Playback final : public BackendPlayback
        {
        public:
            explicit XAudio2Playback(DecodedPcm pcm)
                : Callback()
                , SourceVoice(nullptr)
                , Pcm(std::move(pcm))
            {
            }

            ~XAudio2Playback() override
            {
                this->DestroyVoice();
            }

            Result Initialize(IXAudio2& engine, float volume)
            {
                if (this->Pcm.Samples.size() > static_cast<std::size_t>(std::numeric_limits<UINT32>::max()))
                {
                    return std::unexpected(Error(ErrorCode::PlaybackFailed, "The decoded PCM data is too large for one XAudio2 buffer."));
                }

                auto format = WAVEFORMATEX();
                format.wFormatTag = this->Pcm.BitsPerSample == 32U ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
                format.nChannels = this->Pcm.Channels;
                format.nSamplesPerSec = this->Pcm.SampleRate;
                format.wBitsPerSample = this->Pcm.BitsPerSample;
                format.nBlockAlign = static_cast<WORD>(
                    static_cast<std::uint32_t>(format.nChannels) * (format.wBitsPerSample / 8U));
                format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
                format.cbSize = 0U;

                auto result = engine.CreateSourceVoice(
                    &this->SourceVoice,
                    &format,
                    0U,
                    XAUDIO2_DEFAULT_FREQ_RATIO,
                    &this->Callback);
                if (FAILED(result) || this->SourceVoice == nullptr)
                {
                    this->DestroyVoice();
                    return std::unexpected(MakeXAudio2Error(
                        ErrorCode::PlaybackFailed,
                        "XAudio2 source voice creation",
                        result));
                }

                result = this->SourceVoice->SetVolume(volume);
                if (FAILED(result))
                {
                    this->DestroyVoice();
                    return std::unexpected(MakeXAudio2Error(
                        ErrorCode::PlaybackFailed,
                        "XAudio2 source voice volume configuration",
                        result));
                }

                auto buffer = XAUDIO2_BUFFER();
                buffer.Flags = XAUDIO2_END_OF_STREAM;
                buffer.AudioBytes = static_cast<UINT32>(this->Pcm.Samples.size());
                buffer.pAudioData = reinterpret_cast<const BYTE*>(this->Pcm.Samples.data());
                buffer.pContext = this;
                result = this->SourceVoice->SubmitSourceBuffer(&buffer);
                if (FAILED(result))
                {
                    this->DestroyVoice();
                    return std::unexpected(MakeXAudio2Error(
                        ErrorCode::PlaybackFailed,
                        "XAudio2 source buffer submission",
                        result));
                }

                result = this->SourceVoice->Start(0U);
                if (FAILED(result))
                {
                    this->DestroyVoice();
                    return std::unexpected(MakeXAudio2Error(
                        ErrorCode::PlaybackFailed,
                        "XAudio2 source voice start",
                        result));
                }

                return {};
            }

            Result Stop() override
            {
                if (this->SourceVoice == nullptr)
                {
                    this->Callback.MarkStopped();
                    return {};
                }

                auto result = this->SourceVoice->Stop(0U);
                if (FAILED(result))
                {
                    return std::unexpected(MakeXAudio2Error(
                        ErrorCode::PlaybackFailed,
                        "XAudio2 source voice stop",
                        result));
                }

                this->Callback.MarkStopped();
                result = this->SourceVoice->FlushSourceBuffers();
                if (FAILED(result))
                {
                    return std::unexpected(MakeXAudio2Error(
                        ErrorCode::PlaybackFailed,
                        "XAudio2 source buffer flush",
                        result));
                }

                return {};
            }

            Result SetVolume(float volume) override
            {
                if (this->SourceVoice == nullptr)
                {
                    return std::unexpected(Error(ErrorCode::PlaybackFailed, "The XAudio2 source voice is unavailable."));
                }

                auto result = this->SourceVoice->SetVolume(volume);
                if (FAILED(result))
                {
                    return std::unexpected(MakeXAudio2Error(
                        ErrorCode::PlaybackFailed,
                        "XAudio2 source voice volume update",
                        result));
                }

                return {};
            }

            Expected<PlaybackState> State() override
            {
                auto voiceError = this->Callback.ErrorResult();
                if (FAILED(voiceError))
                {
                    return std::unexpected(MakeXAudio2Error(
                        ErrorCode::PlaybackFailed,
                        "XAudio2 asynchronous voice processing",
                        voiceError));
                }

                return this->Callback.State();
            }

        private:
            void DestroyVoice()
            {
                if (this->SourceVoice == nullptr)
                {
                    return;
                }

                static_cast<void>(this->SourceVoice->Stop(0U));
                this->Callback.MarkStopped();
                static_cast<void>(this->SourceVoice->FlushSourceBuffers());
                this->SourceVoice->DestroyVoice();
                this->SourceVoice = nullptr;
            }

            VoiceCallback Callback;
            IXAudio2SourceVoice* SourceVoice;
            DecodedPcm Pcm;
        };
    }

    XAudio2AudioEngine::XAudio2AudioEngine()
        : AudioEngineBase(BackendId::XAudio2)
        , Engine(nullptr)
        , MasteringVoice(nullptr)
        , XAudio2Library()
        , DidInitializeCom(false)
        , DidStartMediaFoundation(false)
    {
    }

    XAudio2AudioEngine::~XAudio2AudioEngine()
    {
        this->Shutdown();
    }

    Result XAudio2AudioEngine::OnInitialize(const AudioEngineCreateInfo& createInfo)
    {
        this->ReleaseResources();

        auto comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
        {
            return std::unexpected(MakeXAudio2Error(
                ErrorCode::InitializationFailed,
                "COM initialization for XAudio2",
                comResult));
        }

        this->DidInitializeCom = SUCCEEDED(comResult);
        if (comResult == RPC_E_CHANGED_MODE)
        {
            this->Report(
                DiagnosticSeverity::Warning,
                "XAudio2",
                "The owner thread was already initialized with a different COM apartment model.");
        }

        auto mediaFoundationResult = MFStartup(MF_VERSION);
        if (FAILED(mediaFoundationResult))
        {
            auto error = MakeXAudio2Error(
                ErrorCode::InitializationFailed,
                "Media Foundation initialization for XAudio2",
                mediaFoundationResult);
            this->ReleaseResources();
            return std::unexpected(std::move(error));
        }
        this->DidStartMediaFoundation = true;

        this->XAudio2Library = DynamicLibrary::LoadFromRuntimeSearchPath(
            { L"xaudio2_9.dll", L"xaudio2_8.dll" },
            createInfo.RuntimeLibrarySearchPaths);
        if (!this->XAudio2Library.IsLoaded())
        {
            this->ReleaseResources();
            return std::unexpected(Error(
                ErrorCode::BackendUnavailable,
                "No supported XAudio2 runtime library could be loaded."));
        }

        auto createXAudio2 = static_cast<XAudio2CreateFunction>(nullptr);
        if (!this->XAudio2Library.LoadSymbol("XAudio2Create", createXAudio2))
        {
            this->ReleaseResources();
            return std::unexpected(Error(
                ErrorCode::BackendUnavailable,
                "The loaded XAudio2 runtime does not export XAudio2Create."));
        }

        auto engine = static_cast<IXAudio2*>(nullptr);
        auto result = createXAudio2(&engine, 0U, XAUDIO2_DEFAULT_PROCESSOR);
        if (FAILED(result) || engine == nullptr)
        {
            if (engine != nullptr)
            {
                engine->Release();
            }

            auto error = MakeXAudio2Error(
                ErrorCode::InitializationFailed,
                "XAudio2 engine creation",
                result);
            this->ReleaseResources();
            return std::unexpected(std::move(error));
        }
        this->Engine = engine;

        auto masteringVoice = static_cast<IXAudio2MasteringVoice*>(nullptr);
        result = engine->CreateMasteringVoice(&masteringVoice);
        if (FAILED(result) || masteringVoice == nullptr)
        {
            if (masteringVoice != nullptr)
            {
                masteringVoice->DestroyVoice();
            }

            auto error = MakeXAudio2Error(
                IsAudioDeviceUnavailable(result)
                    ? ErrorCode::DeviceUnavailable
                    : ErrorCode::InitializationFailed,
                "XAudio2 mastering voice creation",
                result);
            this->ReleaseResources();
            return std::unexpected(std::move(error));
        }
        this->MasteringVoice = masteringVoice;

        return {};
    }

    void XAudio2AudioEngine::OnShutdown()
    {
        this->ReleaseResources();
    }

    Expected<std::unique_ptr<BackendPlayback>> XAudio2AudioEngine::CreatePlayback(const PlaybackDescription& description)
    {
        if (this->Engine == nullptr)
        {
            return std::unexpected(Error(ErrorCode::InitializationFailed, "The XAudio2 engine is unavailable."));
        }

        auto decoded = DecodeWithMediaFoundation(description.Source, PcmSampleFormat::Float32);
        if (!decoded)
        {
            return std::unexpected(std::move(decoded.error()));
        }

        auto playback = std::make_unique<XAudio2Playback>(std::move(*decoded));
        auto initialization = playback->Initialize(*static_cast<IXAudio2*>(this->Engine), description.Volume);
        if (!initialization)
        {
            return std::unexpected(std::move(initialization.error()));
        }

        return std::unique_ptr<BackendPlayback>(std::move(playback));
    }

    void XAudio2AudioEngine::ReleaseResources()
    {
        if (this->MasteringVoice != nullptr)
        {
            static_cast<IXAudio2MasteringVoice*>(this->MasteringVoice)->DestroyVoice();
            this->MasteringVoice = nullptr;
        }

        if (this->Engine != nullptr)
        {
            static_cast<IXAudio2*>(this->Engine)->Release();
            this->Engine = nullptr;
        }

        this->XAudio2Library.Reset();

        if (this->DidStartMediaFoundation)
        {
            MFShutdown();
            this->DidStartMediaFoundation = false;
        }

        if (this->DidInitializeCom)
        {
            CoUninitialize();
            this->DidInitializeCom = false;
        }
    }
}
