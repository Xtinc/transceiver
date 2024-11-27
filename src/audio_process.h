#ifndef AUDIO_PROCESS_HEADER
#define AUDIO_PROCESS_HEADER
#include <cinttypes>

void mix_channels(const int16_t *ssrc, int out_chan, int ssrc_chan, int frames_num, int16_t *output);

void decimator_2(const int16_t *src, size_t len, int16_t *dst, int32_t *filtState);

void decimator_3(const int16_t *src, size_t len, int16_t *dst, int32_t *filtState, double *buffer);

class SincInterpolator
{
public:
    SincInterpolator(int order, int precision, double cutoff, double ratio);
    ~SincInterpolator();

    void operator()(const double *input, int n_input, double *out_put, int n_output, int stride);

private:
    double tlb_interpolator(double x, const double *input);

    double kernel_func(double x, double cutoff) const;

private:
    const int ord;
    const int quan;
    const double step;
    double *prev;
    double *kern;
};

#endif