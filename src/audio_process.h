#ifndef AUDIO_PROCESS_HEADER
#define AUDIO_PROCESS_HEADER
#include <cinttypes>

void mix_channels(const int16_t *ssrc, int out_chan, int ssrc_chan, int frames_num, int16_t *output);

void decimator_2(const int16_t *src, size_t len, int16_t *dst, int32_t *filtState);

void decimator_3(const int16_t *src, size_t len, int16_t *dst, int32_t *filtState, double *buffer);

#endif