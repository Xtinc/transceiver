#include "transceiver.h"

int main(int, char **)
{
    start_sound_control();
    {
        Receiver recv(2, 48'000, 768);
        recv.listen();
    }
    close_sound_control();
    return 0;
}
