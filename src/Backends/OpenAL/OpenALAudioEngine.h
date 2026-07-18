#pragma once

#include "Core/AudioEngineBase.h"
#include "Platform/Windows/DynamicLibrary.h"

#include <AL/alc.h>

#include <memory>

namespace aengine::detail
{
    class OpenALAudioEngine final : public AudioEngineBase
    {
    public:
        OpenALAudioEngine();
        ~OpenALAudioEngine() override;

    protected:
        Result OnInitialize(const AudioEngineCreateInfo& createInfo) override;
        void OnShutdown() override;
        Expected<std::unique_ptr<BackendPlayback>> CreatePlayback(const PlaybackDescription& description) override;

    private:
        struct OpenALApi;
        class CurrentContextScope;
        class OpenALPlayback;

        [[nodiscard]] Result LoadOpenALLibrary(const AudioEngineCreateInfo& createInfo);
        void ReleaseBackendResources();

        std::unique_ptr<OpenALApi> Api;
        DynamicLibrary OpenALLibrary;
        ALCdevice* Device;
        ALCcontext* Context;
        bool DidInitializeCom;
        bool DidStartMediaFoundation;
        bool DidUseContextDetachFallback;
        bool MustAbandonOpenALResources;
    };
}
