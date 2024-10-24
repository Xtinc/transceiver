#include "audio_network.h"
#include <cmath>

namespace
{
    constexpr char AUDIO_PACKET_MONO_CHAN = 1;
    constexpr char AUDIO_PACKET_DUAL_CHAN = 2;
    constexpr char MINIMUM_AUDIO_ENCODER_IDX = enum2val(AudioEncoderFormat::PCM);
    constexpr char MAXIMUM_AUDIO_ENCODER_IDX = enum2val(AudioEncoderFormat::OPUS);
    constexpr uint64_t fixedFraction = 1LL << 32;
    constexpr double normFixed = 1.0 / (1LL << 32);

    uint64_t ReSampleS16LE(const int16_t *input, int16_t *output, int fsi, int fso, uint64_t input_ps, uint32_t channels)
    {
        uint64_t output_ps = input_ps * fso / fsi;
        double stepDist = ((double)fsi / (double)fso);
        uint64_t step = ((uint64_t)(stepDist * fixedFraction + 0.5));
        uint64_t curOffset = 0;
        for (size_t i = 0; i < output_ps; i += 1)
        {
            for (size_t c = 0; c < channels; c += 1)
            {
                *output++ =
                    (int16_t)(input[c] + (input[c + channels] - input[c]) *
                                             ((double)(curOffset >> 32) + ((curOffset & (fixedFraction - 1)) * normFixed)));
            }
            curOffset += step;
            input += (curOffset >> 32) * channels;
            curOffset &= (fixedFraction - 1);
        }
        return output_ps;
    }
} // namespace

bool PacketHeader::validate(const char *data, size_t len)
{
    if (len < sizeof(PacketHeader))
    {
        return false;
    }

    if (data[1] != AUDIO_PACKET_MONO_CHAN && data[1] != AUDIO_PACKET_DUAL_CHAN)
    {
        return false;
    }

    if (cast_uint8_as_bandwidth((uint8_t)data[2]) == AudioBandWidth::Unknown)
    {
        return false;
    }

    if (data[3] < MINIMUM_AUDIO_ENCODER_IDX || data[3] > MAXIMUM_AUDIO_ENCODER_IDX)
    {
        return false;
    }

    return true;
}

SessionData::SessionData(size_t blk_sz, size_t blk_num, int _chan)
    : chan(_chan), max_len(2 * blk_sz * blk_num), enable(true), os(&buf), is(&buf)
{
    out_buf = new char[blk_sz];
    buf.prepare(max_len);
}

SessionData::~SessionData()
{
    delete[] out_buf;
}

void SessionData::store_data(const char *data, size_t len)
{
    lock spin(ready);
    if (buf.size() < max_len)
    {
        os.write(data, static_cast<std::streamsize>(len));
    }
}

void SessionData::load_data(size_t len)
{
    std::memset(out_buf, 0, len);
    lock spin(ready);
    if (buf.size() >= len)
    {
        is.read(out_buf, static_cast<std::streamsize>(len));
    }
    if (buf.size() > max_len)
    {
        buf.consume(max_len);
    }
}

NetEncoder::NetEncoder(uint8_t _sender, uint8_t _channel, int _period, AudioBandWidth _bandwidth)
    : head{_sender, _channel, cast_bandwidth_as_uint8(_bandwidth), 1, 0}, period(_period), os(&buf), encoder(nullptr),
      enc_buf(nullptr)
{
    buf.prepare(256);
    auto err = 0;
    encoder = opus_encoder_create(enum2val(_bandwidth), head.channel, OPUS_APPLICATION_AUDIO, &err);
    if (err != 0)
    {
        AUDIO_ERROR_PRINT("%s\n", opus_strerror(err));
    }
    enc_buf = new unsigned char[head.channel * sizeof(int16_t) * period];
}

NetEncoder::~NetEncoder()
{
    if (encoder)
    {
        opus_encoder_destroy(encoder);
    }

    delete[] enc_buf;
}

asio::streambuf &NetEncoder::prepare(const char *data, size_t len, size_t &out_len)
{
    auto opus_bytes = opus_encode(encoder, (const opus_int16 *)data, period, enc_buf, static_cast<opus_int32>(len));
    if (opus_bytes <= 0)
    {
        out_len = 0;
        AUDIO_ERROR_PRINT("%s\n", opus_strerror(opus_bytes));
        return buf;
    }

    head.timestamp =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    head.sequence++;
    os.write((const char *)&head, sizeof(head));
    os.write((const char *)enc_buf, opus_bytes);
    out_len = opus_bytes;
    return buf;
}

