
#include "tool.h"
#include "ftxui/dom/node.hpp"      // for Render
#include "ftxui/screen/color.hpp"  // for Color, Color::BlueLight, Color::RedLight, Color::YellowLight, ftxui
#include "ftxui/dom/elements.hpp"  // for graph, operator|, separator, color, Element, vbox, flex, inverted, operator|=, Fit, hbox, size, border, GREATER_THAN, HEIGHT
#include "ftxui/screen/screen.hpp" // for Full, Screen
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include <iostream>
#include <thread>
#include <cmath>

using udp = asio::ip::udp;

static constexpr uint8_t DEBUG_TOOL_TOKEN = UINT8_MAX;
static constexpr unsigned short DEBUG_TOOL_PORT = (uint16_t)(0xccu << 8) + (uint16_t)DEBUG_TOOL_TOKEN;
static constexpr auto DEBUG_TOOL_SAMPLE_RATE = AudioBandWidth::Full;
static constexpr auto DEBUG_TOOL_FRESH_INTERV = AudioPeriodSize::INR_40MS;
static constexpr auto MAXIMUM_TRANSMISSION_SIZE = 1024 * 6;

static constexpr int GRAPH_WINDOWS_SIZE[2] = {21, 60};
static std::mutex fresh_mtx;

WaveGraph::WaveGraph(AudioPeriodSize max_len) : length(enum2val(DEBUG_TOOL_SAMPLE_RATE) * enum2val(max_len) / 1000)
{
    data = new int16_t[length];
    std::memset(data, 0, length * sizeof(int16_t));
}

WaveGraph::~WaveGraph()
{
    delete[] data;
}

std::vector<int> WaveGraph::operator()(int width, int height)
{
    std::vector<int> result;
    result.reserve(width);
    for (size_t i = 0; i < width; i++)
    {
        auto data_idx = i * length / width;
        auto amp = (int)((double)(data[data_idx] + 16384) / 32768 * height);
        result.emplace_back(amp);
    }
    return result;
}

void WaveGraph::set_data(const int16_t *ssrc, int ssrc_chan, int frames_num, int chan_idx)
{
    if (frames_num != length)
    {
        return;
    }

    if (ssrc_chan == 1)
    {
        std::memcpy(data, ssrc, sizeof(int16_t) * length);
    }
    else if (ssrc_chan == 2)
    {
        for (int i = 0; i < frames_num; i++)
        {
            data[i] = ssrc[2 * i + chan_idx];
        }
    }
}

EnergyGraph::EnergyGraph(AudioPeriodSize max_len) : length(enum2val(DEBUG_TOOL_SAMPLE_RATE) * enum2val(max_len) / 1000)
{
}

std::vector<int> EnergyGraph::operator()(int width, int height)
{
    std::vector<int> result;
    result.reserve(width);
    while (data.size() < width)
    {
        data.push_back(0);
    }

    while (data.size() > width)
    {
        data.pop_front();
    }

    for (size_t i = 0; i < width; i++)
    {
        auto amp = data[i] > 0 ? data[i] : 32767e-2;
        auto db = 20 * std::log10(amp / 32767) + 40;
        auto val = (int)(db * height / 40);
        result.emplace_back(val);
    }
    xrange = width;
    return result;
}

void EnergyGraph::set_data(const int16_t *ssrc, int ssrc_chan, int frames_num, int chan_idx)
{
    if (frames_num != length)
    {
        return;
    }
    double sum = 0;
    if (ssrc_chan == 1)
    {
        for (int i = 0; i < frames_num; i++)
        {
            sum += std::abs(ssrc[i]);
        }
    }
    else if (ssrc_chan == 2)
    {
        for (int i = 0; i < frames_num; i++)
        {
            sum += std::abs(ssrc[2 * i + chan_idx]);
        }
    }
    sum /= frames_num;
    data.push_back(sum);
}

int EnergyGraph::xaxis_range() const
{
    return xrange;
}

FreqGraph::FreqGraph(AudioPeriodSize max_len) : length(enum2val(DEBUG_TOOL_SAMPLE_RATE) * enum2val(max_len) / 1000), freq_len(length / 2 + 1)
{
    fft_cfg = kiss_fft_alloc(length, 0, nullptr, nullptr);
    cxi = new kiss_fft_cpx[length];
    cxo = new kiss_fft_cpx[length];
}

