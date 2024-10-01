#include "audio_stream.h"
#include "audio_device.h"
#include "audio_interface.h"
#include "audio_mixer.h"
#include "audio_network.h"

using udp = asio::ip::udp;
#define SERVICE (AudioService::GetService().executor())

static constexpr auto PCM_CUSTOM_PERIOD_SIZE = 1024;
static constexpr auto PCM_CUSTOM_SAMPLE_INRV = PCM_CUSTOM_PERIOD_SIZE * 1000 * 1000 / 48000;

inline constexpr uint16_t token2port(unsigned char token)
{
    return (uint16_t)(0xccu << 8) + (uint16_t)token;
}

void start_audio_service()
{
    AudioService::GetService().start();
    AUDIO_INFO_PRINT("compiled at %s %s\n", __DATE__, __TIME__);
}

void stop_audio_service()
{
    AudioService::GetService().stop();
}

// OAStream
OAStream::OAStream(unsigned char _token, const std::string &_hw_name, AudioBandWidth _bandwidth,
                   AudioPeriodSize _period, bool _enable_network)
{
    impl = std::make_shared<OAStreamImpl>(_token, _bandwidth, _period, _hw_name, _enable_network);
}

OAStream::~OAStream() = default;

bool OAStream::start()
{
    return impl->start();
}

void OAStream::stop()
{
    impl->stop();
}

void OAStream::direct_push_pcm(uint8_t input_token, uint8_t input_chan, int input_period, int sample_rate,
                               const int16_t *data)
{
    impl->direct_push_pcm(input_token, input_chan, input_period, sample_rate, data);
}

OAStreamImpl::OAStreamImpl(unsigned char _token, AudioBandWidth _bandwidth, AudioPeriodSize _period,
                           const std::string &_hw_name, bool _enable_network)
    : token(_token), enable_network(_enable_network), fs(enum2val(_bandwidth)), ps(enum2val(_period)), chan_num(0),
      max_chan(0), recv_buf(nullptr), oas_ready(false), timer(SERVICE)
{
    if (_hw_name.find(".pcm") != std::string::npos)
    {
        odevice = std::make_unique<WaveOADevice>(SERVICE);
    }
    else if (_hw_name.find(".multi") != std::string::npos)
    {
        odevice = std::make_unique<MultiOADevice>(3, 11, 16);
    }
    else
    {
        odevice = std::make_unique<PhsyOADevice>();
    }

    if (odevice->create(_hw_name, this, fs, ps, chan_num, max_chan))
    {
        // choose a bit large buffer size.
        recv_buf = new char[6 * PCM_CUSTOM_PERIOD_SIZE];
    }
}

OAStreamImpl::~OAStreamImpl()
{
    stop();
    delete[] recv_buf;
}

bool OAStreamImpl::start()
{
    if (oas_ready)
    {
        return true;
    }

    if (!odevice->start())
    {
        return false;
    }

    oas_ready = true;

    if (enable_network)
    {
        try
        {
            sock = std::make_unique<udp::socket>(SERVICE, udp::endpoint(udp::v4(), token2port(token)));
        }
        catch (const std::exception &e)
        {
            AUDIO_ERROR_PRINT("%s\n", e.what());
            return false;
        }

        asio::post(SERVICE, [self = shared_from_this()]()
                   { self->do_receive(); });
    }

    if (odevice->enable_external_loop())
    {
        exec_external_loop();
    }

    AUDIO_INFO_PRINT("start oastream\n");
    return true;
}

void OAStreamImpl::stop()
{
    if (!oas_ready)
    {
        return;
    }

    if (odevice->stop())
    {
        oas_ready = false;
    }
    AUDIO_INFO_PRINT("stop oastream\n");
}

