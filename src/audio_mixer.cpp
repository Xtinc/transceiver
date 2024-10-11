#include "audio_mixer.h"
#include "audio_network.h"
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

bool fit_sliences(int16_t *input, int in_chan, int frames_num)
{
    auto sum = 0.0f;
    {
        for (int i = 0; i < frames_num; i++)
        {
            for (int j = 0; j < in_chan; j++)
            {
                auto amp = input[i * in_chan + j];
                sum += amp * amp * HanningWindows(frames_num - i - 1, frames_num);
            }
        }
        sum /= frames_num * in_chan;
    }
    return sum < 107374.18f;
}
