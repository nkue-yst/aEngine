#include "Backends/OpenAL/OpenALAudioEngine.h"

#include <aengine/backends.h>

#include <memory>

namespace aengine
{
    Result RegisterOpenALBackend(BackendRegistry& registry)
    {
        auto capabilities = BackendCapabilities();
        capabilities.SupportsBasicPlayback = true;
        capabilities.SupportsPerPlaybackVolume = true;

        return registry.Register(
            BackendId::OpenAL,
            "OpenAL",
            capabilities,
            []
            {
                return std::make_unique<detail::OpenALAudioEngine>();
            });
    }
}