void OAStreamImpl::write_pcm_frames(int16_t *output, int frame_number)
{
    std::memset(output, 0, chan_num * frame_number * sizeof(int16_t));
    for (const auto &s : net_sessions)
    {
        s.second->load_data(ps * s.second->chan * sizeof(int16_t));
        mix_channels((const int16_t *)s.second->out_buf, chan_num, s.second->chan, ps, (int16_t *)output);
    }
    for (const auto &s : loc_sessions)
    {
        s.second->load_data(ps * s.second->chan * sizeof(int16_t));
        mix_channels((const int16_t *)s.second->out_buf, chan_num, s.second->chan, ps, (int16_t *)output);
    }
}

void OAStreamImpl::exec_external_loop()
{
    if (!oas_ready)
    {
        return;
    }
    auto now = std::chrono::steady_clock::now();
    auto interval = ceil_div(ps * 1000, fs);
    if (!odevice->async_task(interval))
    {
        return;
    }
    timer.expires_at(now + asio::chrono::milliseconds(interval));
    timer.async_wait([self = shared_from_this()](const asio::error_code &ec)
                     {
        if (ec)
        {
            return;
        }
        self->exec_external_loop(); });
}

void OAStreamImpl::do_receive()
{
    if (!oas_ready)
    {
        return;
    }
    static udp::endpoint sender_endpoint;
    sock->async_receive_from(
        asio::buffer(recv_buf, PCM_CUSTOM_PERIOD_SIZE * 6), sender_endpoint,
        [self = shared_from_this()](std::error_code ec, std::size_t bytes)
        {
            if (!ec && PacketHeader::validate(self->recv_buf, bytes))
            {
                auto sender = self->recv_buf[0];
                auto chan = self->recv_buf[1];
                std::lock_guard<std::mutex> grd(self->dest_mtx);
                if (self->net_sessions.find(sender) == self->net_sessions.end())
                {
                    // auto session = std::make_unique<SessionData>(self->ps * chan * sizeof(int16_t), 6, chan);
                    self->decoders.insert({sender, std::make_unique<NetDecoder>(sender, chan, self->fs)});
                    self->net_sessions.insert(
                        {sender, std::make_unique<SessionData>(self->ps * chan * sizeof(int16_t), 6, chan)});
                    AUDIO_INFO_PRINT("new connection: %u\n", sender);
                }
                const char *decode_data = nullptr;
                size_t decode_length = 0;
                if (self->decoders.at(sender)->commit(self->recv_buf, bytes, decode_data, decode_length))
                {
                    self->net_sessions.at(sender)->store_data(decode_data, decode_length);
                }
            }
            self->do_receive();
        });
}

void OAStreamImpl::direct_push_pcm(uint8_t input_token, uint8_t input_chan, int input_period, int sample_rate,
                                   const int16_t *data)
{
    std::lock_guard<std::mutex> grd(dest_mtx);
    if (loc_sessions.find(input_token) == loc_sessions.end())
    {
        loc_sessions.insert(
            {input_token, std::make_unique<SessionData>(ps * input_chan * sizeof(int16_t), 3, input_chan)});
        samplers.insert({input_token, std::make_unique<LocEncoder>(sample_rate, fs, input_chan)});
        AUDIO_INFO_PRINT("new connection: %u\n", input_token);
    }
    int16_t *decode_data = nullptr;
    size_t decode_frame = 0;
    if (samplers.at(input_token)
            ->commit((int16_t *)data, input_period, decode_data, decode_frame))
    {
        loc_sessions.at(input_token)->store_data((const char *)decode_data, decode_frame * input_chan * sizeof(int16_t));
    }
}

// IAStream
IAStream::IAStream(unsigned char _token, const std::string &_hw_name, AudioBandWidth _bandwidth,
                   AudioPeriodSize _period, bool _enable_network)
{
    if (_bandwidth == AudioBandWidth::Unknown)
    {
        AUDIO_ERROR_PRINT("Sample rate unknown is not allowed for input stream.\n");
        _bandwidth = AudioBandWidth::Full;
    }
    impl = std::make_shared<IAStreamImpl>(_token, _bandwidth, _period, _hw_name, _enable_network);
}

