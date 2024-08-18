#ifndef AUDIO_STREAM_HEADER
#define AUDIO_STREAM_HEADER

#include "asio.hpp"
#include "audioprotocol.h"
#include <map>
#include <mutex>

class Session;
class IAStream;

class OAStream
{
    using asio_udp = asio::ip::udp;
    using session_ptr = std::unique_ptr<Session>;

public:
    OAStream(unsigned char _token, short _port, AudioBandWidth _bandwidth, AudioPeriodSize _period);
    ~OAStream();

    void start();

    void stop();

    void write_pcm_frames(void *output);

private:
    void recv_audio_packet(const char *data, size_t bytes);

private:
    const unsigned char token;
    const int fs;
    const int ps;
    int chan_num;

    void *os;
    std::mutex recv_mtx;
    std::map<char, session_ptr> sessions;
};

class IAStream
{
public:
    IAStream(unsigned char _token, AudioBandWidth _bandwidth, AudioPeriodSize _period);
    ~IAStream();

    void start();

    void stop();

    void read_pcm_frames(const void *input);

private:
    const unsigned char token;
    const int fs;
    const int ps;
    int chan_num;

    void *is;
};

#endif
