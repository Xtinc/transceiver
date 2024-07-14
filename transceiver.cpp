#include "transceiver.h"
#include "portaudio.h"
#include "wave.h"
#include <iostream>

// debug
WAVFile wav_file(2, 48'000, 32);

using asio::ip::udp;

static constexpr char MAGIC_NUM_1 = 0xab;
static constexpr char MAGIC_NUM_2 = 0xcd;
static constexpr char MONO_CHAN = 1;
static constexpr char DUAL_CHAN = 2;
static constexpr size_t HEADER_LEN = 8;

static int io_callback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                       const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags, void *userData)
{
    reinterpret_cast<TransCeiver *>(userData)->send_pcm_frames(inputBuffer);
    reinterpret_cast<TransCeiver *>(userData)->recv_pcm_frames(outputBuffer);
    return paContinue;
}

static void mix_channels(float *output, float *ssrc, int out_chan, int ssrc_chan, int frames_num)
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

TransCeiver::TransCeiver(char _token, short _port, int _sample_rate, int _period)
    : token(_token), ios(nullptr), sample_rate(_sample_rate), period(_period),
      sock(io_context, udp::endpoint(udp::v4(), _port))
{
    PaStreamParameters input_para;
    input_para.device = Pa_GetDefaultInputDevice();
    auto input_default_info = Pa_GetDeviceInfo(input_para.device);
    ichan_num = input_default_info->maxInputChannels > 1 ? 2 : 1;
    input_para.channelCount = ichan_num;
    input_para.sampleFormat = paFloat32;
    input_para.suggestedLatency = input_default_info->defaultLowInputLatency;
    input_para.hostApiSpecificStreamInfo = nullptr;

    PaStreamParameters output_para;
    output_para.device = Pa_GetDefaultOutputDevice();
    auto output_default_info = Pa_GetDeviceInfo(output_para.device);
    ochan_num = output_default_info->maxOutputChannels > 1 ? 2 : 1;
    output_para.channelCount = ochan_num;
    output_para.sampleFormat = paFloat32;
    output_para.suggestedLatency = Pa_GetDeviceInfo(output_para.device)->defaultLowOutputLatency;
    output_para.hostApiSpecificStreamInfo = nullptr;

    auto err = Pa_OpenStream(&ios, &input_para, &output_para, sample_rate, period, 0, io_callback, this);
    if (err != paNoError)
    {
        ios = nullptr;
        std::cerr << Pa_GetErrorText(err) << "\n";
    }
    send_session = std::make_unique<Session>(ichan_num * period * sizeof(float) + HEADER_LEN, 1, ichan_num);
}

TransCeiver::~TransCeiver()
{
    if (ios)
    {
        stop();
    }
}

bool TransCeiver::connect(const std::string &ip, short port)
{
    udp::resolver resolver(io_context);
    asio::error_code ec;

    dest = *resolver.resolve(udp::v4(), ip, std::to_string(port), ec).begin();
    if (ec)
    {
        std::cerr << ec << "\n";
        return false;
    }
    return true;
}

void TransCeiver::start()
{
    if (!ios)
    {
        return;
    }
    Pa_StartStream(ios);
    do_receive();
    io_thd = std::make_unique<std::thread>([this]()
                                           { io_context.run(); });
}

void TransCeiver::stop()
{
    if (ios)
    {
        Pa_CloseStream(ios);
        ios = nullptr;
    }
    io_context.stop();
    if (io_thd)
    {
        io_thd->join();
    }
}

void TransCeiver::play(const std::string &filename)
{
    WAVFile wav_file(2, 48'000, 32);
    if (wav_file.open(filename))
    {
        auto bytes = 2 * period * sizeof(float);
        auto interval = period * 1000 / 48'000;
        auto tp = std::chrono::system_clock::now();
        for (size_t i = 0; i < wav_file.size(); i += bytes)
        {
            send_session->assemble_pack(token + 1, wav_file.data() + i, bytes);
            sock.async_send_to(send_session->buf.data(), dest, [](std::error_code /*ec*/, std::size_t) {});
            send_session->buf.consume(bytes + HEADER_LEN);
            tp += std::chrono::milliseconds(interval);
            std::this_thread::sleep_until(tp);
        }
    }
}

void TransCeiver::do_receive()
{
    udp::endpoint sender_endpoint;
    sock.async_receive_from(asio::buffer(recv_buf), sender_endpoint, [this](std::error_code ec, std::size_t bytes)
                            {
        if (!ec && bytes > HEADER_LEN && validate_pack())
        {
            auto sender = recv_buf[0];
            auto chan = (unsigned char)recv_buf[1];
            if (sessions.find(sender) == sessions.end())
            {
                sessions.insert({sender, std::make_unique<Session>(period * chan * sizeof(float), 8, chan)});
            }
            sessions.at(recv_buf[0])->store_data(recv_buf.data() + HEADER_LEN, bytes - HEADER_LEN);
        }
        do_receive(); });
}

bool TransCeiver::validate_pack() const
{
    auto chan_num = (unsigned char)recv_buf[1];
    if (chan_num != MONO_CHAN && chan_num != DUAL_CHAN)
    {
        return false;
    }

    if (recv_buf[2] != MAGIC_NUM_1 || recv_buf[3] != MAGIC_NUM_2)
    {
        return false;
    }
    return true;
}

void TransCeiver::send_pcm_frames(const void *input)
{
    auto bytes = ichan_num * period * sizeof(float);
    send_session->assemble_pack(token, reinterpret_cast<const char *>(input), bytes);
    sock.async_send_to(send_session->buf.data(), dest, [](std::error_code /*ec*/, std::size_t) {});
    // debug
    // wav_file.write(reinterpret_cast<const char *>(input), bytes);
    // if (wav_file.size() > sample_rate * 2 * 4)
    // {
    //     printf("save wav\n");
    //     wav_file.save("rec.wav");
    // }
    send_session->buf.consume(bytes + HEADER_LEN);
}

void TransCeiver::recv_pcm_frames(void *output)
{
    std::memset(output, 0, ochan_num * period * sizeof(float));
    for (auto &s : sessions)
    {
        auto out = reinterpret_cast<float *>(output);
        s.second->load_data(period * s.second->chan * sizeof(float));
        auto ssrc = reinterpret_cast<float *>(s.second->out_buf);
        mix_channels(out, ssrc, ochan_num, s.second->chan, period);
    }
}