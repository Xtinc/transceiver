#include "audio_process.h"
#include <fstream>

constexpr int16_t clamp_s16(int32_t v)
{
    return static_cast<int16_t>(v < -32768 ? -32768 : 32767 < v ? 32767
                                                                : v);
}

void mix_channels(const int16_t *ssrc, int out_chan, int ssrc_chan, int frames_num, int16_t *output)
{
    if (out_chan == ssrc_chan)
    {
        for (auto i = 0; i < frames_num * ssrc_chan; i++)
        {
            auto res = (int32_t)output[i] + (int32_t)ssrc[i];
            output[i] = clamp_s16(res);
        }
    }
    else if (out_chan == 1 && ssrc_chan == 2)
    {
        for (auto i = 0; i < frames_num; i++)
        {
            auto res = (int32_t)output[i] + ((int32_t)ssrc[2 * i] + (int32_t)ssrc[2 * i + 1]) / 2;
            output[i] = clamp_s16(res);
        }
    }
    else if (out_chan == 2 && ssrc_chan == 1)
    {
        for (auto i = 0; i < frames_num; i++)
        {
            auto res = (int32_t)output[2 * i] + (int32_t)ssrc[i];
            output[2 * i] = clamp_s16(res);
            res = (int32_t)output[2 * i + 1] + (int32_t)ssrc[i];
            output[2 * i + 1] = clamp_s16(res);
        }
    }
}

void decimator_2(const int16_t *src, size_t len, int16_t *dst)
{
    for (size_t i = 0; i < len / 2; i++)
    {
        dst[i] = src[2 * i];
    }
}
