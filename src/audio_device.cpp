#include "audio_device.h"
#include "audio_network.h"
#include "audio_stream.h"
#include "audio_mixer.h"
#include "portaudio.h"
#include <cmath>

#if defined(_WIN64)
#include <timeapi.h>
#endif

namespace
{
    int get_specified_device(const std::string &card)
    {
        if (card == "default_input")
        {
            return Pa_GetDefaultInputDevice();
        }
        else if (card == "default_output")
        {
            return Pa_GetDefaultOutputDevice();
        }
        else
        {
            auto dev_num = Pa_GetDeviceCount();
            if (dev_num < 0)
            {
                AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(dev_num));
                return paNoDevice;
            }
            std::string card_name, sub_card_name;
            auto pos = card.find(',');
            if (pos != std::string::npos)
            {
                card_name = card.substr(0, pos);
                sub_card_name = card.substr(pos);
            }
            else
            {
                card_name = card;
            }

            for (int i = 0; i < dev_num; i++)
            {
                std::string dev_name = Pa_GetDeviceInfo(i)->name;
                AUDIO_INFO_PRINT("search snd card:%s\n", dev_name.c_str());
                if (dev_name.find(card_name) != std::string::npos)
                {
                    if (sub_card_name.empty())
                    {
                        return i;
                    }

                    if (dev_name.find(sub_card_name) != std::string::npos)
                    {
                        return i;
                    }
                }
            }
            return paNoDevice;
        }
    }

    int output_callback(const void *, void *outputBuffer, unsigned long frame_number, const PaStreamCallbackTimeInfo *,
                        PaStreamCallbackFlags, void *userData)
    {
        reinterpret_cast<AudioDevice *>(userData)->transfer_pcm_data((int16_t *)outputBuffer, (int)frame_number);
        return paContinue;
    }

    int input_callback(const void *inputBuffer, void *, unsigned long frame_number, const PaStreamCallbackTimeInfo *,
                       PaStreamCallbackFlags, void *userData)
    {
        reinterpret_cast<AudioDevice *>(userData)->transfer_pcm_data((int16_t *)inputBuffer, (int)frame_number);
        return paContinue;
    }
} // namespace

// AudioService
AudioService &AudioService::GetService()
{
    static AudioService instance;
    return instance;
}

AudioService::AudioService() : work_guard(io_ctx.get_executor())
{
}

void AudioService::start()
{
#ifdef _WIN64
    timeBeginPeriod(1);
#endif
    auto err = Pa_Initialize();
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(err));
        return;
    }
    for (std::size_t i = 0; i < 2; ++i)
    {
        io_thds.emplace_back([this]()
                             {
#ifdef __linux__
                                pthread_t thread=pthread_self();
                                struct sched_param param;
                                param.sched_priority = 10;
                                pthread_setschedparam(thread, SCHED_RR, &param);
                                pthread_setname_np(thread, "audio_thrdpool");
#endif
                                io_ctx.run(); });
    }
}

void AudioService::stop()
{
#ifdef _WIN64
    timeEndPeriod(1);
#endif
    io_ctx.stop();
    for (auto &thread : io_thds)
    {
        thread.join();
    }

    auto err = Pa_Terminate();
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(err));
    }
}

asio::io_context &AudioService::executor()
{
    return io_ctx;
}

// Phsy Input Device
PhsyIADevice::~PhsyIADevice()
{
    if (!device)
    {
        return;
    }

    auto err = Pa_CloseStream(device);
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(err));
    }
}

bool PhsyIADevice::create(const std::string &name, void *cls, int &fs, int &ps, int &chan, int &max_chan)
{
    stream = reinterpret_cast<IAStreamImpl *>(cls);
    auto device_number = get_specified_device(name);
    if (device_number == paNoDevice)
    {
        AUDIO_ERROR_PRINT("Invalid device\n");
        return false;
    }
    PaStreamParameters input_para;
    input_para.device = device_number;
    auto in_default_info = Pa_GetDeviceInfo(input_para.device);
    max_chan = in_default_info->maxInputChannels;
    max_chan = chan = max_chan > 1 ? 2 : 1;
    input_para.channelCount = chan;
    input_para.sampleFormat = paInt16;
    input_para.suggestedLatency = in_default_info->defaultLowInputLatency;
    input_para.hostApiSpecificStreamInfo = nullptr;

    PaError err;
    if (Pa_IsFormatSupported(&input_para, nullptr, fs) == paNoError)
    {
        err = Pa_OpenStream(&device, &input_para, nullptr, fs, ps, 0, input_callback, this);
    }
    else
    {
        AUDIO_INFO_PRINT("require fs %d, resample from %f\n", fs, in_default_info->defaultSampleRate);
        stream->set_resampler_parameter((int)in_default_info->defaultSampleRate, fs, chan);
        int resample_period = ceil_div(ps * in_default_info->defaultSampleRate, fs);
        err = Pa_OpenStream(&device, &input_para, nullptr, in_default_info->defaultSampleRate, resample_period, 0,
                            input_callback, this);
    }
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(err));
        return false;
    }
    AUDIO_INFO_PRINT("open idevice: %s, ichan = %d, max_chan = %d, fs = %d, ps = %d\n", in_default_info->name, chan,
                     max_chan, fs, ps);
    ready = true;
    return true;
}