IAStream::~IAStream() = default;

bool IAStream::start()
{
    return impl->start();
}

void IAStream::mute()
{
    impl->mute();
}

void IAStream::unmute()
{
    impl->unmute();
}

void IAStream::stop()
{
    impl->stop();
}

void IAStream::connect(const OAStream &sink)
{
    impl->connect(sink.impl);
}

bool IAStream::connect(const std::string &ip, unsigned char token)
{
    return impl->connect(ip, token2port(token));
}

void IAStream::set_callback(AudioInputCallBack _cb, void *_user_data)
{
    impl->set_callback(_cb, _user_data);
}

IAStreamImpl::IAStreamImpl(unsigned char _token, AudioBandWidth _bandwidth, AudioPeriodSize _period,
                           const std::string &_hw_name, bool _enable_network)
    : token(_token), enable_network(_enable_network), fs(enum2val(_bandwidth)), ps(fs / 1000 * (enum2val(_period))),
      chan_num(0), max_chan(0), muted(false), timer0(SERVICE), timer1(SERVICE), cb0(nullptr), usr_data0(nullptr),
      ias_ready(false)
{
    if (_hw_name.find(".wav") != std::string::npos)
    {
        idevice = std::make_unique<WaveIADevice>(SERVICE);
    }
    else if (_hw_name.find(".multi") != std::string::npos)
    {
        idevice = std::make_unique<MultiIADevice>(0, 8, 16);
    }
    else
    {
        idevice = std::make_unique<PhsyIADevice>();
    }

    if (idevice->create(_hw_name, this, fs, ps, chan_num, max_chan))
    {
        // session for raw data
        session = std::make_unique<SessionData>(PCM_CUSTOM_PERIOD_SIZE * max_chan * sizeof(int16_t), 2, max_chan);
    }

    if (enable_network)
    {
        encoder = std::make_unique<NetEncoder>(token, chan_num, ps, _bandwidth);
    }
}

IAStreamImpl::~IAStreamImpl()
{
    stop();
    if (cb1)
    {
        cb1();
    }
}

bool IAStreamImpl::start()
{
    if (ias_ready)
    {
        return true;
    }

    if (enable_network)
    {
        sock = std::make_unique<udp::socket>(SERVICE, udp::endpoint(udp::v4(), 0));
    }

    if (!idevice->start())
    {
        return false;
    }

    ias_ready = true;

    if (idevice->enable_external_loop())
    {
        exec_external_loop();
    }

    if (cb0)
    {
        copy_pcm_frames();
    }

    AUDIO_INFO_PRINT("start iastream :%u\n", token);
    return true;
}

void IAStreamImpl::mute()
{
    muted.store(true);
}

void IAStreamImpl::unmute()
{
    muted.store(false);
}

void IAStreamImpl::stop()
{
    if (!ias_ready)
    {
        return;
    }

    if (idevice->stop())
    {
        ias_ready = false;
    }
    AUDIO_INFO_PRINT("stop iastream :%u\n", token);
}

void IAStreamImpl::connect(const std::shared_ptr<OAStreamImpl> &sink)
{
    std::lock_guard<std::mutex> grd(dest_mtx);
    loc_dests.emplace_back(sink);
}

bool IAStreamImpl::connect(const std::string &ip, uint16_t port)
{
    if (!enable_network)
    {
        AUDIO_ERROR_PRINT("net transport is disabled\n");
        return false;
    }

    udp::resolver resolver(SERVICE);
    asio::error_code ec;

    auto dest = *resolver.resolve(udp::v4(), ip, std::to_string(port), ec).begin();
    if (ec)
    {
        AUDIO_ERROR_PRINT("%s\n", ec.message().c_str());
        return false;
    }
    std::lock_guard<std::mutex> grd(dest_mtx);
    net_dests.push_back(std::move(dest));
    return true;
}

