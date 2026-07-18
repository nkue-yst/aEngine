#pragma once

#include <aengine/aengine.h>

namespace aengine
{
    AENGINE_MINIAUDIO_API Result RegisterMiniaudioBackend(BackendRegistry& registry);
    AENGINE_XAUDIO2_API Result RegisterXAudio2Backend(BackendRegistry& registry);
    AENGINE_OPENAL_API Result RegisterOpenALBackend(BackendRegistry& registry);
    AENGINE_ALL_BACKENDS_API Result RegisterAllBackends(BackendRegistry& registry);
}
