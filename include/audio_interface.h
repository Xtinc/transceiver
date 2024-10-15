#ifndef AUDIO_INTERFACE_HEADER
#define AUDIO_INTERFACE_HEADER

#include <cinttypes>
#include <cstdio>
#include <memory>
#include <string>

using AudioInputCallBack = void (*)(const int16_t *input_data, unsigned int chan_num, unsigned int frame_num,
                                    void *user_data);

enum class AudioBandWidth : int
{
  Unknown = 0,
  Narrow = 8000,
  Wide = 16000,
  SemiSuperWide = 24000,
  Full = 48000
};

enum class AudioPeriodSize : int
{
  INR_05MS = 0x05,
  INR_10MS = 0x0a,
  INR_20MS = 0x14,
  INR_40MS = 0x28
};

class OAStreamImpl;
class IAStreamImpl;
class AudioPlayerImpl;

void start_audio_service();

void stop_audio_service();

class OAStream
{
  friend class IAStream;
  friend class AudioPlayer;

public:
  OAStream(unsigned char _token, const std::string &_hw_name = "default_output",
           AudioBandWidth _bandwidth = AudioBandWidth::Unknown, AudioPeriodSize _period = AudioPeriodSize::INR_10MS,
           bool _enable_network = false);
  ~OAStream();

  bool start();

  void stop();

  void connect(const OAStream &other);

  void direct_push_pcm(uint8_t input_token, uint8_t input_chan, int input_period, int sample_rate,
                       const int16_t *data);

private:
  std::shared_ptr<OAStreamImpl> impl;
};

class IAStream
{
public:
  IAStream(unsigned char _token, const std::string &_hw_name = "default_input",
           AudioBandWidth _bandwidth = AudioBandWidth::Full, AudioPeriodSize _period = AudioPeriodSize::INR_10MS,
           bool _enable_network = false, bool _enable_auto_reset = false);
  ~IAStream();

  bool start();

  void mute();

  void unmute();

  void stop();

  void connect(const OAStream &sink);

  bool connect(const std::string &ip, unsigned char token);

  void set_callback(AudioInputCallBack _cb, void *_user_data);

private:
  std::shared_ptr<IAStreamImpl> impl;
};

class AudioPlayer
{
public:
  AudioPlayer(unsigned char _token);
  ~AudioPlayer();

  bool play(const std::string &name, const OAStream &sink);

  bool play(const std::string &name, const std::string &ip, unsigned char token);

  void stop(const std::string &name);

private:
  std::unique_ptr<AudioPlayerImpl> impl;
};

#endif