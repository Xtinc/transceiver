#ifndef AUDIO_NETWORK_HEADER
#define AUDIO_NETWORK_HEADER

/*                    Packet Frame Format
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  sender id   |    channels   |  sample rate  | encoder format |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                            sequence                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           timestamp                           |
+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
|                            payload                            |
|                             ....                              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/

#include "asio.hpp"
#include "audio_interface.h"
#include "opus.h"

#define AUDIO_INFO_PRINT(fmt, ...) printf("[INF] %s(%d): " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define AUDIO_ERROR_PRINT(fmt, ...) printf("[ERR] %s(%d): " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)

enum class AudioEncoderFormat : uint8_t
{
    PCM = 0,
    OPUS = 1
};

template <typename T>
constexpr typename std::underlying_type<T>::type enum2val(T e)
{
    return static_cast<typename std::underlying_type<T>::type>(e);
}
constexpr int ceil_div(int a, int b)
{
    return (a + b - 1) / b;
}

constexpr uint8_t cast_bandwidth_as_uint8(AudioBandWidth bandwidth)
{
    uint8_t result = 0;
    switch (bandwidth)
    {
    case AudioBandWidth::Narrow:
        result = 8;
        break;
    case AudioBandWidth::Wide:
        result = 16;
        break;
    case AudioBandWidth::SemiSuperWide:
        result = 24;
        break;
    case AudioBandWidth::Full:
        result = 48;
        break;
    default:
        break;
    }
    return result;
}

constexpr AudioBandWidth cast_uint8_as_bandwidth(uint8_t sample_rate)
{
    switch (sample_rate)
    {
    case 8:
        return AudioBandWidth::Narrow;
    case 16:
        return AudioBandWidth::Wide;
    case 24:
        return AudioBandWidth::SemiSuperWide;
    case 48:
        return AudioBandWidth::Full;
    default:
        return AudioBandWidth::Unknown;
    }
}

struct ChannelInfo
{
    uint8_t token;
    double lost_rate;
    double jitter;
    double recv_interv;
    double send_interv;
};

struct PacketHeader
{
    uint8_t sender;
    uint8_t channel;
    uint8_t fs_rate;
    uint8_t enc_fmt;
    uint32_t sequence;
    uint64_t timestamp;

    static bool validate(const char *data, size_t len);
};

class SessionData
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
    SessionData(size_t blk_sz, size_t blk_num, int _chan);

    ~SessionData();

    void store_data(const char *data, size_t len);

    void load_data(size_t len);

public:
    const int chan;
    const size_t max_len;
    asio::streambuf buf;
    char *out_buf;

private:
    std::atomic_flag ready = ATOMIC_FLAG_INIT;
    std::ostream os;
    std::istream is;
};

class LocEncoder
{
public:
    LocEncoder(int input_fs, int output_fs, int channel);
    ~LocEncoder();

    bool commit(int16_t *input, size_t input_len, int16_t *&output, size_t &output_size);

private:
    const int fsi;
    const int fso;
    const int chan;

    int16_t *src_buf;
};

class NetEncoder
{
public:
    NetEncoder(uint8_t _sender, uint8_t _channel, int _period, AudioBandWidth _bandwidth);
    ~NetEncoder();

    asio::streambuf &prepare(const char *data, size_t len, size_t &out_len);

private:
    const int period;
    PacketHeader head;
    asio::streambuf buf;
    std::ostream os;
    OpusEncoder *encoder;
    unsigned char *enc_buf;
};

class NetDecoder
{
public:
    NetDecoder(uint8_t _token, uint8_t _channel, int _bandwidth);
    ~NetDecoder();

    bool commit(const char *data, size_t len, const char *&out_data, size_t &out_len);

    ChannelInfo statistic_info();

private:
    const uint8_t token;
    const uint8_t chann;

    OpusDecoder *decoder;
    opus_int16 *dec_buf;
    opus_int16 *rsc_buf;
    int fsi;
    int fso;

    uint32_t iseq_last;
    uint32_t pack_lost;
    uint64_t rnow_last;
    uint64_t snow_last;
    double jitter;
    double recv_interv;
    double send_interv;

    std::mutex mtx;
    double lost_rate;
    double avg_jitter;
    double avg_recv_interv;
    double avg_send_interv;
};

#endif