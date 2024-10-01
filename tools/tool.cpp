
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

static constexpr int SMALL_WINDOWS_SIZE[2] = {21, 60};
static constexpr int MEDIUM_WINDOWS_SIZE[2] = {21, 120};

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
            data[i] = ssrc[i + chan_idx];
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
        auto amp = data[i] > 0 ? data[i] : 1e-4;
        auto db = 20 * std::log10(amp / 32767) + 40;
        auto val = (int)(db * height / 40);
        result.emplace_back(val);
    }
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
            sum += std::abs(ssrc[i + chan_idx]);
        }
    }
    sum /= frames_num;
    data.push_back(sum);
}

Observer::Observer(UiElement *element, AudioPeriodSize interval_ms)
    : fresh_interv(enum2val(interval_ms)), fs(enum2val(DEBUG_TOOL_SAMPLE_RATE)), ps(fs * fresh_interv / 1000),
      ui_element(element), timer(SERVICE), recv_buf(nullptr), oas_ready(false)
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
        sock = std::make_unique<udp::socket>(SERVICE, udp::endpoint(udp::v4(), DEBUG_TOOL_PORT));
    }
    catch (const std::exception &e)
    {
        AUDIO_ERROR_PRINT("%s\n", e.what());
        return false;
    }

    asio::post(SERVICE, [self = shared_from_this()]()
               { self->do_receive();
                self->fresh_graph(); });

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
        [self = shared_from_this()](std::error_code ec, std::size_t bytes)
        {
            if (!ec && PacketHeader::validate(self->recv_buf, bytes))
            {
                auto sender = self->recv_buf[0];
                auto chan = self->recv_buf[1];
                std::lock_guard<std::mutex> grd(self->dest_mtx);
                if (self->net_sessions.find(sender) == self->net_sessions.end())
                {
                    self->decoders.insert({sender, std::make_unique<NetDecoder>(sender, chan, self->fs)});
                    self->net_sessions.insert(
                        {sender, std::make_unique<SessionData>(self->ps * chan * sizeof(int16_t), 18, chan)});
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
            s->second->load_data(ps * ichan * sizeof(int16_t));
            std::lock_guard<std::mutex> grd2(fresh_mtx);
            ui_element->wave_left.set_data(idata, ichan, ps, 0);
            ui_element->wave_right.set_data(idata, ichan, ps, 1);
            ui_element->energy_left.set_data(idata, ichan, ps, 0);
            ui_element->energy_right.set_data(idata, ichan, ps, 1);
            ui_element->info = decoders.at(s->first)->statistic_info();
        }
    }

    if (cb)
    {
        cb();
    }

    timer.expires_at(now + asio::chrono::milliseconds(fresh_interv));
    timer.async_wait([self = shared_from_this()](const asio::error_code &ec)
                     {
        if (ec)
        {
            return;
        }
        self->fresh_graph(); });
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
        hbox({
            vbox({text("+1.0 "),
                  filler(),
                  text("+0.5 "),
                  filler(),
                  text("±0.0 "),
                  filler(),
                  text("-0.5 "),
                  filler(),
                  text("-1.0 ")}),
            graph(std::ref(usr_graph0)) | flex | color(Color::BlueLight),
        });
    auto rwin =
        hbox({
            vbox({text("+1.0 "),
                  filler(),
                  text("+0.5 "),
                  filler(),
                  text("±0.0 "),
                  filler(),
                  text("-0.5 "),
                  filler(),
                  text("-1.0 ")}),
            graph(std::ref(usr_graph1)) | flex | color(Color::BlueLight),
        });
    lwin |= size(HEIGHT, GREATER_THAN, height);
    lwin |= size(WIDTH, GREATER_THAN, width);
    rwin |= size(HEIGHT, GREATER_THAN, height);
    rwin |= size(WIDTH, GREATER_THAN, width);
    return vbox({text("Left Channel") | hcenter, lwin, separator(), text("Right Channel") | hcenter, rwin});
}

