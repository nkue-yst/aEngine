#include "Backends/XAudio2/XAudio2Backend.h"

#include <aengine/backends.h>

#include <memory>

namespace aengine
{
    Result RegisterXAudio2Backend(BackendRegistry& registry)
    {
        auto capabilities = BackendCapabilities();
        capabilities.SupportsBasicPlayback = true;
        capabilities.SupportsPerPlaybackVolume = true;
        capabilities.SupportsSpatialAudio = false;
        capabilities.SupportsEffects = false;
        capabilities.SupportsStreaming = false;

        return registry.Register(
            BackendId::XAudio2,
            "XAudio2",
            capabilities,
            []
            {
                return std::make_unique<detail::XAudio2AudioEngine>();
            });
    }
}
