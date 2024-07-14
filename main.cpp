#include "transceiver.h"

int main(int, char **)
{
    start_sound_control();
    {
        TransCeiver recv('a', 9992, 48'000, 1024);
        if (recv.connect("127.0.0.1", 9994))
        {
            recv.start();
            std::this_thread::sleep_for(std::chrono::seconds(2));
            recv.play("rec.wav");
        }
    }
    close_sound_control();
    return 0;
}
