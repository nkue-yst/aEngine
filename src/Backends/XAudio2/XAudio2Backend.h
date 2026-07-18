#pragma once

#include "Core/AudioEngineBase.h"
#include "Platform/Windows/DynamicLibrary.h"

namespace aengine::detail
{
    class XAudio2AudioEngine final : public AudioEngineBase
    {
    public:
        XAudio2AudioEngine();
        ~XAudio2AudioEngine() override;

    protected:
        Result OnInitialize(const AudioEngineCreateInfo& createInfo) override;
        void OnShutdown() override;
        Expected<std::unique_ptr<BackendPlayback>> CreatePlayback(const PlaybackDescription& description) override;

    private:
        void ReleaseResources();

        void* Engine;
        void* MasteringVoice;
        DynamicLibrary XAudio2Library;
        bool DidInitializeCom;
        bool DidStartMediaFoundation;
    };
}
