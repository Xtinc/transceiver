#include "audiomixer.h"

void mix_channels(const int16_t *ssrc, int out_chan, int ssrc_chan, int frames_num, int16_t *output)
{
    if (out_chan == ssrc_chan)
    {
        for (auto i = 0; i < frames_num * ssrc_chan; i++)
        {
            *output++ += *ssrc++;
        }
    }
    else if (out_chan == 1 && ssrc_chan == 2)
    {
        for (auto i = 0; i < frames_num; i++)
        {
            *output++ += *ssrc;
            ssrc += 2;
        }
    }
    else if (out_chan == 2 && ssrc_chan == 1)
    {
        for (auto i = 0; i < frames_num; i++)
        {
            *output++ += *ssrc;
            *output++ += *ssrc;
            ssrc++;
        }
    }
}