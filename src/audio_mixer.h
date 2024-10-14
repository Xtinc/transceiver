#ifndef AUDIO_MIXER_HEADER
#define AUDIO_MIXER_HEADER
#include <cinttypes>

void mix_channels(const int16_t *ssrc, int out_chan, int ssrc_chan, int frames_num, int16_t *output);

#endif