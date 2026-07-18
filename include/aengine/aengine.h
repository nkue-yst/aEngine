#pragma once

#include <aengine/export.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aengine
{
    class IAudioEngine;

    namespace detail
    {
        class AudioEngineBase;
    }

    enum class ErrorCode
    {
        InvalidArgument,
        Unsupported,
        BackendUnavailable,
        DeviceUnavailable,
        InitializationFailed,
        ResourceLoadFailed,
        DecodeFailed,
        PlaybackFailed,
        InvalidHandle,
        IoError
    };

    struct Error final
    {
        AENGINE_CORE_API Error(ErrorCode code, std::string message);

        ErrorCode Code;
        std::string Message;
    };

    template<typename T>
    using Expected = std::expected<T, Error>;

    using Result = Expected<void>;

    enum class DiagnosticSeverity
    {
        Information,
        Warning,
        Error
    };

    struct Diagnostic final
    {
        AENGINE_CORE_API Diagnostic(DiagnosticSeverity severity, std::string category, std::string message);

        DiagnosticSeverity Severity;
        std::string Category;
        std::string Message;
    };

    using DiagnosticCallback = std::function<void(const Diagnostic&)>;

    enum class BackendId
    {
        Miniaudio,
        XAudio2,
        OpenAL
    };

    struct BackendCapabilities final
    {
        AENGINE_CORE_API BackendCapabilities();

        bool SupportsBasicPlayback;
        bool SupportsPerPlaybackVolume;
        bool SupportsSpatialAudio;
        bool SupportsEffects;
        bool SupportsStreaming;
    };

    enum class AudioSourceKind
    {
        File,
        Memory
    };

    struct AudioSource final
    {
        AENGINE_CORE_API AudioSource();
        AENGINE_CORE_API explicit AudioSource(std::string filePath);
        AENGINE_CORE_API AudioSource(std::span<const std::byte> encodedBytes, std::string name);

        AudioSourceKind Kind;
        std::string FilePath;
        std::span<const std::byte> EncodedBytes;
        std::string Name;
    };

    struct PlaybackDescription final
    {
        AENGINE_CORE_API PlaybackDescription();

        AudioSource Source;
        float Volume;
    };

    enum class PlaybackState
    {
        Playing,
        Stopped,
        Finished
    };

    // Handle ID with a table index and version for stale-reference detection.
    template<typename Tag>
    class Handle final
    {
    public:
        constexpr Handle()
            : IndexValue(0U)
            , VersionValue(0U)
            , OwnerTokenValue(0U)
        {
        }

        [[nodiscard]] constexpr bool IsValid() const
        {
            return this->VersionValue != 0U && this->OwnerTokenValue != 0U;
        }

        [[nodiscard]] constexpr std::uint32_t Index() const
        {
            return this->IndexValue;
        }

        [[nodiscard]] constexpr std::uint64_t Version() const
        {
            return this->VersionValue;
        }

        [[nodiscard]] constexpr bool operator==(const Handle&) const = default;

    private:
        friend class IAudioEngine;
        friend class detail::AudioEngineBase;

        constexpr Handle(std::uint32_t index, std::uint64_t version, std::uint64_t ownerToken)
            : IndexValue(index)
            , VersionValue(version)
            , OwnerTokenValue(ownerToken)
        {
        }

        std::uint32_t IndexValue;
        std::uint64_t VersionValue;
        std::uint64_t OwnerTokenValue;
    };

    struct PlaybackTag final {};
    using PlaybackHandle = Handle<PlaybackTag>;

    struct AudioEngineCreateInfo final
    {
        AENGINE_CORE_API AudioEngineCreateInfo();

        std::vector<std::string> RuntimeLibrarySearchPaths;
        DiagnosticCallback DiagnosticSink;
    };

    class AENGINE_CORE_API IAudioEngine
    {
    public:
        virtual ~IAudioEngine();
        IAudioEngine(const IAudioEngine&) = delete;
        IAudioEngine& operator=(const IAudioEngine&) = delete;
        IAudioEngine(IAudioEngine&&) = delete;
        IAudioEngine& operator=(IAudioEngine&&) = delete;

        [[nodiscard]] virtual BackendId Backend() const = 0;
        virtual Result Initialize(const AudioEngineCreateInfo& createInfo) = 0;
        virtual void Shutdown() = 0;
        virtual Expected<PlaybackHandle> Play(const PlaybackDescription& description) = 0;
        virtual Result Stop(PlaybackHandle handle) = 0;
        virtual Result SetVolume(PlaybackHandle handle, float volume) = 0;
        virtual Expected<PlaybackState> State(PlaybackHandle handle) = 0;
        virtual Result DestroyPlayback(PlaybackHandle handle) = 0;
        virtual Result StopAll() = 0;

    protected:
        IAudioEngine();
        [[nodiscard]] PlaybackHandle MakePlaybackHandle(std::uint32_t index, std::uint64_t version) const;
        [[nodiscard]] bool OwnsPlaybackHandle(PlaybackHandle handle) const;

    private:
        std::uint64_t PlaybackOwnerToken;
    };

    class BackendRegistry final
    {
    public:
        using Factory = std::function<std::unique_ptr<IAudioEngine>()>;

        AENGINE_CORE_API BackendRegistry();
        AENGINE_CORE_API ~BackendRegistry();
        AENGINE_CORE_API BackendRegistry(BackendRegistry&& other) noexcept;
        AENGINE_CORE_API BackendRegistry& operator=(BackendRegistry&& other) noexcept;
        BackendRegistry(const BackendRegistry&) = delete;
        BackendRegistry& operator=(const BackendRegistry&) = delete;

        AENGINE_CORE_API Result Register(BackendId id, std::string name, BackendCapabilities capabilities, Factory factory);
        [[nodiscard]] AENGINE_CORE_API std::vector<BackendId> Backends() const;
        [[nodiscard]] AENGINE_CORE_API std::string_view Name(BackendId id) const;
        [[nodiscard]] AENGINE_CORE_API Expected<BackendCapabilities> Capabilities(BackendId id) const;
        AENGINE_CORE_API Expected<std::unique_ptr<IAudioEngine>> Create(BackendId id, const AudioEngineCreateInfo& createInfo) const;

    private:
        class Implementation;
        std::unique_ptr<Implementation> State;
    };
}
