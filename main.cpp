#include "audio_interface.h"
#include <chrono>
#include <fstream>
#include <thread>

int main(int, char **)
{
    start_audio_service();
    {
        // test output
        OAStream oas(97, "NVIDIA Jetson AGX Xavier APE,0", AudioBandWidth::Full);
        //OAStream oas(97, "yu.pcm",AudioBandWidth::Full);
        oas.start();
        
        //IAStream ias(66, "Logi USB Headset H340");
        IAStream ias(66, "bothlent usb audio", AudioBandWidth::Full);
        ias.start();
        ias.connect(oas);

        AudioPlayer player(1);
        player.play("./resource/Raphael_final_act.wav", oas);
        std::this_thread::sleep_for(std::chrono::seconds(20));
        player.stop("./resource/Raphael_final_act.wav");
        player.play("./resource/chinese_modern.wav", oas);
        // IAStream ias1(11, "default_input", AudioBandWidth::Narrow);
        // IAStream ias1(11, "Raphael_final_act.wav");
        // IAStream ias2(12, "bothlent usb audio");
        // IAStream ias3(13, "Jetson AGX Xavier APE,1.multi", AudioBandWidth::Full);
        // OAStream oas(AudioPeriodSize::INR_10MS, "Logi USB Headset H340");
        // OAStream oas(99, "default_output", AudioBandWidth::Full);
        // ias1.set_callback(test_cb, nullptr);
        // ias.connect(oas);
        // ias1.connect(oas);
        // ias1.connect("127.0.0.1", 99);
        // ias2.connect(oas);
        // ias3.connect(oas);
        // ias.connect("172.16.1.113");
        // ias.connect(oas);
        // ias1.start();
        // ias2.start();
        // ias3.start();
        // ias.start();
        std::this_thread::sleep_for(std::chrono::seconds(2000));
    }
    stop_audio_service();
    return 0;
}