static ftxui::Element construct_graph(int height, int width, EnergyGraph &usr_graph0, EnergyGraph &usr_graph1)
{
    using namespace ftxui;
    auto lwin =
        hbox({
            vbox({text("0dbFS"),
                  filler(),
                  text("-10  "),
                  filler(),
                  text("-20  "),
                  filler(),
                  text("-30  "),
                  filler(),
                  text("-40 ")}),
            graph(std::ref(usr_graph0)) | flex | color(Color::BlueLight),
        });
    auto rwin =
        hbox({
            vbox({text("0dbFS"),
                  filler(),
                  text("-10  "),
                  filler(),
                  text("-20  "),
                  filler(),
                  text("-30  "),
                  filler(),
                  text("-40 ")}),
            graph(std::ref(usr_graph1)) | flex | color(Color::BlueLight),
        });
    lwin |= size(HEIGHT, GREATER_THAN, height);
    lwin |= size(WIDTH, GREATER_THAN, width);
    rwin |= size(HEIGHT, GREATER_THAN, height);
    rwin |= size(WIDTH, GREATER_THAN, width);
    return vbox({text("Left Channel") | hcenter, lwin, separator(), text("Right Channel") | hcenter, rwin});
}

static ftxui::Element construct_infos(const ChannelInfo &sta_info)
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

    return vbox({
        window(text("FPS") | hcenter | bold, text(std::to_string(fps_count())) | hcenter),
        window(text("Statistics No." + std::to_string((unsigned int)sta_info.token)) | hcenter | bold, vbox(std::move(content))),
    });
}

int main(int argc, char **argv)
{
    start_audio_service();
    {
        UiElement ui_element{DEBUG_TOOL_FRESH_INTERV};
        auto ob = std::make_shared<Observer>(&ui_element, DEBUG_TOOL_FRESH_INTERV);
        if (!ob->start())
        {
            return 0;
        }

        auto screen = ftxui::ScreenInteractive::Fullscreen();
        screen.SetCursor(ftxui::Screen::Cursor{0});

        int tab_selected = 0;
        std::vector<std::string> tab_values{"WAVE", "ENERGY"};
        auto tab_toggle = ftxui::Toggle(&tab_values, &tab_selected);
        auto tab1 = ftxui::Renderer(
            [&ui_element, tab_selected]()
            {
                return tab_selected == 0 ? construct_graph(SMALL_WINDOWS_SIZE[0], SMALL_WINDOWS_SIZE[1], ui_element.wave_left, ui_element.wave_right) : ftxui::text("");
            });
        auto tab2 = ftxui::Renderer(
            [&ui_element, &tab_selected]()
            {
                return tab_selected == 1 ? construct_graph(SMALL_WINDOWS_SIZE[0], SMALL_WINDOWS_SIZE[1], ui_element.energy_left, ui_element.energy_right) : ftxui::text("");
            });
        auto tab_container = ftxui::Container::Tab(
            {tab1, tab2},
            &tab_selected);
        auto container = ftxui::Container::Vertical({
            tab_toggle,
            tab_container,
        });

        auto renderer = ftxui::Renderer(container, [&tab_toggle, &tab_container, &ui_element]
                                        {
            std::lock_guard<std::mutex> grd(fresh_mtx);
            auto tbc = ftxui::vbox({
                tab_toggle->Render(),
                ftxui::separator(),
                tab_container->Render(),
            });
            auto text_box = construct_infos(ui_element.info);
            auto document = ftxui::window(ftxui::text("Audio Debug Tool " + std::string(__DATE__)) | ftxui::hcenter | ftxui::bold, ftxui::hbox({std::move(tbc) | ftxui::flex, ftxui::separator(), text_box}));
            document |= ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 48);
            return document; });

        ob->set_callback([&screen]()
                         { screen.Post(ftxui::Event::Custom); });
        screen.Loop(renderer);
    }
    stop_audio_service();
    return 0;
}