FreqGraph::~FreqGraph()
{
    delete[] cxo;
    delete[] cxi;
    kiss_fft_free(fft_cfg);
}

std::vector<int> FreqGraph::operator()(int width, int height)
{
    std::vector<int> result;
    result.reserve(width);
    cxo[0].r = 0;
    cxo[0].i = 0;
    cxo[freq_len].r = 0;
    cxo[freq_len].i = 0;

    auto last_idx = 0;
    for (size_t i = 1; i < width + 1; i++)
    {
        auto data_idx = i * freq_len / width;
        auto sum = 0.0f;
        for (size_t j = last_idx; j < data_idx; j++)
        {
            sum += std::sqrt(cxo[j].r * cxo[j].r + cxo[j].i * cxo[j].i);
        }
        sum *= 2.0f / ((data_idx - last_idx) * length);

        auto amp = sum > 0.0f ? sum : 32767e-4;
        auto db = 20.0f * std::log10(sum / 32767) + 80.0f;
        auto val = (int)(db * height / 80.0f);
        result.emplace_back(val);
        last_idx = data_idx;
    }
    return result;
}

void FreqGraph::set_data(const int16_t *ssrc, int ssrc_chan, int frames_num, int chan_idx)
{
    if (frames_num != length)
    {
        return;
    }

    if (ssrc_chan == 1)
    {
        for (int i = 0; i < frames_num; i++)
        {
            cxi[i].r = ssrc[i] * HanningWindows(frames_num - i - 1, frames_num);
            cxi[i].i = 0;
        }
    }
    else if (ssrc_chan == 2)
    {
        for (int i = 0; i < frames_num; i++)
        {
            cxi[i].r = ssrc[2 * i + chan_idx] * HanningWindows(frames_num - i - 1, frames_num);
            cxi[i].i = 0;
        }
    }
    kiss_fft(fft_cfg, cxi, cxo);
}

CespGraph::CespGraph(AudioPeriodSize max_len) : length(enum2val(DEBUG_TOOL_SAMPLE_RATE) * enum2val(max_len) / 1000), freq_len(length / 2 + 1)
{
    fft_cfg1 = kiss_fft_alloc(length, 0, nullptr, nullptr);
    fft_cfg2 = kiss_fft_alloc(length, 1, nullptr, nullptr);
    cxi = new kiss_fft_cpx[length];
    cxo = new kiss_fft_cpx[length];
}

CespGraph::~CespGraph()
{
    delete[] cxo;
    delete[] cxi;
    kiss_fft_free(fft_cfg1);
    kiss_fft_free(fft_cfg2);
}

std::vector<int> CespGraph::operator()(int width, int height)
{
    std::vector<int> result;
    result.reserve(width);

    for (int i = 0; i < length; i++)
    {
        cxi[i].r = std::log10(cxo[i].r * cxo[i].r + cxo[i].i * cxo[i].i) / length;
        cxi[i].i = 0.0;
    }

    kiss_fft(fft_cfg2, cxi, cxo);
    auto min_val = 1e12f;
    for (int i = 0; i < length; i++)
    {
        if (cxo[i].r < min_val)
        {
            min_val = cxo[i].r;
        }
    }
    auto last_idx = 0;
    for (int i = 1; i < width + 1; i++)
    {
        auto data_idx = i * freq_len / width;
        auto sum = 0.0f;
        for (int j = last_idx; j < data_idx; j++)
        {
            sum += cxo[j].r;
        }
        sum /= (data_idx - last_idx);
        result.emplace_back((int)(std::log10(sum - min_val) * 65536));
        last_idx = data_idx;
    }
    auto minmax = std::minmax_element(result.cbegin(), result.cend());
    auto min_height = *(minmax.first);
    auto max_height = *(minmax.second);
    auto ratio = max_height == min_height ? 65536 : max_height - min_height;

    for (auto &e : result)
    {
        e = (e - min_height) * height / ratio;
    }
    return result;
}