bool PhsyIADevice::start()
{
    if (!ready)
    {
        AUDIO_ERROR_PRINT("device created failed. would not be opened.\n");
        return false;
    }

    auto err = Pa_StartStream(device);
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(err));
        return false;
    }
    return true;
}

bool PhsyIADevice::stop()
{
    auto err = Pa_StopStream(device);
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(err));
        return false;
    }
    return true;
}

void PhsyIADevice::transfer_pcm_data(int16_t *input, int frame_number)
{
    if (fit_sliences(input, stream->chan_num, frame_number))
    {
        return;
    }

    if (stream->sampler)
    {
        int16_t *out = input;
        size_t out_frames = 0;
        stream->sampler->commit(input, frame_number, out, out_frames);
        stream->read_raw_frames(out, (int)out_frames);
        stream->read_pcm_frames(out, (int)out_frames);
    }
    else
    {
        stream->read_raw_frames(input, frame_number);
        stream->read_pcm_frames(input, frame_number);
    }
}

// Wave Input Device
WaveIADevice::~WaveIADevice()
{
    delete[] pick_ups;
}

bool WaveIADevice::create(const std::string &name, void *cls, int &fs, int &ps, int &chan, int &max_chan)
{
    auto err = ifs.open(name, WavFile::mode::in);
    if (err != WavErrorCode::NoError)
    {
        AUDIO_ERROR_PRINT("%s\n", WavError2str(err));
        return false;
    }
    iastream = reinterpret_cast<IAStreamImpl *>(cls);
    max_chan = chan = ifs.channel_number();
    if (fs != ifs.sample_rate())
    {
        AUDIO_INFO_PRINT("require fs %d, resample from %u\n", fs, ifs.sample_rate());
        iastream->set_resampler_parameter((int)ifs.sample_rate(), fs, chan);
    }
    auto max_pickup_size = std::max(ps, ceil_div(ps * ifs.sample_rate(), fs));
    pick_ups = new int16_t[max_pickup_size * chan];
    AUDIO_INFO_PRINT("file idevice: %s, ochan = %d, max_chan = %d, fs = %d, ps = %d\n", name.c_str(), chan, max_chan,
                     fs, ps);
    ready = true;
    return true;
}

bool WaveIADevice::start()
{
    if (!ready)
    {
        AUDIO_ERROR_PRINT("device created failed. would not be opened.\n");
        return false;
    }
    return true;
}

bool WaveIADevice::stop()
{
    timer.cancel();
    return true;
}

bool WaveIADevice::async_task(int interv)
{
    auto frame_number = ceil_div(interv * ifs.sample_rate(), 1000);
    if (ifs.tell() + frame_number >= ifs.frame_number())
    {
        return false;
    }
    auto err = ifs.read(pick_ups, frame_number);
    if (err != WavErrorCode::NoError)
    {
        AUDIO_ERROR_PRINT("%s\n", WavError2str(err));
        return false;
    }
    if (iastream->sampler)
    {
        int16_t *out = pick_ups;
        size_t out_frames = 0;
        iastream->sampler->commit(pick_ups, frame_number, out, out_frames);
        iastream->read_raw_frames(out, (int)out_frames);
        iastream->read_pcm_frames(out, (int)out_frames);
    }
    else
    {
        iastream->read_raw_frames(pick_ups, (int)frame_number);
        iastream->read_pcm_frames(pick_ups, (int)frame_number);
    }
    return true;
}

bool WaveIADevice::enable_external_loop() const
{
    return true;
}

// Phys Multi Input Device
MultiIADevice::~MultiIADevice()
{
    if (!device)
    {
        return;
    }

    delete[] pick_ups;

    auto err = Pa_CloseStream(device);
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(err));
    }
}