void IAStreamImpl::set_callback(AudioInputCallBack _cb, void *_user_data)
{
    cb0 = _cb;
    usr_data0 = _user_data;
}

void IAStreamImpl::set_destory_callback(std::function<void()> &&_cb)
{
    cb1 = _cb;
}

void IAStreamImpl::set_resampler_parameter(int fsi, int fso, int chan)
{
    sampler = std::make_unique<LocEncoder>(fsi, fso, chan);
}

void IAStreamImpl::read_raw_frames(const int16_t *input, int frame_number)
{
    if (!cb0)
    {
        return;
    }

    session->store_data((const char *)input, frame_number * sizeof(int16_t) * max_chan);
}

void IAStreamImpl::read_pcm_frames(const int16_t *input, int frame_number)
{
    if (muted.load())
    {
        return;
    }

    std::lock_guard<std::mutex> grd(dest_mtx);
    for (const auto &dest : loc_dests)
    {
        if (auto np = dest.lock())
        {
            np->direct_push_pcm(token, chan_num, frame_number, fs, input);
        }
    }

    if (!enable_network)
    {
        return;
    }

    size_t len = 0;
    auto &msg = encoder->prepare((const char *)input, frame_number * sizeof(int16_t) * chan_num, len);
    for (const auto &dest : net_dests)
    {
        sock->async_send_to(msg.data(), dest, [](std::error_code, std::size_t) {});
    }
    msg.consume(len + sizeof(PacketHeader));
}

void IAStreamImpl::copy_pcm_frames()
{
    if (!ias_ready)
    {
        return;
    }
    auto now = std::chrono::steady_clock::now();
    session->load_data(PCM_CUSTOM_PERIOD_SIZE * max_chan * sizeof(int16_t));
    cb0((int16_t *)session->out_buf, max_chan, PCM_CUSTOM_PERIOD_SIZE, usr_data0);
    timer0.expires_at(now + asio::chrono::microseconds(PCM_CUSTOM_SAMPLE_INRV));
    timer0.async_wait([self = shared_from_this()](const asio::error_code &ec)
                      {
        if (ec)
        {
            return;
        }
        self->copy_pcm_frames(); });
}

void IAStreamImpl::exec_external_loop()
{
    if (!ias_ready)
    {
        return;
    }
    auto now = std::chrono::steady_clock::now();
    auto interval = ceil_div(ps * 1000, fs);
    if (!idevice->async_task(interval))
    {
        return;
    }
    timer1.expires_at(now + asio::chrono::milliseconds(interval));
    timer1.async_wait([self = shared_from_this()](const asio::error_code &ec)
                      {
        if (ec)
        {
            return;
        }
        self->exec_external_loop(); });
}

// AudioPlayer
AudioPlayer::AudioPlayer(unsigned char _token)
{
    impl = std::make_unique<AudioPlayerImpl>(_token);
}

AudioPlayer::~AudioPlayer() = default;

bool AudioPlayer::play(const std::string &name, const OAStream &sink)
{
    return impl->play(name, sink.impl);
}

bool AudioPlayer::play(const std::string &name, const std::string &ip, unsigned char token)
{
    return false;
}

void AudioPlayer::stop(const std::string &name)
{
    return impl->stop(name);
}

AudioPlayerImpl::AudioPlayerImpl(unsigned char _token) : token(_token), preemptive(0)
{
}

bool AudioPlayerImpl::play(const std::string &name, const std::shared_ptr<OAStreamImpl> &sink)
{
    return play_tmpl(name, sink);
}

bool AudioPlayerImpl::play(const std::string &name, const std::string &ip, unsigned char token)
{
    return play_tmpl(name, ip, token);
}

void AudioPlayerImpl::stop(const std::string &name)
{
    std::lock_guard<std::mutex> grd(mtx);
    if (sounds.find(name) != sounds.cend())
    {
        if (auto np = sounds.at(name).lock())
        {
            np->stop();
            preemptive--;
        }
    }
}