void CespGraph::set_data(const int16_t *ssrc, int ssrc_chan, int frames_num, int chan_idx)
{
    if (frames_num != length)
    {
        return;
    }

    if (ssrc_chan == 1)
    {
        for (int i = 0; i < frames_num; i++)
        {
            cxi[i].r = ssrc[i] * HanningWindows(frames_num - i - 1, frames_num);
            cxi[i].i = 0;
        }
    }
    else if (ssrc_chan == 2)
    {
        for (int i = 0; i < frames_num; i++)
        {
            cxi[i].r = ssrc[2 * i + chan_idx] * HanningWindows(frames_num - i - 1, frames_num);
            cxi[i].i = 0;
        }
    }
    kiss_fft(fft_cfg1, cxi, cxo);
}

Observer::Observer(asio::io_context &io, UiElement *element, AudioPeriodSize interval_ms)
    : fresh_interv(enum2val(interval_ms)), fs(enum2val(DEBUG_TOOL_SAMPLE_RATE)), ps(fs * fresh_interv / 1000),
      ui_element(element), ioc(io), timer(ioc), recv_buf(nullptr), oas_ready(false)
{
    recv_buf = new char[MAXIMUM_TRANSMISSION_SIZE];
}

Observer::~Observer()
{
    stop();
    delete[] recv_buf;
}

bool Observer::start()
{
    if (oas_ready)
    {
        return true;
    }

    oas_ready = true;

    try
    {
        sock = std::make_unique<udp::socket>(ioc, udp::endpoint(udp::v4(), DEBUG_TOOL_PORT));
    }
    catch (const std::exception &e)
    {
        AUDIO_ERROR_PRINT("%s\n", e.what());
        return false;
    }

    asio::post(ioc, [this]()
               { do_receive();
                fresh_graph(); });

    return true;
}

void Observer::stop()
{
    if (!oas_ready)
    {
        return;
    }
    oas_ready = false;
}

void Observer::set_callback(fresh_cb &&callback)
{
    cb = callback;
}

void Observer::do_receive()
{
    if (!oas_ready)
    {
        return;
    }
    static udp::endpoint sender_endpoint;
    sock->async_receive_from(
        asio::buffer(recv_buf, MAXIMUM_TRANSMISSION_SIZE * 6), sender_endpoint,
        [this](std::error_code ec, std::size_t bytes)
        {
            if (!ec && PacketHeader::validate(recv_buf, bytes))
            {
                auto sender = recv_buf[0];
                auto chan = recv_buf[1];
                std::lock_guard<std::mutex> grd(dest_mtx);
                if (net_sessions.find(sender) == net_sessions.end())
                {
                    decoders.insert({sender, std::make_unique<NetDecoder>(sender, chan, fs)});
                    net_sessions.insert(
                        {sender, std::make_unique<SessionData>(ps * chan * sizeof(int16_t), 18, chan)});
                }
                const char *decode_data = nullptr;
                size_t decode_length = 0;
                if (decoders.at(sender)->commit(recv_buf, bytes, decode_data, decode_length))
                {
                    net_sessions.at(sender)->store_data(decode_data, decode_length);
                }
            }
            do_receive();
        });
}

void Observer::fresh_graph()
{
    auto now = std::chrono::steady_clock::now();
    if (!oas_ready)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> grd(dest_mtx);
        auto s = net_sessions.begin();
        if (s != net_sessions.end())
        {
            auto ichan = s->second->chan;
            auto idata = (const int16_t *)s->second->out_buf;
            auto isize = ps * ichan * sizeof(int16_t);
            s->second->load_data(isize);
            std::lock_guard<std::mutex> grd2(fresh_mtx);
            if (ui_element->recorded)
            {
                if (!ofs.is_open())
                {
                    ofs.open("48000hz_" + std::to_string(ichan) + "ch.pcm", std::ios_base::binary);
                }
                ofs.write(reinterpret_cast<const char *>(idata), isize);
            }

            switch (ui_element->selected)
            {
            case 0:
                ui_element->wave_left.set_data(idata, ichan, ps, 0);
                ui_element->wave_right.set_data(idata, ichan, ps, 1);
                break;
            case 1:
                ui_element->energy_left.set_data(idata, ichan, ps, 0);
                ui_element->energy_right.set_data(idata, ichan, ps, 1);
                break;
            case 2:
                ui_element->freq_left.set_data(idata, ichan, ps, 0);
                ui_element->freq_right.set_data(idata, ichan, ps, 1);
                break;
            case 3:
                ui_element->cesp_left.set_data(idata, ichan, ps, 0);
                ui_element->cesp_right.set_data(idata, ichan, ps, 1);
                break;

            default:
                break;
            }
            ui_element->info = decoders.at(s->first)->statistic_info();
        }
    }

    if (cb)
    {
        cb();
    }

    timer.expires_at(now + asio::chrono::milliseconds(fresh_interv));
    timer.async_wait([this](const asio::error_code &ec)
                     {
        if (ec)
        {
            return;
        }
        fresh_graph(); });
}

