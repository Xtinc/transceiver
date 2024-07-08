#ifndef AUDIO_TRANSCERIVER_HEADER
#define AUDIO_TRANSCERIVER_HEADER

#include <mutex>
#include <condition_variable>

void start_sound_control();
void close_sound_control();

class Receiver
{
public:
    Receiver(int _channels, int _sample_rate, int _period);
    ~Receiver();

public:
    void listen();

    void recv_pcm_frames(const float *input);

private:
    void *iss;
    const int channels;
    const int period;
    std::unique_ptr<float> frames;
    bool ready;
    std::mutex mtx;
    std::condition_variable cond;
};

#endif