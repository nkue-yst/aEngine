#pragma once

#include <aengine/aengine.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace aengine::detail
{
    enum class PcmSampleFormat
    {
        Float32,
        Signed16
    };

    struct DecodedPcm final
    {
        DecodedPcm();

        std::vector<std::byte> Samples;
        std::uint32_t SampleRate;
        std::uint16_t Channels;
        std::uint16_t BitsPerSample;
    };

    Expected<DecodedPcm> DecodeWithMediaFoundation(const AudioSource& source, PcmSampleFormat sampleFormat);
}
