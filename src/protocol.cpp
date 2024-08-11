#include "protocol.h"
#include <mutex>

std::mutex mem_mtx;

void *AudioPacket::operator new(size_t header_size, size_t body_size)
{
    std::lock_guard<std::mutex> grd(mem_mtx);
    if (free_list0 == nullptr)
    {
        return std::malloc(header_size + body_size);
    }
    auto *temp = free_list0;
    free_list0 = free_list0->next;
    return temp;
}

void AudioPacket::operator delete(void *ptr)
{
    std::lock_guard<std::mutex> grd(mem_mtx);
    ((AudioPacket *)ptr)->next = free_list0;
    free_list0 = (AudioPacket *)ptr;
}

void AudioPacket::free()
{
    std::lock_guard<std::mutex> grd(mem_mtx);
    while (free_list0 != nullptr)
    {
        auto temp = free_list0;
        free_list0 = free_list0->next;
        std::free(temp);
    }
}
