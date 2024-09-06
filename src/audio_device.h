#ifndef AUDIO_DEVICE_HEADER
#define AUDIO_DEVICE_HEADER
#include "asio.hpp"
#include "audio_wavfile.h"

class IAStreamImpl;
class OAStreamImpl;
class LocEncoder;
using PaStream = void;

// General Device
class AudioDevice
{
  public:
    virtual ~AudioDevice() = default;

    virtual bool create(const std::string &name, void *cls, int &fs, int &ps, int &chan, int &max_chan) = 0;

    virtual bool start() = 0;

    virtual bool stop() = 0;

    virtual void transfer_pcm_data(int16_t *data, int frame_number)
    {
    }

    virtual bool async_task(int interval)
    {
        return false;
    }

    virtual bool enable_external_loop() const
    {
        return false;
    }

  protected:
    bool ready{false};
};

// Phsy Input Device
class PhsyIADevice final : public AudioDevice
{
  public:
    PhsyIADevice() : device(nullptr), stream(nullptr) {};
    ~PhsyIADevice() override;

    bool create(const std::string &name, void *cls, int &fs, int &ps, int &chan, int &max_chan) override;

    bool start() override;

    bool stop() override;

    void transfer_pcm_data(int16_t *input, int frame_number) override;

  private:
    PaStream *device;
    IAStreamImpl *stream;
};

// Phys Output Device
class PhsyOADevice final : public AudioDevice
{
  public:
    PhsyOADevice() : device(nullptr), stream(nullptr) {};
    ~PhsyOADevice() override;

    bool create(const std::string &name, void *cls, int &fs, int &ps, int &chan, int &max_chan) override;

    bool start() override;

    bool stop() override;

    void transfer_pcm_data(int16_t *input, int frame_number) override;

  private:
    PaStream *device;
    OAStreamImpl *stream;
};

// Wave Output Device
class WaveOADevice final : public AudioDevice
{
  public:
    WaveOADevice(asio::io_context &_io) : io_ctx(_io), timer(io_ctx), oastream(nullptr), pick_ups(nullptr)
    {
    }
    ~WaveOADevice() override;

    bool create(const std::string &name, void *cls, int &fs, int &ps, int &chan, int &max_chan) override;

    bool start() override;

    bool stop() override;

  private:
    void async_task(int ps, int interv);

  private:
    asio::io_context &io_ctx;
    asio::steady_timer timer;
    std::ofstream ofs;
    OAStreamImpl *oastream;
    int16_t *pick_ups;
};

// Wave Input Device
class WaveIADevice final : public AudioDevice
{
  public:
    WaveIADevice(asio::io_context &_io) : io_ctx(_io), timer(io_ctx), iastream(nullptr), pick_ups(nullptr)
    {
    }
    ~WaveIADevice() override;

    bool create(const std::string &name, void *cls, int &fs, int &ps, int &chan, int &max_chan) override;

    bool start() override;

    bool stop() override;

    bool async_task(int interv) override;

    bool enable_external_loop() const override;

  private:
    void async_task(int ps, int interv);

  private:
    asio::io_context &io_ctx;
    asio::steady_timer timer;
    WavFile ifs;
    IAStreamImpl *iastream;
    int16_t *pick_ups;
};

// Phys Multi Input Device
class MultiIADevice final : public AudioDevice
{
  public:
    MultiIADevice(int left_input_channel, int right_input_channel, int max_input_channel)
        : input_l(left_input_channel), input_r(right_input_channel), max_pick(max_input_channel) {};
    ~MultiIADevice() override;

    bool create(const std::string &name, void *cls, int &fs, int &ps, int &chan, int &max_chan) override;

    bool start() override;

    bool stop() override;

    void transfer_pcm_data(int16_t *input, int frame_number) override;

  private:
    PaStream *device{nullptr};
    IAStreamImpl *stream{nullptr};
    const int input_l;
    const int input_r;
    const int max_pick;
    int16_t *pick_ups;
};

// Phys Multi Output Device
class MultiOADevice final : public AudioDevice
{
  public:
    MultiOADevice(int left_input_channel, int right_input_channel, int max_input_channel)
        : input_l(left_input_channel), input_r(right_input_channel), max_pick(max_input_channel) {};
    ~MultiOADevice() override;

    bool create(const std::string &name, void *cls, int &fs, int &ps, int &chan, int &max_chan) override;

    bool start() override;

    bool stop() override;

    void transfer_pcm_data(int16_t *input, int frame_number) override;

  private:
    PaStream *device{nullptr};
    OAStreamImpl *stream{nullptr};
    const int input_l;
    const int input_r;
    const int max_pick;
    int16_t *pick_ups;
};

#endif