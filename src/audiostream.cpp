#include "audiostream.h"
#include "portaudio.h"
#include "audiomixer.h"

#define AUDIO_ERROR_PRINT(str) printf("audio error: %s\n", str)

namespace
{
    template <typename T>
    constexpr typename std::underlying_type<T>::type enum2val(T e)
    {
        return static_cast<typename std::underlying_type<T>::type>(e);
    }

    int output_callback(const void *, void *outputBuffer, unsigned long framesPerBuffer,
                        const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags, void *userData)
    {
        reinterpret_cast<OAStream *>(userData)->write_pcm_frames(outputBuffer);
        return paContinue;
    }

    int input_callback(const void *inputBuffer, void *, unsigned long framesPerBuffer,
                       const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags, void *userData)
    {
        reinterpret_cast<IAStream *>(userData)->read_pcm_frames(inputBuffer);
        return paContinue;
    }
} // namespace

class Session
{
    struct lock
    {
        explicit lock(std::atomic_flag &flag) : m_flag(flag)
        {
            while (m_flag.test_and_set(std::memory_order_acquire))
                ;
        }

        ~lock()
        {
            m_flag.clear(std::memory_order_release);
        }

    private:
        std::atomic_flag &m_flag;
    };

public:
    Session(size_t blk_sz, size_t blk_num, int _chan) : chan(_chan), ready{ATOMIC_FLAG_INIT}, os(&buf), is(&buf)
    {
        out_buf = new char[blk_sz];
        buf.prepare(blk_sz * blk_num);
    }

    ~Session()
    {
        delete[] out_buf;
    }

    void store_data(const char *data, size_t len)
    {
        lock spin(ready);
        os.write(data, len);
    }

    void load_data(size_t len)
    {
        std::memset(out_buf, 0, len);
        lock spin(ready);
        if (buf.size() >= len)
        {
            is.read(out_buf, len);
        }
    }

public:
    char *out_buf;
    asio::streambuf buf;
    const int chan;

private:
    std::atomic_flag ready;
    std::ostream os;
    std::istream is;
};

OAStream::OAStream(unsigned char _token, short _port, AudioBandWidth _bandwidth, AudioPeriodSize _period)
    : token(_token), fs(enum2val(_bandwidth)), ps(fs / 1000 * (enum2val(_period))), chan_num(0), os(nullptr)
{
    PaStreamParameters output_para;
    output_para.device = Pa_GetDefaultOutputDevice();
    auto output_default_info = Pa_GetDeviceInfo(output_para.device);
    chan_num = output_default_info->maxOutputChannels > 1 ? 2 : 1;
    output_para.channelCount = chan_num;
    output_para.sampleFormat = paInt16;
    output_para.suggestedLatency = Pa_GetDeviceInfo(output_para.device)->defaultLowOutputLatency;
    output_para.hostApiSpecificStreamInfo = nullptr;

    auto err = Pa_OpenStream(&os, NULL, &output_para, fs, ps, 0, output_callback, this);
    if (err != paNoError)
    {
        os = nullptr;
        AUDIO_ERROR_PRINT(Pa_GetErrorText(err));
    }
}

OAStream::~OAStream()
{
}

void OAStream::start()
{
    if (!os)
    {
        return;
    }
    auto err = Pa_StartStream(os);
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT(Pa_GetErrorText(err));
    }
}

void OAStream::stop()
{
    if (os)
    {
        auto err = Pa_CloseStream(os);
        if (err != paNoError)
        {
            AUDIO_ERROR_PRINT(Pa_GetErrorText(err));
            return;
        }
        os = nullptr;
    }
}

void OAStream::write_pcm_frames(void *output)
{
    std::memset(output, 0, chan_num * ps * sizeof(int16_t));
    for (auto &s : sessions)
    {
        s.second->load_data(ps * s.second->chan * sizeof(int16_t));
        mix_channels((const int16_t *)s.second->out_buf, chan_num, s.second->chan, ps, (int16_t *)output);
    }
}

void OAStream::recv_audio_packet(const char *data, size_t bytes)
{
    if (!AudioPackHeader::validate(data, bytes))
    {
        return;
    }
    AudioPackHeader head;
    std::memcpy(&head, data, sizeof(head));
    std::lock_guard<std::mutex> grd(recv_mtx);
    if (sessions.find(head.sender) == sessions.end())
    {
        sessions.insert({head.sender, std::make_unique<Session>(ps * head.channel * sizeof(int16_t), 8, head.channel)});
    }
    sessions.at(head.sender)->store_data(data + sizeof(AudioPackHeader), bytes - sizeof(AudioPackHeader));
}

IAStream::IAStream(unsigned char _token, AudioBandWidth _bandwidth, AudioPeriodSize _period)
    : token(_token), fs(enum2val(_bandwidth)), ps(fs / 1000 * (enum2val(_period))), chan_num(0), is(nullptr)
{
    PaStreamParameters input_para;
    input_para.device = Pa_GetDefaultInputDevice();
    auto input_default_info = Pa_GetDeviceInfo(input_para.device);
    chan_num = input_default_info->maxInputChannels > 1 ? 2 : 1;
    input_para.channelCount = chan_num;
    input_para.sampleFormat = paFloat32;
    input_para.suggestedLatency = input_default_info->defaultLowInputLatency;
    input_para.hostApiSpecificStreamInfo = nullptr;

    auto err = Pa_OpenStream(&is, &input_para, NULL, fs, ps, 0, input_callback, this);
    if (err != paNoError)
    {
        is = nullptr;
        AUDIO_ERROR_PRINT(Pa_GetErrorText(err));
    }
}

void IAStream::read_pcm_frames(const void *input)
{
}
