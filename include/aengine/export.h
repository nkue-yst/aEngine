#pragma once

#if defined(_WIN32) && !defined(AENGINE_STATIC)
    #if defined(AENGINE_CORE_EXPORTS)
        #define AENGINE_CORE_API __declspec(dllexport)
    #else
        #define AENGINE_CORE_API __declspec(dllimport)
    #endif

    #if defined(AENGINE_MINIAUDIO_EXPORTS)
        #define AENGINE_MINIAUDIO_API __declspec(dllexport)
    #else
        #define AENGINE_MINIAUDIO_API __declspec(dllimport)
    #endif

    #if defined(AENGINE_XAUDIO2_EXPORTS)
        #define AENGINE_XAUDIO2_API __declspec(dllexport)
    #else
        #define AENGINE_XAUDIO2_API __declspec(dllimport)
    #endif

    #if defined(AENGINE_OPENAL_EXPORTS)
        #define AENGINE_OPENAL_API __declspec(dllexport)
    #else
        #define AENGINE_OPENAL_API __declspec(dllimport)
    #endif

    #if defined(AENGINE_ALL_BACKENDS_EXPORTS)
        #define AENGINE_ALL_BACKENDS_API __declspec(dllexport)
    #else
        #define AENGINE_ALL_BACKENDS_API __declspec(dllimport)
    #endif
#else
    #define AENGINE_CORE_API
    #define AENGINE_MINIAUDIO_API
    #define AENGINE_XAUDIO2_API
    #define AENGINE_OPENAL_API
    #define AENGINE_ALL_BACKENDS_API
#endif