bool MultiIADevice::create(const std::string &name, void *cls, int &fs, int &ps, int &chan, int &max_chan)
{
    auto pos = name.find(".multi");
    if (pos == std::string::npos)
    {
        AUDIO_ERROR_PRINT("invalid device name\n");
        return false;
    }
    auto device_number = get_specified_device(name.substr(0, pos));
    if (device_number == paNoDevice)
    {
        AUDIO_ERROR_PRINT("Invalid device\n");
        return false;
    }
    PaStreamParameters input_para;
    input_para.device = device_number;
    auto in_default_info = Pa_GetDeviceInfo(input_para.device);
    auto default_max_chan = in_default_info->maxInputChannels;
    if (max_pick < 2 || max_pick > default_max_chan)
    {
        AUDIO_ERROR_PRINT("invalid total channel numbers\n");
        return false;
    }
    if (input_l >= max_pick || input_r >= max_pick)
    {
        AUDIO_ERROR_PRINT("invalid channel number\n");
        return false;
    }

    chan = 2;
    input_para.channelCount = max_chan = max_pick;
    input_para.sampleFormat = paInt16;
    input_para.suggestedLatency = in_default_info->defaultLowInputLatency;
    input_para.hostApiSpecificStreamInfo = nullptr;

    auto err = Pa_OpenStream(&device, &input_para, nullptr, fs, ps, 0, input_callback, this);
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(err));
        return false;
    }
    stream = static_cast<IAStreamImpl *>(cls);
    pick_ups = new int16_t[ps * chan];
    AUDIO_INFO_PRINT("open idevice: %s, ichan = %d, max_chan = %d, fs = %d, ps = %d\n", in_default_info->name, chan,
                     max_chan, fs, ps);
    ready = true;
    return true;
}

bool MultiIADevice::start()
{
    if (!ready)
    {
        AUDIO_ERROR_PRINT("device created failed. would not be opened.\n");
        return false;
    }

    auto err = Pa_StartStream(device);
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(err));
        return false;
    }
    return true;
}

bool MultiIADevice::stop()
{
    auto err = Pa_StopStream(device);
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(err));
        return false;
    }
    return true;
}

void MultiIADevice::transfer_pcm_data(int16_t *input, int frame_number)
{
    if (fit_sliences(input, stream->chan_num, frame_number))
    {
        return;
    }

    stream->read_raw_frames(input, frame_number);
    auto dst = pick_ups;
    for (size_t i = 0; i < frame_number; i++)
    {
        *dst++ = input[input_l];
        *dst++ = input[input_r];
        input += max_pick;
    }
    stream->read_pcm_frames(pick_ups, frame_number);
}

// Phys Output Device
PhsyOADevice::~PhsyOADevice()
{
    if (!device)
    {
        return;
    }

    auto err = Pa_CloseStream(device);
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(err));
    }
}

bool PhsyOADevice::create(const std::string &name, void *cls, int &fs, int &ps, int &chan, int &max_chan)
{
    auto device_number = get_specified_device(name);
    if (device_number == paNoDevice)
    {
        AUDIO_ERROR_PRINT("Invalid device\n");
        return false;
    }
    PaStreamParameters output_para;
    output_para.device = device_number;
    auto output_default_info = Pa_GetDeviceInfo(output_para.device);
    max_chan = output_default_info->maxOutputChannels;
    max_chan = chan = max_chan > 1 ? 2 : 1;
    if (fs == 0)
    {
        fs = output_default_info->defaultSampleRate;
    }
    ps = ceil_div(ps * fs, 1000);
    output_para.channelCount = chan;
    output_para.sampleFormat = paInt16;
    output_para.suggestedLatency = Pa_GetDeviceInfo(output_para.device)->defaultLowOutputLatency;
    output_para.hostApiSpecificStreamInfo = nullptr;

    auto err = Pa_OpenStream(&device, nullptr, &output_para, fs, ps, 0, output_callback, this);
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(err));
        return false;
    }
    stream = static_cast<OAStreamImpl *>(cls);
    AUDIO_INFO_PRINT("open odevice: %s, ochan = %d, max_chan = %d, fs = %d, ps = %d\n", output_default_info->name, chan,
                     max_chan, fs, ps);
    ready = true;
    return true;
}

bool PhsyOADevice::start()
{
    if (!ready)
    {
        AUDIO_ERROR_PRINT("device created failed. would not be opened.\n");
        return false;
    }

    auto err = Pa_StartStream(device);
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(err));
        return false;
    }
    return true;
}

bool PhsyOADevice::stop()
{
    auto err = Pa_StopStream(device);
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(err));
        return false;
    }
    return true;
}

void PhsyOADevice::transfer_pcm_data(int16_t *input, int frame_number)
{
    stream->write_pcm_frames(input, frame_number);
}

