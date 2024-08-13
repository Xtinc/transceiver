#include "audioprotocol.h"

namespace
{
    constexpr char AUDIO_PACKET_MONO_CHAN = 1;
    constexpr char AUDIO_PACKET_DUAL_CHAN = 2;
    constexpr char MINIMUM_SAMPLE_RATE_RATIO = 1;
    constexpr char MAXIMUM_SAMPLE_RATE_RATIO = 6;
    constexpr char MINIMUM_AUDIO_ENCODER_IDX = 1;
    constexpr char MAXIMUM_AUDIO_ENCODER_IDX = 2;
}

bool AudioPackHeader::validate(const char *data, size_t len)
{
    if (len < sizeof(AudioPackHeader))
    {
        return false;
    }

    if (data[1] != AUDIO_PACKET_MONO_CHAN && data[1] != AUDIO_PACKET_DUAL_CHAN)
    {
        return false;
    }

    if (data[2] < MINIMUM_SAMPLE_RATE_RATIO || data[2] > MAXIMUM_SAMPLE_RATE_RATIO)
    {
        return false;
    }

    if (data[3] < MINIMUM_AUDIO_ENCODER_IDX || data[3] > MAXIMUM_AUDIO_ENCODER_IDX)
    {
        return false;
    }

    return true;
}
