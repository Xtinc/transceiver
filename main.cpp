#include "protocol.h"
#include <chrono>
#include <thread>
#include <iostream>

struct DATA1
{
    char AAA[12]{'1', '2', '3', '4', '5'};
};

struct DATA2
{
    char AAA[68]{'1', '2', '3', '4', '5'};
};

struct DATA3
{
    char AAA[188]{'1', '2', '3', '4', '5'};
};

struct DATA4
{
    char AAA[288]{'1', '2', '3', '4', '5'};
};

int main(int, char **)
{
    auto aaaa = DATA1();
    auto bbbb = DATA2();
    auto cccc = DATA3();
    auto dddd = DATA4();
    {
        AudioPacketList list;
        for (size_t i = 0; i < 100; i++)
        {
            list.insert(AudioPacket::make(1, true, 0, i, (const char *)&aaaa, sizeof(aaaa)));
            list.insert(AudioPacket::make(1, true, 0, i, (const char *)&bbbb, sizeof(bbbb)));
            list.insert(AudioPacket::make(1, true, 0, i, (const char *)&cccc, sizeof(cccc)));
            list.insert(AudioPacket::make(1, true, 0, i, (const char *)&dddd, sizeof(dddd)));
        }
        for (AudioPacket *iter = list.pop(); iter != nullptr; iter = list.pop())
        {
            delete iter;
        }
        for (size_t i = 0; i < 100; i++)
        {
            list.insert(AudioPacket::make(1, true, 0, i, (const char *)&aaaa, sizeof(aaaa)));
            list.insert(AudioPacket::make(1, true, 0, i, (const char *)&bbbb, sizeof(bbbb)));
            list.insert(AudioPacket::make(1, true, 0, i, (const char *)&cccc, sizeof(cccc)));
            list.insert(AudioPacket::make(1, true, 0, i, (const char *)&dddd, sizeof(dddd)));
            delete list.pop();
            delete list.pop();
            delete list.pop();
            delete list.pop();
        }
    }
    AudioPacket::free();
    return 0;
}
