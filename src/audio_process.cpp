#include "audio_process.h"
#include <fstream>

#define SCALEDIFF32(A, B, C) (C + (B >> 16) * A + (((uint32_t)(B & 0x0000FFFF) * A) >> 16))

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

static constexpr uint16_t ALLPASS_COFF1[3] = {3284, 24441, 49528};
static constexpr uint16_t ALLPASS_COFF2[3] = {12199, 37471, 60255};
static constexpr double CHEBY1_COFF1[4][6] = {{8.346817632453194e-05, 1.669363526490639e-04, 8.346817632453194e-05, 1.000000000000000e+00, -1.343579857463170e+00, 4.736480396716250e-01},
                                              {1.000000000000000e+00, 2.000000000000000e+00, 1.000000000000000e+00, 1.000000000000000e+00, -1.233016587258799e+00, 5.579412577211709e-01},
                                              {1.000000000000000e+00, 2.000000000000000e+00, 1.000000000000000e+00, 1.000000000000000e+00, -1.098793183091336e+00, 7.034332121984010e-01},
                                              {1.000000000000000e+00, 2.000000000000000e+00, 1.000000000000000e+00, 1.000000000000000e+00, -1.049696260282874e+00, 8.907238380078117e-01}};

void cheby1_ord8(double *inout, size_t len, const double sos[4][6], int32_t *filtState)
{
    double states[8];
    for (size_t i = 0; i < 8; i++)
    {
        states[i] = filtState[i];
    }

    for (int k = 0; k < 4; ++k)
    {
        auto w1 = states[k * 2];
        auto w2 = states[k * 2 + 1];
        auto b0 = sos[k][0];
        auto b1 = sos[k][1];
        auto b2 = sos[k][2];
        auto a0 = sos[k][3];
        auto a1 = sos[k][4];
        auto a2 = sos[k][5];

        for (int n = 0; n < len; ++n)
        {
            auto w0 = inout[n];
            w0 = w0 - a1 * w1 - a2 * w2;
            auto yn = b0 * w0 + b1 * w1 + b2 * w2;
            w2 = w1;
            w1 = w0;
            inout[n] = yn;
        }
        states[k * 2] = w1;
        states[k * 2 + 1] = w2;
    }

    for (size_t i = 0; i < 8; i++)
    {
        filtState[i] = static_cast<int32_t>(states[i]);
    }
}

void interpolator_2(const int16_t *in, size_t len, int16_t *out, int32_t *filtState)
{
    int32_t tmp1, tmp2, diff;
    int32_t states[8];
    std::memcpy(states, filtState, sizeof(states));
    for (size_t i = len; i > 0; i--)
    {
        // lower allpass filter
        auto in32 = (int32_t)(*in++) * (1 << 10);
        diff = in32 - states[1];
        tmp1 = SCALEDIFF32(ALLPASS_COFF1[0], diff, states[0]);
        states[0] = in32;
        diff = tmp1 - states[2];
        tmp2 = SCALEDIFF32(ALLPASS_COFF1[1], diff, states[1]);
        states[1] = tmp1;
        diff = tmp2 - states[3];
        states[3] = SCALEDIFF32(ALLPASS_COFF1[2], diff, states[2]);
        states[2] = tmp2;
        *out++ = clamp_s16((states[3] + 512) >> 10);

        // upper allpass filter
        diff = in32 - states[5];
        tmp1 = SCALEDIFF32(ALLPASS_COFF2[0], diff, states[4]);
        states[4] = in32;
        diff = tmp1 - states[6];
        tmp2 = SCALEDIFF32(ALLPASS_COFF2[1], diff, states[5]);
        states[5] = tmp1;
        diff = tmp2 - states[7];
        states[7] = SCALEDIFF32(ALLPASS_COFF2[2], diff, states[6]);
        states[6] = tmp2;
        *out++ = clamp_s16((states[7] + 512) >> 10);
    }
    std::memcpy(filtState, states, sizeof(states));
}

void decimator_2(const int16_t *in, size_t len, int16_t *out, int32_t *filtState)
{
    int32_t tmp1, tmp2, diff;
    int32_t states[8];
    std::memcpy(states, filtState, sizeof(states));
    for (size_t i = len / 2; i > 0; i--)
    {
        // lower allpass filter
        auto in32 = (int32_t)(*in++) * (1 << 10);
        diff = in32 - states[1];
        tmp1 = SCALEDIFF32(ALLPASS_COFF2[0], diff, states[0]);
        states[0] = in32;
        diff = tmp1 - states[2];
        tmp2 = SCALEDIFF32(ALLPASS_COFF2[1], diff, states[1]);
        states[1] = tmp1;
        diff = tmp2 - states[3];
        states[3] = SCALEDIFF32(ALLPASS_COFF2[2], diff, states[2]);
        states[2] = tmp2;

        // upper allpass filter
        in32 = (int32_t)(*in++) * (1 << 10);
        diff = in32 - states[5];
        tmp1 = SCALEDIFF32(ALLPASS_COFF1[0], diff, states[4]);
        states[4] = in32;
        diff = tmp1 - states[6];
        tmp2 = SCALEDIFF32(ALLPASS_COFF1[1], diff, states[5]);
        states[5] = tmp1;
        diff = tmp2 - states[7];
        states[7] = SCALEDIFF32(ALLPASS_COFF1[2], diff, states[6]);
        states[6] = tmp2;

        *out++ = clamp_s16((states[3] + states[7] + 1024) >> 11);
    }
    std::memcpy(filtState, states, sizeof(states));
}

void interpolator_3(const int16_t *in, size_t len, int16_t *out, int32_t *filtState, double *buffer)
{
    for (size_t i = 0; i < len; i++)
    {
        buffer[i] = in[i];
    }
    cheby1_ord8(buffer, len, CHEBY1_COFF1, filtState);
    for (size_t i = 0; i < len / 3; i++)
    {
        out[i] = clamp_s16(buffer[3 * i]);
    }
}

void decimator_3(const int16_t *in, size_t len, int16_t *out, int32_t *filtState, double *buffer)
{
    for (size_t i = 0; i < len; i++)
    {
        buffer[i] = in[i];
        buffer[i + 1] = 0.0;
        buffer[i + 2] = 0.0;
    }
    cheby1_ord8(buffer, len, CHEBY1_COFF1, filtState);
    for (size_t i = 0; i < len / 3; i++)
    {
        out[i] = clamp_s16(buffer[3 * i]);
    }
}