// Wave Output Device
WaveOADevice::~WaveOADevice()
{
    delete[] pick_ups;
}

bool WaveOADevice::create(const std::string &name, void *cls, int &fs, int &ps, int &chan, int &max_chan)
{
    ofs.open(name, std::ios::binary);
    if (!ofs)
    {
        return false;
    }
    oastream = static_cast<OAStreamImpl *>(cls);
    max_chan = chan = 1;
    if (fs == 0)
    {
        fs = 48000;
    }
    ps = ceil_div(ps * fs, 1000);
    pick_ups = new int16_t[ps * chan];
    AUDIO_INFO_PRINT("file odevice: %s, ochan = %d, max_chan = %d, fs = %d, ps = %d\n", name.c_str(), chan, max_chan,
                     fs, ps);
    ready = true;
    return true;
}

bool WaveOADevice::start()
{
    if (!ready)
    {
        AUDIO_ERROR_PRINT("device created failed. would not be opened.\n");
        return false;
    }
    return true;
}

bool WaveOADevice::stop()
{
    timer.cancel();
    return true;
}

bool WaveOADevice::async_task(int interv)
{
    auto frame_number = ceil_div(interv * oastream->fs, 1000);
    oastream->write_pcm_frames(pick_ups, frame_number);
    if (!ofs.write((const char *)pick_ups, (std::streamsize)(sizeof(int16_t) * frame_number)))
    {
        return false;
    }
    return true;
}

bool WaveOADevice::enable_external_loop() const
{
    return true;
}

// Phys Multi Output Device
MultiOADevice::~MultiOADevice()
{
    if (!device)
    {
        return;
    }

    delete[] pick_ups;

    auto err = Pa_CloseStream(device);
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(err));
    }
}

bool MultiOADevice::create(const std::string &name, void *cls, int &fs, int &ps, int &chan, int &max_chan)
{
    auto pos = name.find(".multi");
    if (pos == std::string::npos)
    {
        AUDIO_ERROR_PRINT("invalid device name\n");
        return false;
    }
    auto device_number = get_specified_device(name.substr(0, pos));
    if (device_number == paNoDevice)
    {
        AUDIO_ERROR_PRINT("Invalid device\n");
        return false;
    }
    PaStreamParameters output_para;
    output_para.device = device_number;
    auto output_default_info = Pa_GetDeviceInfo(output_para.device);
    auto default_max_chan = output_default_info->maxInputChannels;
    if (max_pick < 2 || max_pick > default_max_chan)
    {
        AUDIO_ERROR_PRINT("invalid total channel numbers\n");
        return false;
    }
    if (input_l >= max_pick || input_r >= max_pick)
    {
        AUDIO_ERROR_PRINT("invalid channel number\n");
        return false;
    }

    chan = 2;
    if (fs == 0)
    {
        fs = output_default_info->defaultSampleRate;
    }
    ps = ps * fs / 1000;
    output_para.channelCount = max_chan = max_pick;
    output_para.sampleFormat = paInt16;
    output_para.suggestedLatency = Pa_GetDeviceInfo(output_para.device)->defaultLowOutputLatency;
    output_para.hostApiSpecificStreamInfo = nullptr;

    auto err = Pa_OpenStream(&device, nullptr, &output_para, fs, ps, 0, output_callback, this);
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(err));
        return false;
    }
    stream = static_cast<OAStreamImpl *>(cls);
    AUDIO_INFO_PRINT("open odevice: %s, ochan = %d, max_chan = %d, fs = %d, ps = %d\n", output_default_info->name, chan,
                     max_chan, fs, ps);
    pick_ups = new int16_t[ps * chan];
    ready = true;
    return true;
}

bool MultiOADevice::start()
{
    if (!ready)
    {
        AUDIO_ERROR_PRINT("device created failed. would not be opened.\n");
        return false;
    }

    auto err = Pa_StartStream(device);
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(err));
        return false;
    }
    return true;
}

bool MultiOADevice::stop()
{
    auto err = Pa_StopStream(device);
    if (err != paNoError)
    {
        AUDIO_ERROR_PRINT("%s\n", Pa_GetErrorText(err));
        return false;
    }
    return true;
}

void MultiOADevice::transfer_pcm_data(int16_t *input, int frame_number)
{
    stream->write_pcm_frames(pick_ups, frame_number);
    auto src = pick_ups;
    for (size_t i = 0; i < frame_number; i++)
    {
        input[input_l] = *src++;
        input[input_r] = *src++;
        input += max_pick;
    }
}
