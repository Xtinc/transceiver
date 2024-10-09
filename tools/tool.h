#ifndef AUDIO_DEBUG_TOOL_HEADER
#define AUDIO_DEBUG_TOOL_HEADER

#include "audio_stream.h"
#include "audio_network.h"
#include "kiss_fft.h"
#include <deque>
#include <fstream>

class WaveGraph
{
public:
    WaveGraph(AudioPeriodSize max_len);
    ~WaveGraph();

    std::vector<int> operator()(int width, int height);

    void set_data(const int16_t *ssrc, int ssrc_chan, int frames_num, int chan_idx);

private:
    int length;
    int16_t *data;
};

class EnergyGraph
{
public:
    EnergyGraph(AudioPeriodSize max_len);

    std::vector<int> operator()(int width, int height);

    void set_data(const int16_t *ssrc, int ssrc_chan, int frames_num, int chan_idx);

private:
    int length;
    std::deque<double> data;
};

class FreqGraph
{
public:
    FreqGraph(AudioPeriodSize max_len);
    ~FreqGraph();

    std::vector<int> operator()(int width, int height);

    void set_data(const int16_t *ssrc, int ssrc_chan, int frames_num, int chan_idx);

private:
    int length;
    int freq_len;
    kiss_fft_cfg fft_cfg;
    kiss_fft_cpx *cxi;
    kiss_fft_cpx *cxo;
};

class CespGraph
{
public:
    CespGraph(AudioPeriodSize max_len);
    ~CespGraph();

    std::vector<int> operator()(int width, int height);

    void set_data(const int16_t *ssrc, int ssrc_chan, int frames_num, int chan_idx);

private:
    int length;
    int freq_len;
    kiss_fft_cfg fft_cfg1;
    kiss_fft_cfg fft_cfg2;
    kiss_fft_cpx *cxi;
    kiss_fft_cpx *cxo;
};

struct UiElement
{
    UiElement(int tab_selected, AudioPeriodSize ps)
        : info{}, wave_left(ps), wave_right(ps), energy_left(ps), energy_right(ps), freq_left(ps), freq_right(ps), cesp_left(ps), cesp_right(ps), selected(tab_selected), recorded(false) {}
    ChannelInfo info;
    WaveGraph wave_left;
    WaveGraph wave_right;
    EnergyGraph energy_left;
    EnergyGraph energy_right;
    FreqGraph freq_left;
    FreqGraph freq_right;
    CespGraph cesp_left;
    CespGraph cesp_right;
    int selected;
    bool recorded;
};

class Observer : public std::enable_shared_from_this<Observer>
{
public:
    using fresh_cb = std::function<void()>;

    Observer(UiElement *element, AudioPeriodSize interval_ms);
    ~Observer();

    bool start();

    void stop();

    void set_callback(fresh_cb &&callback);

private:
    void do_receive();

    void fresh_graph();

private:
    const int fresh_interv;
    const int fs;
    const int ps;

    std::map<uint8_t, decoder_ptr> decoders;
    std::map<uint8_t, session_ptr> net_sessions;
    fresh_cb cb;

    UiElement *ui_element;
    std::mutex dest_mtx;
    asio::steady_timer timer;
    usocket_ptr sock;
    char *recv_buf;
    std::atomic_bool oas_ready;
    std::ofstream ofs;
};

#endif