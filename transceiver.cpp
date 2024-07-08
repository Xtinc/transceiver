#include "transceiver.h"
#include "portaudio.h"
#include "audiofile.h"
#include <functional>
#include <iostream>

constexpr auto CUSTOM_SAMPLE_RATE = 48'000;
constexpr auto FRAMES_PER_BUFFER = 256;

/* Non-linear amplifier with soft distortion curve. */
float CubicAmplifier(float input)
{
    // float output, temp;
    // if (input < 0.0)
    // {
    //     temp = input + 1.0f;
    //     output = (temp * temp * temp) - 1.0f;
    // }
    // else
    // {
    //     temp = input - 1.0f;
    //     output = (temp * temp * temp) + 1.0f;
    // }

    // return output;
    return input;
}
#define FUZZ(x) CubicAmplifier(CubicAmplifier(CubicAmplifier(CubicAmplifier(x))))

static int gNumNoInputs = 0;
/* This routine will be called by the PortAudio engine when audio is needed.
** It may be called at interrupt level on some machines so don't do anything
** that could mess up the system like calling malloc() or free().
*/
static int fuzzCallback(const void *inputBuffer, void *outputBuffer,
                        unsigned long framesPerBuffer,
                        const PaStreamCallbackTimeInfo *timeInfo,
                        PaStreamCallbackFlags statusFlags,
                        void *userData)
{
    float *out = (float *)outputBuffer;
    const float *in = (const float *)inputBuffer;
    unsigned int i;
    (void)timeInfo; /* Prevent unused variable warnings. */
    (void)statusFlags;
    (void)userData;

    if (inputBuffer == NULL)
    {
        for (i = 0; i < framesPerBuffer; i++)
        {
            *out++ = 0; /* left - silent */
            // *out++ = 0; /* right - silent */
        }
        gNumNoInputs += 1;
    }
    else
    {
        for (i = 0; i < framesPerBuffer; i++)
        {
            float sample = *in++; /* MONO input */
            // *out++ = FUZZ(sample); /* left - distorted */
            *out++ = sample; /* right - clean */
        }
    }

    return paContinue;
}

static int receiver_callback(const void *inputBuffer, void *, unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags, void *userData)
{
    auto &receiver = *(reinterpret_cast<Receiver *>(userData));
    auto in = reinterpret_cast<const float *>(inputBuffer);
    receiver.recv_pcm_frames(in);
    return paContinue;
}

void start_sound_control()
{
    auto err = Pa_Initialize();
    if (err != paNoError)
    {
        std::cerr << Pa_GetErrorText(err) << "\n";
    }
}

void close_sound_control()
{
    auto err = Pa_Terminate();
    if (err != paNoError)
    {
        std::cerr << Pa_GetErrorText(err) << "\n";
    }
}

Receiver::Receiver(int _channels, int sample_rate, int _period)
    : iss(nullptr), channels(_channels), period(_period), frames(new float[channels * period]), ready(false)
{

    PaStreamParameters input_para;
    input_para.device = Pa_GetDefaultInputDevice();
    input_para.channelCount = channels;
    input_para.sampleFormat = paFloat32;
    input_para.suggestedLatency = Pa_GetDeviceInfo(input_para.device)->defaultLowInputLatency;
    input_para.hostApiSpecificStreamInfo = nullptr;

    // PaStreamParameters outputParameters;
    // outputParameters.device = Pa_GetDefaultOutputDevice();
    // outputParameters.channelCount = 1; /* stereo output */
    // outputParameters.sampleFormat = paFloat32;
    // outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    // outputParameters.hostApiSpecificStreamInfo = NULL;

    auto err = Pa_OpenStream(&iss, &input_para, nullptr, sample_rate, period, 0, receiver_callback, this);
    if (err != paNoError)
    {
        iss = nullptr;
        std::cerr << Pa_GetErrorText(err) << "\n";
    }
}

Receiver::~Receiver()
{
    if (iss)
    {
        Pa_CloseStream(iss);
    }
}

void Receiver::listen()
{
    auto err = Pa_StartStream(iss);
    // 1. Create an AudioBuffer
    AudioFile<float> audioFile;
    AudioFile<float>::AudioBuffer buffer;
    // 2. Set to (e.g.) two channels
    buffer.resize(channels);
    audioFile.setSampleRate(CUSTOM_SAMPLE_RATE);

    std::vector<float> pcm_buf;
    while (pcm_buf.size() < 10 * channels * CUSTOM_SAMPLE_RATE)
    {
        std::unique_lock<std::mutex> lck(mtx);
        if (cond.wait_for(lck, std::chrono::seconds(1), [this]()
                          { return ready; }))
        {
            std::copy(frames.get(), frames.get() + channels * period, std::back_inserter(pcm_buf));
            ready = false;
        }
        printf("%lu\n", pcm_buf.size());
    }

    for (size_t i = 0; i < pcm_buf.size(); i++)
    {
        buffer[i % 2 ? 1 : 0].push_back(pcm_buf[i]);
        // for (size_t j = 0; j < channels; j++)
        // {
        //     buffer[j].push_back(pcm_buf[i++]);
        // }
    }

    bool ok = audioFile.setAudioBuffer(buffer);
    // 5. Put into the AudioFile object
    audioFile.save("audioFile.wav");
}

void Receiver::recv_pcm_frames(const float *input)
{
    {
        std::lock_guard<std::mutex> grd(mtx);
        std::copy_n(input, channels * period, frames.get());
        ready = true;
    }
    cond.notify_one();
}