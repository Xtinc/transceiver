#ifndef AUDIO_STREAM_HEADER
#define AUDIO_STREAM_HEADER

#include "asio.hpp"
#include "audio_interface.h"
#include <atomic>
#include <functional>
#include <map>
#include <mutex>

class SessionData;
class LocEncoder;
class NetEncoder;
class NetDecoder;
class AudioDevice;

using usocket_ptr = std::unique_ptr<asio::ip::udp::socket>;
using odevice_ptr = std::unique_ptr<AudioDevice>;
using idevice_ptr = std::unique_ptr<AudioDevice>;
using decoder_ptr = std::unique_ptr<NetDecoder>;
using encoder_ptr = std::unique_ptr<NetEncoder>;
using sampler_ptr = std::unique_ptr<LocEncoder>;
using session_ptr = std::unique_ptr<SessionData>;
using sampler_ptr = std::unique_ptr<LocEncoder>;
using net_endpoints = std::vector<asio::ip::udp::endpoint>;
using loc_endpoints = std::vector<std::weak_ptr<OAStreamImpl>>;

class AudioService
{
public:
  static AudioService &GetService();

  void start();

  void stop();

  asio::io_context &executor();

private:
  AudioService();
  ~AudioService() = default;

private:
  asio::io_context io_ctx;
  std::vector<std::thread> io_thds;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard;
};

class OAStreamImpl : public std::enable_shared_from_this<OAStreamImpl>
{
  friend class PhsyOADevice;
  friend class WaveOADevice;
  friend class MultiOADevice;
  friend class PipeIADevice;

public:
  OAStreamImpl(unsigned char _token, AudioBandWidth _bandwidth, AudioPeriodSize _period, const std::string &_hw_name,
               bool _enable_network);
  ~OAStreamImpl();

  bool start();

  void mute(unsigned char _token);

  void unmute(unsigned char _token);

  void stop();

  void direct_push_pcm(uint8_t input_token, uint8_t input_chan, int input_period, int sample_rate,
                       const int16_t *data);

  void set_callback(std::function<void(const int16_t *, int)> &&fn);

private:
  void do_receive();

  void write_pcm_frames(int16_t *output, int frame_number);

  void exec_external_loop();

private:
  const unsigned char token;
  bool enable_network;
  int fs;
  int ps;
  int chan_num;
  int max_chan;

  odevice_ptr odevice;
  std::mutex recv_mtx;
  std::map<uint8_t, decoder_ptr> decoders;
  std::map<uint8_t, sampler_ptr> samplers;
  std::map<uint8_t, session_ptr> net_sessions;
  std::map<uint8_t, session_ptr> loc_sessions;
  asio::steady_timer timer;
  usocket_ptr sock;
  char *recv_buf;
  std::mutex delv_mtx;
  std::function<void(const int16_t *, int)> delv_cb;
  std::atomic_bool oas_ready;
};

class IAStreamImpl : public std::enable_shared_from_this<IAStreamImpl>
{
  friend class PhsyIADevice;
  friend class WaveIADevice;
  friend class MultiIADevice;
  friend class PipeIADevice;

public:
  IAStreamImpl(unsigned char _token, AudioBandWidth _bandwidth, AudioPeriodSize _period, const std::string &_hw_name,
               bool _enable_network, bool _enable_reset);
  IAStreamImpl(unsigned char _token, const std::shared_ptr<OAStreamImpl> &oas, bool _enable_network, bool _enable_reset);
  ~IAStreamImpl();

  bool start();

  void mute();

  void unmute();

  void stop();

  void connect(const std::shared_ptr<OAStreamImpl> &sink);

  bool connect(const std::string &ip, uint16_t port);

  void set_callback(AudioInputCallBack _cb, int _ps, void *_user_data);

  void set_destory_callback(std::function<void()> &&_cb);

private:
  void reset_phsy_device();

  void set_resampler_parameter(int fsi, int fso, int chan);

  void read_raw_frames(const int16_t *input, int frame_number);

  void read_pcm_frames(const int16_t *input, int frame_number);

  void copy_pcm_frames();

  void exec_external_loop();

private:
  const unsigned char token;
  bool enable_network;
  const std::string hw_name;
  int fs;
  int ps;
  int chan_num;
  int max_chan;
  std::atomic_bool muted;

  idevice_ptr idevice;
  session_ptr session;
  encoder_ptr encoder;
  sampler_ptr sampler;
  loc_endpoints loc_dests;
  net_endpoints net_dests;

  usocket_ptr sock;
  std::mutex dest_mtx;
  asio::steady_timer timer0;
  asio::steady_timer timer1;
  AudioInputCallBack usr_cb;
  void *usr_data;
  int usr_ps;
  std::function<void()> dtor_cb;
  std::atomic_bool ias_ready;
};

class AudioPlayerImpl
{
public:
  AudioPlayerImpl(unsigned char _token);
  ~AudioPlayerImpl() = default;

  bool play(const std::string &name, const std::shared_ptr<OAStreamImpl> &sink);

  bool play(const std::string &name, const std::string &ip, unsigned char token);

  void stop(const std::string &name);

private:
  template <typename... T>
  bool play_tmpl(const std::string &name, T... args)
  {
    if (preemptive > 5)
    {
      return false;
    }
    auto audio_sender = std::make_shared<IAStreamImpl>(token + preemptive, AudioBandWidth::Full,
                                                       AudioPeriodSize::INR_20MS, name, false, false);
    audio_sender->set_destory_callback([this, name]()
                                       {
            preemptive--;
            std::lock_guard<std::mutex> grd(mtx);
            auto iter = sounds.find(name);
            if (iter != sounds.cend())
            {
                sounds.erase(iter);
            } });
    audio_sender->connect(args...);
    {
      std::lock_guard<std::mutex> grd(mtx);
      sounds.insert({name, audio_sender});
    }
    if (!audio_sender->start())
    {
      return false;
    }
    preemptive++;
    return true;
  }

private:
  const unsigned char token;
  std::mutex mtx;
  std::atomic_int preemptive;
  std::map<std::string, std::weak_ptr<IAStreamImpl>> sounds;
};

#define SERVICE (AudioService::GetService().executor())
#endif