NetDecoder::NetDecoder(uint8_t _token, uint8_t _channel, int _bandwidth)
    : token(_token), chann(_channel), decoder(nullptr), dec_buf(nullptr), rsc_buf(nullptr),
      fsi(ceil_div(_bandwidth, 8000) * 8000), fso(_bandwidth), rnow_last(0), snow_last(0), iseq_last(0), pack_lost(0),
      jitter(0), recv_interv(0), send_interv(0), lost_rate(0), avg_jitter(0), avg_recv_interv(0), avg_send_interv(0)
{
    auto err = 0;
    decoder = opus_decoder_create(fsi, chann, &err);
    if (err != 0)
    {
        AUDIO_ERROR_PRINT("%s\n", opus_strerror(err));
    }
    dec_buf = new int16_t[enum2val(AudioBandWidth::Full) * chann * enum2val(AudioPeriodSize::INR_40MS) / 1000];
    if (fsi != fso)
    {
        rsc_buf = new int16_t[enum2val(AudioBandWidth::Full) * chann * enum2val(AudioPeriodSize::INR_40MS) / 1000];
    }
}

NetDecoder::~NetDecoder()
{
    if (decoder)
    {
        opus_decoder_destroy(decoder);
    }

    delete[] dec_buf;
    delete[] rsc_buf;
}

bool NetDecoder::commit(const char *data, size_t len, const char *&out_data, size_t &out_len)
{
    auto frame_nums = opus_decode(decoder, (unsigned char *)data + sizeof(PacketHeader),
                                  static_cast<opus_int32>(len - sizeof(PacketHeader)), dec_buf, 5760, 0);
    if (frame_nums <= 0)
    {
        return false;
    }

    PacketHeader head{};
    std::memcpy(&head, data, sizeof(head));
    auto snow = head.timestamp;
    auto iseq = head.sequence;
    auto rnow =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();

    if (iseq_last != 0)
    {
        auto rinterv = rnow > rnow_last ? (double)(rnow - rnow_last) : 0.0;
        auto sinterv = snow > snow_last ? (double)(snow - snow_last) : 0.0;
        recv_interv += (rinterv - recv_interv) / 16.0;
        send_interv += (sinterv - send_interv) / 16.0;
        jitter += (fabs(rinterv - sinterv) - jitter) / 16.0;
        if (iseq_last + 1 != iseq)
        {
            pack_lost++;
        }
    }

    if (iseq_last && iseq_last % 200 == 0)
    {
        std::lock_guard<std::mutex> grd(mtx);
        lost_rate = 100 * (double)pack_lost / iseq_last;
        avg_send_interv = send_interv;
        avg_recv_interv = recv_interv;
        avg_jitter = jitter;
    }

    snow_last = snow;
    rnow_last = rnow;
    iseq_last = iseq;

    if (fsi == fso)
    {
        out_len = sizeof(opus_int16) * frame_nums * chann;
        out_data = (const char *)dec_buf;
    }
    else
    {
        out_len = sizeof(opus_int16) * ReSampleS16LE(dec_buf, rsc_buf, fsi, fso, frame_nums, chann) * chann;
        out_data = (const char *)rsc_buf;
    }

    return true;
}

ChannelInfo NetDecoder::statistic_info()
{
    std::lock_guard<std::mutex> grd(mtx);
    return {token, lost_rate, avg_jitter, avg_recv_interv, avg_send_interv};
}

LocEncoder::LocEncoder(int inSampleRate, int outSampleRate, int channel)
    : fsi(inSampleRate), fso(outSampleRate), chan(channel), src_buf(nullptr)
{
    src_buf = new int16_t[8 * enum2val(AudioBandWidth::Full) * chan * enum2val(AudioPeriodSize::INR_40MS) / 1000];
}

LocEncoder::~LocEncoder()
{
    delete[] src_buf;
}

bool LocEncoder::commit(int16_t *input, size_t input_len, int16_t *&output, size_t &output_size)
{
    if (!input)
    {
        return false;
    }

    if (fsi == fso)
    {
        output = input;
        output_size = input_len;
        return true;
    }
    output_size = ReSampleS16LE(input, src_buf, fsi, fso, input_len, chan);
    output = src_buf;
    return true;
}
