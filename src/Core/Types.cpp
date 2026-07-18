#include <aengine/aengine.h>

#include <atomic>
#include <exception>
#include <limits>
#include <utility>

namespace aengine
{
    namespace
    {
        std::uint64_t NextPlaybackOwnerToken()
        {
            static auto nextOwnerToken = std::atomic<std::uint64_t>(1U);
            auto token = nextOwnerToken.load(std::memory_order_relaxed);
            while (token != std::numeric_limits<std::uint64_t>::max())
            {
                if (nextOwnerToken.compare_exchange_weak(
                    token,
                    token + 1U,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed))
                {
                    return token;
                }
            }

            std::terminate();
        }
    }

    Error::Error(ErrorCode code, std::string message)
        : Code(code)
        , Message(std::move(message))
    {
    }

    Diagnostic::Diagnostic(DiagnosticSeverity severity, std::string category, std::string message)
        : Severity(severity)
        , Category(std::move(category))
        , Message(std::move(message))
    {
    }

    BackendCapabilities::BackendCapabilities()
        : SupportsBasicPlayback(true)
        , SupportsPerPlaybackVolume(true)
        , SupportsSpatialAudio(false)
        , SupportsEffects(false)
        , SupportsStreaming(false)
    {
    }

    AudioSource::AudioSource()
        : Kind(AudioSourceKind::File)
        , FilePath()
        , EncodedBytes()
        , Name()
    {
    }

    AudioSource::AudioSource(std::string filePath)
        : Kind(AudioSourceKind::File)
        , FilePath(std::move(filePath))
        , EncodedBytes()
        , Name(this->FilePath)
    {
    }

    AudioSource::AudioSource(std::span<const std::byte> encodedBytes, std::string name)
        : Kind(AudioSourceKind::Memory)
        , FilePath()
        , EncodedBytes(encodedBytes)
        , Name(std::move(name))
    {
    }

    PlaybackDescription::PlaybackDescription()
        : Source()
        , Volume(1.0f)
    {
    }

    IAudioEngine::IAudioEngine()
        : PlaybackOwnerToken(NextPlaybackOwnerToken())
    {
    }

    IAudioEngine::~IAudioEngine() = default;

    PlaybackHandle IAudioEngine::MakePlaybackHandle(std::uint32_t index, std::uint64_t generation) const
    {
        if (generation == 0U)
        {
            return PlaybackHandle();
        }

        return PlaybackHandle(index, generation, this->PlaybackOwnerToken);
    }

    bool IAudioEngine::OwnsPlaybackHandle(PlaybackHandle handle) const
    {
        return handle.IsValid() && handle.OwnerTokenValue == this->PlaybackOwnerToken;
    }

    AudioEngineCreateInfo::AudioEngineCreateInfo()
        : RuntimeLibrarySearchPaths()
        , DiagnosticSink()
    {
    }
}