static int fps_count()
{
    static int fps0 = 0;
    static int fps1 = 0;
    static auto start = std::chrono::steady_clock::now();
    ++fps0;
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    if (duration > 1000)
    {
        start = now;
        fps1 = fps0 * duration / 1000;
        fps0 = 0;
    }
    return fps1;
}

static ftxui::Element construct_graph(int height, int width, WaveGraph &usr_graph0, WaveGraph &usr_graph1)
{
    using namespace ftxui;
    auto lwin =
        hbox({vbox({text("+1.0 "),
                    filler(),
                    text("+0.5 "),
                    filler(),
                    text("±0.0 "),
                    filler(),
                    text("-0.5 "),
                    filler(),
                    text("-1.0 ")}),
              graph(std::ref(usr_graph0)) | flex | color(Color::BlueLight)});
    auto rwin =
        hbox({vbox({text("+1.0 "),
                    filler(),
                    text("+0.5 "),
                    filler(),
                    text("±0.0 "),
                    filler(),
                    text("-0.5 "),
                    filler(),
                    text("-1.0 ")}),
              graph(std::ref(usr_graph1)) | flex | color(Color::BlueLight)});
    lwin |= size(HEIGHT, GREATER_THAN, height);
    lwin |= size(WIDTH, GREATER_THAN, width);
    rwin |= size(HEIGHT, GREATER_THAN, height);
    rwin |= size(WIDTH, GREATER_THAN, width);
    return vbox({lwin, separator(), rwin});
}

static ftxui::Element construct_graph(int height, int width, EnergyGraph &usr_graph0, EnergyGraph &usr_graph1)
{
    using namespace ftxui;
    auto lwin =
        hbox({vbox({text("0dbFS"),
                    filler(),
                    text("-10  "),
                    filler(),
                    text("-20  "),
                    filler(),
                    text("-30  "),
                    filler(),
                    text("-40 ")}),
              graph(std::ref(usr_graph0)) | flex | color(Color::BlueLight)});
    auto rwin =
        hbox({vbox({text("0dbFS"),
                    filler(),
                    text("-10  "),
                    filler(),
                    text("-20  "),
                    filler(),
                    text("-30  "),
                    filler(),
                    text("-40 ")}),
              graph(std::ref(usr_graph1)) | flex | color(Color::BlueLight)});
    auto xaxis_max = usr_graph0.xaxis_range() * enum2val(DEBUG_TOOL_FRESH_INTERV);
    auto xtitle = hbox({text("ms   "),
                        text(std::to_string(xaxis_max)),
                        filler(),
                        text(std::to_string(7 * xaxis_max / 8)),
                        filler(),
                        text(std::to_string(3 * xaxis_max / 4)),
                        filler(),
                        text(std::to_string(5 * xaxis_max / 8)),
                        filler(),
                        text(std::to_string(xaxis_max / 2)),
                        filler(),
                        text(std::to_string(3 * xaxis_max / 8)),
                        filler(),
                        text(std::to_string(xaxis_max / 4)),
                        filler(),
                        text(std::to_string(xaxis_max / 8)),
                        filler(),
                        text("0")});
    lwin |= size(HEIGHT, GREATER_THAN, height);
    lwin |= size(WIDTH, GREATER_THAN, width);
    rwin |= size(HEIGHT, GREATER_THAN, height);
    rwin |= size(WIDTH, GREATER_THAN, width);
    return vbox({lwin, xtitle, rwin});
}

