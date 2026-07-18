#include "Core/AudioEngineBase.h"

#include <aengine/backends.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "miniaudio.h"

#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aengine
{
    namespace
    {
        Error MakeMiniaudioError(ErrorCode code, std::string_view operation, ma_result result)
        {
            auto message = std::string(operation);
            auto description = ma_result_description(result);
            if (description != nullptr && description[0] != '\0')
            {
                message += ": ";
                message += description;
            }

            return Error(code, std::move(message));
        }

        bool IsAudioDeviceUnavailable(ma_result result)
        {
            return result == MA_NO_BACKEND
                || result == MA_NO_DEVICE
                || result == MA_DEVICE_NOT_INITIALIZED
                || result == MA_FAILED_TO_OPEN_BACKEND_DEVICE;
        }

        Expected<std::wstring> Utf8ToWidePath(std::string_view path)
        {
            if (path.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                return std::unexpected(Error(
                    ErrorCode::InvalidArgument,
                    "The UTF-8 audio file path is too long to convert to a native Windows path."));
            }

            auto inputLength = static_cast<int>(path.size());
            auto wideLength = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                path.data(),
                inputLength,
                nullptr,
                0);
            if (wideLength <= 0)
            {
                return std::unexpected(Error(
                    ErrorCode::InvalidArgument,
                    "The audio file path is not valid UTF-8."));
            }

            auto widePath = std::wstring(static_cast<std::size_t>(wideLength), L'\0');
            auto convertedLength = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                path.data(),
                inputLength,
                widePath.data(),
                wideLength);
            if (convertedLength != wideLength)
            {
                return std::unexpected(Error(
                    ErrorCode::InvalidArgument,
                    "The UTF-8 audio file path could not be converted to a native Windows path."));
            }

            return widePath;
        }

        class MiniaudioPlayback final : public detail::BackendPlayback
        {
        public:
            explicit MiniaudioPlayback(ma_engine* engine)
                : Engine(engine)
                , EncodedBytes()
                , Decoder()
                , Sound()
                , IsDecoderInitialized(false)
                , IsSoundInitialized(false)
                , WasStopped(false)
            {
            }

            ~MiniaudioPlayback() override
            {
                this->Teardown();
            }

            Result Initialize(const PlaybackDescription& description)
            {
                auto initialization = MA_SUCCESS;
                if (description.Source.Kind == AudioSourceKind::Memory)
                {
                    this->EncodedBytes.assign(
                        description.Source.EncodedBytes.begin(),
                        description.Source.EncodedBytes.end());

                    initialization = ma_decoder_init_memory(
                        this->EncodedBytes.data(),
                        this->EncodedBytes.size(),
                        nullptr,
                        &this->Decoder);
                    if (initialization != MA_SUCCESS)
                    {
                        this->EncodedBytes.clear();
                        return std::unexpected(MakeMiniaudioError(
                            ErrorCode::DecodeFailed,
                            "miniaudio failed to decode the memory audio source",
                            initialization));
                    }

                    this->IsDecoderInitialized = true;
                    initialization = ma_sound_init_from_data_source(
                        this->Engine,
                        &this->Decoder,
                        MA_SOUND_FLAG_NO_SPATIALIZATION,
                        nullptr,
                        &this->Sound);
                }
                else
                {
                    auto widePath = Utf8ToWidePath(description.Source.FilePath);
                    if (!widePath)
                    {
                        return std::unexpected(std::move(widePath.error()));
                    }

                    initialization = ma_sound_init_from_file_w(
                        this->Engine,
                        widePath->c_str(),
                        MA_SOUND_FLAG_NO_SPATIALIZATION,
                        nullptr,
                        nullptr,
                        &this->Sound);
                }

                if (initialization != MA_SUCCESS)
                {
                    auto code = description.Source.Kind == AudioSourceKind::Memory
                        ? ErrorCode::DecodeFailed
                        : ErrorCode::ResourceLoadFailed;
                    this->Teardown();
                    return std::unexpected(MakeMiniaudioError(
                        code,
                        "miniaudio failed to create the playback sound",
                        initialization));
                }

                this->IsSoundInitialized = true;
                ma_sound_set_volume(&this->Sound, description.Volume);

                auto start = ma_sound_start(&this->Sound);
                if (start != MA_SUCCESS)
                {
                    this->Teardown();
                    return std::unexpected(MakeMiniaudioError(
                        ErrorCode::PlaybackFailed,
                        "miniaudio failed to start playback",
                        start));
                }

                return {};
            }

            Result Stop() override
            {
                if (this->WasStopped || !this->IsSoundInitialized)
                {
                    this->WasStopped = true;
                    return {};
                }

                auto stop = ma_sound_stop(&this->Sound);
                if (stop != MA_SUCCESS)
                {
                    return std::unexpected(MakeMiniaudioError(
                        ErrorCode::PlaybackFailed,
                        "miniaudio failed to stop playback",
                        stop));
                }

                this->WasStopped = true;
                return {};
            }

            Result SetVolume(float volume) override
            {
                if (!this->IsSoundInitialized)
                {
                    return std::unexpected(Error(
                        ErrorCode::PlaybackFailed,
                        "The miniaudio playback sound is not initialized."));
                }

                ma_sound_set_volume(&this->Sound, volume);
                return {};
            }

            Expected<PlaybackState> State() override
            {
                if (this->WasStopped)
                {
                    return PlaybackState::Stopped;
                }

                if (!this->IsSoundInitialized)
                {
                    return std::unexpected(Error(
                        ErrorCode::PlaybackFailed,
                        "The miniaudio playback sound is not initialized."));
                }

                if (ma_sound_at_end(&this->Sound) == MA_TRUE)
                {
                    return PlaybackState::Finished;
                }

                return PlaybackState::Playing;
            }

        private:
            void Teardown()
            {
                if (this->IsSoundInitialized)
                {
                    static_cast<void>(ma_sound_stop(&this->Sound));
                    ma_sound_uninit(&this->Sound);
                    this->IsSoundInitialized = false;
                }

                if (this->IsDecoderInitialized)
                {
                    ma_decoder_uninit(&this->Decoder);
                    this->IsDecoderInitialized = false;
                }

                this->EncodedBytes.clear();
                this->WasStopped = true;
            }

            ma_engine* Engine;
            std::vector<std::byte> EncodedBytes;
            ma_decoder Decoder;
            ma_sound Sound;
            bool IsDecoderInitialized;
            bool IsSoundInitialized;
            bool WasStopped;
        };

        class MiniaudioAudioEngine final : public detail::AudioEngineBase
        {
        public:
            MiniaudioAudioEngine()
                : AudioEngineBase(BackendId::Miniaudio)
                , Engine()
                , IsEngineInitialized(false)
            {
            }

            ~MiniaudioAudioEngine() override
            {
                this->Shutdown();
            }

        protected:
            Result OnInitialize(const AudioEngineCreateInfo& createInfo) override
            {
                static_cast<void>(createInfo);

                auto initialization = ma_engine_init(nullptr, &this->Engine);
                if (initialization != MA_SUCCESS)
                {
                    return std::unexpected(MakeMiniaudioError(
                        IsAudioDeviceUnavailable(initialization)
                            ? ErrorCode::DeviceUnavailable
                            : ErrorCode::InitializationFailed,
                        "miniaudio failed to initialize the audio engine",
                        initialization));
                }

                this->IsEngineInitialized = true;
                return {};
            }

            void OnShutdown() override
            {
                if (!this->IsEngineInitialized)
                {
                    return;
                }

                ma_engine_uninit(&this->Engine);
                this->IsEngineInitialized = false;
            }

            Expected<std::unique_ptr<detail::BackendPlayback>> CreatePlayback(
                const PlaybackDescription& description) override
            {
                auto playback = std::make_unique<MiniaudioPlayback>(&this->Engine);
                auto initialization = playback->Initialize(description);
                if (!initialization)
                {
                    return std::unexpected(std::move(initialization.error()));
                }

                auto result = std::unique_ptr<detail::BackendPlayback>(std::move(playback));
                return result;
            }

        private:
            ma_engine Engine;
            bool IsEngineInitialized;
        };
    }

    Result RegisterMiniaudioBackend(BackendRegistry& registry)
    {
        auto capabilities = BackendCapabilities();
        return registry.Register(
            BackendId::Miniaudio,
            "Miniaudio",
            capabilities,
            []() -> std::unique_ptr<IAudioEngine>
            {
                return std::make_unique<MiniaudioAudioEngine>();
            });
    }
}
