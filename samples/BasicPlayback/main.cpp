#include <aengine/aengine.h>
#include <aengine/backends.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <windows.h>

namespace
{
    std::string WideToUtf8(std::wstring_view value)
    {
        if (value.empty() || value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        {
            return {};
        }

        auto requiredSize = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (requiredSize <= 0)
        {
            return {};
        }

        auto result = std::string(static_cast<std::size_t>(requiredSize), '\0');
        auto convertedSize = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            requiredSize,
            nullptr,
            nullptr);
        return convertedSize == requiredSize ? result : std::string();
    }

    std::optional<aengine::BackendId> ResolveBackend(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });

        if (value == "xaudio2")
        {
            return aengine::BackendId::XAudio2;
        }
        if (value == "openal")
        {
            return aengine::BackendId::OpenAL;
        }
        if (value == "miniaudio")
        {
            return aengine::BackendId::Miniaudio;
        }

        return std::nullopt;
    }

    void ReportError(const aengine::Error& error)
    {
        std::cerr << error.Message << std::endl;
    }
}

int wmain(int argumentCount, wchar_t** arguments)
{
    if (argumentCount != 3)
    {
        std::cerr << "Usage: aEngineBasicPlayback <miniaudio|xaudio2|openal> <audio-file>" << std::endl;
        return 1;
    }

    auto backendName = WideToUtf8(arguments[1]);
    auto audioFilePath = WideToUtf8(arguments[2]);
    if (backendName.empty() || audioFilePath.empty())
    {
        std::cerr << "Arguments could not be converted to UTF-8." << std::endl;
        return 2;
    }

    auto registry = aengine::BackendRegistry();
    auto registration = aengine::RegisterAllBackends(registry);
    if (!registration)
    {
        ReportError(registration.error());
        return 3;
    }

    auto createInfo = aengine::AudioEngineCreateInfo();
    createInfo.DiagnosticSink = [](const aengine::Diagnostic& diagnostic)
    {
        std::cerr << diagnostic.Category << ": " << diagnostic.Message << std::endl;
    };

    auto selectedBackend = ResolveBackend(backendName);
    if (!selectedBackend)
    {
        std::cerr << "Unknown backend: " << backendName << std::endl;
        return 4;
    }

    auto engine = registry.Create(*selectedBackend, createInfo);
    if (!engine)
    {
        ReportError(engine.error());
        return 5;
    }

    auto description = aengine::PlaybackDescription();
    description.Source = aengine::AudioSource(std::move(audioFilePath));
    auto playback = (*engine)->Play(description);
    if (!playback)
    {
        ReportError(playback.error());
        return 6;
    }

    while (true)
    {
        auto state = (*engine)->State(*playback);
        if (!state)
        {
            ReportError(state.error());
            return 7;
        }
        if (*state != aengine::PlaybackState::Playing)
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    auto destroyed = (*engine)->DestroyPlayback(*playback);
    if (!destroyed)
    {
        ReportError(destroyed.error());
        return 8;
    }

    return 0;
}
