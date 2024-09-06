#include "audio_interface.h"
#include <chrono>
#include <fstream>
#include <thread>

int main(int, char **)
{
    start_audio_service();
    {
        // test output
        OAStream oas(97);
        oas.start();
        IAStream ias(66);
        ias.start();
        ias.connect(oas);
        std::this_thread::sleep_for(std::chrono::seconds(2000));
    }
    stop_audio_service();
    return 0;
}