static ftxui::Element construct_graph(int height, int width, FreqGraph &usr_graph0, FreqGraph &usr_graph1, int xaxis_max)
{
    using namespace ftxui;
    auto lwin =
        hbox({vbox({text("0dbFS"),
                    filler(),
                    text("-20  "),
                    filler(),
                    text("-40  "),
                    filler(),
                    text("-60  "),
                    filler(),
                    text("-80 ")}),
              graph(std::ref(usr_graph0)) | flex | color(Color::BlueLight)});
    auto rwin =
        hbox({vbox({text("0dbFS"),
                    filler(),
                    text("-20  "),
                    filler(),
                    text("-40  "),
                    filler(),
                    text("-60  "),
                    filler(),
                    text("-80 ")}),
              graph(std::ref(usr_graph1)) | flex | color(Color::BlueLight)});

    auto xtitle = hbox({text("kHz  "),
                        text("0"),
                        filler(),
                        text(std::to_string(xaxis_max / 8)),
                        filler(),
                        text(std::to_string(xaxis_max / 4)),
                        filler(),
                        text(std::to_string(3 * xaxis_max / 8)),
                        filler(),
                        text(std::to_string(xaxis_max / 2)),
                        filler(),
                        text(std::to_string(5 * xaxis_max / 8)),
                        filler(),
                        text(std::to_string(3 * xaxis_max / 4)),
                        filler(),
                        text(std::to_string(7 * xaxis_max / 8)),
                        filler(),
                        text(std::to_string(xaxis_max))});
    lwin |= size(HEIGHT, GREATER_THAN, height);
    lwin |= size(WIDTH, GREATER_THAN, width);
    rwin |= size(HEIGHT, GREATER_THAN, height);
    rwin |= size(WIDTH, GREATER_THAN, width);
    return vbox({lwin, xtitle, rwin});
}

static ftxui::Element construct_graph(int height, int width, CespGraph &usr_graph0, CespGraph &usr_graph1, int xaxis_max)
{
    using namespace ftxui;
    auto lwin =
        hbox({vbox({text("1.00 "),
                    filler(),
                    text("0.75 "),
                    filler(),
                    text("0.50 "),
                    filler(),
                    text("0.25 "),
                    filler(),
                    text("0.00 ")}),
              graph(std::ref(usr_graph0)) | flex | color(Color::BlueLight)});
    auto rwin =
        hbox({vbox({text("1.00 "),
                    filler(),
                    text("0.75 "),
                    filler(),
                    text("0.50 "),
                    filler(),
                    text("0.25 "),
                    filler(),
                    text("0.00 ")}),
              graph(std::ref(usr_graph1)) | flex | color(Color::BlueLight)});
    auto xtitle = hbox({text("ms   "),
                        text("0"),
                        filler(),
                        text(std::to_string(xaxis_max / 8)),
                        filler(),
                        text(std::to_string(xaxis_max / 4)),
                        filler(),
                        text(std::to_string(3 * xaxis_max / 8)),
                        filler(),
                        text(std::to_string(xaxis_max / 2)),
                        filler(),
                        text(std::to_string(5 * xaxis_max / 8)),
                        filler(),
                        text(std::to_string(3 * xaxis_max / 4)),
                        filler(),
                        text(std::to_string(7 * xaxis_max / 8)),
                        filler(),
                        text(std::to_string(xaxis_max))});
    lwin |= size(HEIGHT, GREATER_THAN, height);
    lwin |= size(WIDTH, GREATER_THAN, width);
    rwin |= size(HEIGHT, GREATER_THAN, height);
    rwin |= size(WIDTH, GREATER_THAN, width);
    return vbox({lwin, xtitle, rwin});
}

