#include <aengine/backends.h>

namespace aengine
{
    Result RegisterAllBackends(BackendRegistry& registry)
    {
        static_cast<void>(registry);
#if defined(AENGINE_HAS_MINIAUDIO)
        auto miniaudioResult = RegisterMiniaudioBackend(registry);
        if (!miniaudioResult)
        {
            return miniaudioResult;
        }
#endif
#if defined(AENGINE_HAS_XAUDIO2)
        auto xAudio2Result = RegisterXAudio2Backend(registry);
        if (!xAudio2Result)
        {
            return xAudio2Result;
        }
#endif
#if defined(AENGINE_HAS_OPENAL)
        auto openAlResult = RegisterOpenALBackend(registry);
        if (!openAlResult)
        {
            return openAlResult;
        }
#endif
        return {};
    }
}
