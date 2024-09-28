
#include "tool.h"
#include "ftxui/dom/node.hpp"      // for Render
#include "ftxui/screen/color.hpp"  // for Color, Color::BlueLight, Color::RedLight, Color::YellowLight, ftxui
#include "ftxui/dom/elements.hpp"  // for graph, operator|, separator, color, Element, vbox, flex, inverted, operator|=, Fit, hbox, size, border, GREATER_THAN, HEIGHT
#include "ftxui/screen/screen.hpp" // for Full, Screen
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include <iostream>
#include <thread>
#include <condition_variable>

using udp = asio::ip::udp;

static constexpr uint8_t DEBUG_TOOL_TOKEN = UINT8_MAX;
static constexpr unsigned short DEBUG_TOOL_PORT = (uint16_t)(0xccu << 8) + (uint16_t)DEBUG_TOOL_TOKEN;
static constexpr auto DEBUG_TOOL_SAMPLE_RATE = AudioBandWidth::Full;
static constexpr auto DEBUG_TOOL_FRESH_INTERV = AudioPeriodSize::INR_40MS;
static constexpr auto MAXIMUM_TRANSMISSION_SIZE = 1024 * 6;

static constexpr int SMALL_WINDOWS_SIZE[2] = {11, 60};
static constexpr int MEDIUM_WINDOWS_SIZE[2] = {21, 100};

WaveGrapha::WaveGrapha(AudioPeriodSize max_len) : length(enum2val(DEBUG_TOOL_SAMPLE_RATE) * enum2val(max_len) / 1000)
{
    data = new int16_t[length];
    std::memset(data, 0, length * sizeof(int16_t));
}

WaveGrapha::~WaveGrapha()
{
    delete[] data;
}

std::vector<int> WaveGrapha::operator()(int width, int height)
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

void WaveGrapha::set_data(const int16_t *ssrc, int ssrc_chan, int frames_num, int chan_idx)
{
    if (frames_num != length)
    {
        return;
    }

    std::memset(data, 0, sizeof(int16_t) * length);
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

Observer::Observer(OAStream &default_oas, WaveGrapha *graph0, WaveGrapha *graph1, AudioPeriodSize interval_ms)
    : fresh_interv(enum2val(interval_ms)), fs(enum2val(DEBUG_TOOL_SAMPLE_RATE)), ps(fs * fresh_interv / 1000), oas(default_oas), lgraph(graph0), rgraph(graph1), timer(SERVICE), recv_buf(nullptr), oas_ready(false)
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
                    // auto session = std::make_unique<SessionData>(self->ps * chan * sizeof(int16_t), 18, chan);
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

    if (!lgraph || !rgraph)
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
            oas.direct_push_pcm(s->first, ichan, ps, fs, idata);
            lgraph->set_data(idata, ichan, ps, 0);
            rgraph->set_data(idata, ichan, ps, 1);
        }
    }

    if (cb)
    {
        cb(lgraph, rgraph);
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

ftxui::Element construct_graph(int height, int width, WaveGrapha &usr_graph, const std::string &title)
{
    using namespace ftxui;
    auto win = window(text(title) | hcenter | bold,
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
                          graph(std::ref(usr_graph)) | flex | color(Color::BlueLight),
                      }));
    win |= size(HEIGHT, GREATER_THAN, height);
    win |= size(WIDTH, GREATER_THAN, width);
    return win;
}

int main(int argc, char **argv)
{
    start_audio_service();
    {
        WaveGrapha wave_lgraph(DEBUG_TOOL_FRESH_INTERV);
        WaveGrapha wave_rgraph(DEBUG_TOOL_FRESH_INTERV);
        OAStream oas(254, "default_output", DEBUG_TOOL_SAMPLE_RATE, DEBUG_TOOL_FRESH_INTERV);
        auto ob = std::make_shared<Observer>(oas, &wave_lgraph, &wave_rgraph, DEBUG_TOOL_FRESH_INTERV);
        oas.start();
        if (!ob->start())
        {
            return 0;
        }

        auto screen = ftxui::ScreenInteractive::Fullscreen();
        screen.SetCursor(ftxui::Screen::Cursor{0});

        auto render = ftxui::Renderer(
            [&wave_lgraph, &wave_rgraph]()
            {
            auto left_win = construct_graph(MEDIUM_WINDOWS_SIZE[0], MEDIUM_WINDOWS_SIZE[1], wave_lgraph, "Left channel");
            auto right_win = construct_graph(MEDIUM_WINDOWS_SIZE[0], MEDIUM_WINDOWS_SIZE[1], wave_rgraph, "Right channel");

            auto document = ftxui::vbox({std::move(left_win), std::move(right_win)}) | ftxui::flex | ftxui::border;
            document |= ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 120);
            document |= ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 160);
            return document; });

        ob->set_callback([&screen](WaveGrapha *lg, WaveGrapha *rg)
                         { screen.Post(ftxui::Event::Custom); });
        screen.Loop(render);
    }
    stop_audio_service();
    return 0;
}