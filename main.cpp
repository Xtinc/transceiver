#include "audio_interface.h"
#include <chrono>
#include <fstream>
#include <thread>

int main(int, char **)
{
    start_audio_service();
    {
        // test output
        // OAStream oas(97);
        // oas.start();
        IAStream ias(66, "default_input", AudioBandWidth::Full, AudioPeriodSize::INR_05MS);
        // IAStream ias(66, "raphael.wav");
        ias.start();
        ias.connect("192.168.1.9", 255);
        // ias.connect("127.0.0.1", 255);
        std::this_thread::sleep_for(std::chrono::seconds(2000));
    }
    stop_audio_service();
    return 0;
}