static ftxui::Element construct_infos(const ChannelInfo &sta_info, const ftxui::Element &holder)
{
    using namespace ftxui;
    Elements content;
    char tmp[20]{};
    sprintf(tmp, "RX   :%7.1fus", sta_info.recv_interv);
    content.push_back(text(tmp) | hcenter);
    sprintf(tmp, "TX   :%7.1fus", sta_info.send_interv);
    content.push_back(text(tmp) | hcenter);
    sprintf(tmp, "Jc2c :%7.1fus", sta_info.jitter);
    content.push_back(text(tmp) | hcenter);
    sprintf(tmp, "Lost :%7.3f%% ", sta_info.lost_rate);
    content.push_back(text(tmp) | hcenter);

    return vbox({window(text("FPS") | hcenter | bold, text(std::to_string(fps_count())) | hcenter),
                 window(text("Statistics No." + std::to_string((unsigned int)sta_info.token)) | hcenter | bold, vbox(std::move(content))),
                 window(text("Control") | hcenter | bold, holder)});
}

int main(int argc, char **argv)
{
    asio::io_context ioc;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard(ioc.get_executor());
    std::thread thd([&ioc]()
                    { ioc.run(); });
    {
        UiElement ui_element{0, DEBUG_TOOL_FRESH_INTERV};
        Observer ob(ioc, &ui_element, DEBUG_TOOL_FRESH_INTERV);
        if (!ob.start())
        {
            return EXIT_SUCCESS;
        }

        auto screen = ftxui::ScreenInteractive::TerminalOutput();
        screen.SetCursor(ftxui::Screen::Cursor{0});

        std::vector<std::string> tab_values{"WAVE", "ENERGY", "STFT", "CESP"};
        auto tab_toggle = ftxui::Toggle(&tab_values, &ui_element.selected);
        auto tab1 = ftxui::Renderer(
            [&ui_element]()
            {
                return ui_element.selected == 0 ? construct_graph(GRAPH_WINDOWS_SIZE[0], GRAPH_WINDOWS_SIZE[1], ui_element.wave_left, ui_element.wave_right) : ftxui::text("");
            });
        auto tab2 = ftxui::Renderer(
            [&ui_element]()
            {
                return ui_element.selected == 1 ? construct_graph(GRAPH_WINDOWS_SIZE[0], GRAPH_WINDOWS_SIZE[1], ui_element.energy_left, ui_element.energy_right) : ftxui::text("");
            });
        auto tab3 = ftxui::Renderer(
            [&ui_element]()
            {
                return ui_element.selected == 2 ? construct_graph(GRAPH_WINDOWS_SIZE[0], GRAPH_WINDOWS_SIZE[1], ui_element.freq_left, ui_element.freq_right, enum2val(DEBUG_TOOL_SAMPLE_RATE) / 2000) : ftxui::text("");
            });
        auto tab4 = ftxui::Renderer(
            [&ui_element]()
            {
                return ui_element.selected == 3 ? construct_graph(GRAPH_WINDOWS_SIZE[0], GRAPH_WINDOWS_SIZE[1], ui_element.cesp_left, ui_element.cesp_right, enum2val(DEBUG_TOOL_FRESH_INTERV)) : ftxui::text("");
            });

        auto tab_container = ftxui::Container::Tab(
            {tab1, tab2, tab3, tab4},
            &ui_element.selected);
        auto container0 = ftxui::Container::Vertical({
            tab_toggle,
            tab_container,
        });

        auto chk_box = ftxui::Checkbox("Record", &ui_element.recorded);
        auto container1 = ftxui::Container::Horizontal({container0, chk_box});

        auto renderer = ftxui::Renderer(container1, [&tab_toggle, &tab_container, &chk_box, &ui_element]
                                        {
            std::lock_guard<std::mutex> grd(fresh_mtx);
            auto tbc = ftxui::vbox({
                tab_toggle->Render(),
                ftxui::separator(),
                tab_container->Render(),
            });
            auto text_box = construct_infos(ui_element.info, chk_box->Render());
            auto document = ftxui::window(ftxui::text("Audio Debug Tool " + std::string(__DATE__)) | ftxui::hcenter | ftxui::bold, ftxui::hbox({std::move(tbc) | ftxui::flex, ftxui::separator(), text_box}));
            document |= ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 60);
            return document; });

        ob.set_callback([&screen]()
                        { screen.Post(ftxui::Event::Custom); });
        screen.Loop(renderer);
    }
    thd.join();
    return EXIT_SUCCESS;
}