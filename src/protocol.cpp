#include "protocol.h"
#include <mutex>
#include <cstring>

namespace
{
    std::mutex mem_mtx;
    constexpr uint8_t AUDIO_PACKET_MAGIC_NUM1 = 0xab;
    constexpr uint8_t AUDIO_PACKET_MAGIC_NUM2 = 0xcd;
    constexpr uint8_t AUDIO_PACKET_CHANN_MONO = 0x01;
    constexpr uint8_t AUDIO_PACKET_CHANN_DUAL = 0x02;
    constexpr auto MINMUM_AUDIO_PACKET_SIZE = sizeof(AudioPacket::Header);
    constexpr auto MINMUM_MALLOC_PACKET_LEVEL = 6;
    constexpr auto MAXMUM_MALLOC_PACKET_LEVEL = 13;
    constexpr auto MINMUM_MALLOC_PACKET_SIZE = (uint32_t)1 << MINMUM_MALLOC_PACKET_LEVEL;
    constexpr auto MAXMUM_MALLOC_PACKET_SIZE = (uint32_t)1 << MAXMUM_MALLOC_PACKET_LEVEL;
}

AudioPacket *AudioPacket::free_list[MAXMUM_MALLOC_PACKET_LEVEL - MINMUM_MALLOC_PACKET_LEVEL + 1] = {};

static int scaled_integer_log2(uint32_t x)
{
    int l = MINMUM_MALLOC_PACKET_LEVEL;
    while (x >= MINMUM_MALLOC_PACKET_SIZE)
    {
        l += 1;
        x >>= 1;
    }
    return l;
}

AudioPacket::AudioPacket(const char *data, size_t total_data_len)
    : prev(this), next(this), payload((char *)(&header) + sizeof(header)), header{}
{
    if (data)
    {
        std::memcpy(&header, data, total_data_len);
    }
}

void *AudioPacket::operator new(size_t header_size, size_t body_size)
{
    auto size_level = scaled_integer_log2(body_size);
    auto &free_list_chosen = free_list[size_level - MINMUM_MALLOC_PACKET_LEVEL];

    std::lock_guard<std::mutex> grd(mem_mtx);
    if (free_list_chosen == nullptr)
    {
        return std::malloc(((size_t)1 << size_level) + header_size);
    }

    auto *temp = free_list_chosen;
    free_list_chosen = free_list_chosen->next;
    return temp;
}

void AudioPacket::operator delete(void *ptr, size_t body_size)
{
    auto packet_ptr = reinterpret_cast<AudioPacket *>(ptr);
    auto size_level = scaled_integer_log2(packet_ptr->header.data_len);
    auto &free_list_chosen = free_list[size_level - MINMUM_MALLOC_PACKET_LEVEL];

    std::lock_guard<std::mutex> grd(mem_mtx);
    packet_ptr->next = free_list_chosen;
    free_list_chosen = packet_ptr;
}

void AudioPacket::operator delete(void *ptr)
{
    auto packet_ptr = reinterpret_cast<AudioPacket *>(ptr);
    auto size_level = scaled_integer_log2(packet_ptr->header.data_len);
    auto &free_list_chosen = free_list[size_level - MINMUM_MALLOC_PACKET_LEVEL];

    std::lock_guard<std::mutex> grd(mem_mtx);
    packet_ptr->next = free_list_chosen;
    free_list_chosen = packet_ptr;
}

void AudioPacket::free()
{
    std::lock_guard<std::mutex> grd(mem_mtx);
    for (int i = 0; i < MAXMUM_MALLOC_PACKET_LEVEL - MINMUM_MALLOC_PACKET_LEVEL + 1; i++)
    {
        auto idx = 0;
        auto &free_list_chosen = free_list[i];
        while (free_list_chosen != nullptr)
        {
            auto temp = free_list_chosen;
            free_list_chosen = free_list_chosen->next;
            std::free(temp);
            ++idx;
        }
        printf("recycle free list %d, length: %d\n", i, idx);
    }
}

AudioPacket *AudioPacket::make(const char *data, size_t total_len)
{
    if (!data)
    {
        return nullptr;
    }

    if (total_len > MAXMUM_MALLOC_PACKET_SIZE || total_len < MINMUM_AUDIO_PACKET_SIZE)
    {
        return nullptr;
    }
    if (data[0] != AUDIO_PACKET_MAGIC_NUM1 || data[1] != AUDIO_PACKET_MAGIC_NUM2)
    {
        return nullptr;
    }
    if (data[3] != 1 || data[3] != 2)
    {
        return nullptr;
    }

    return new (total_len - MINMUM_AUDIO_PACKET_SIZE) AudioPacket(data, total_len);
}

AudioPacket *AudioPacket::make(uint8_t sender, bool mono_chann, uint64_t time_us, uint32_t iseq, const char *data, size_t len)
{
    auto self = new (len) AudioPacket(nullptr, len + sizeof(AudioPacket));

    self->header = {
        AUDIO_PACKET_MAGIC_NUM1,
        AUDIO_PACKET_MAGIC_NUM2,
        sender,
        mono_chann ? AUDIO_PACKET_CHANN_MONO : AUDIO_PACKET_CHANN_DUAL,
        0,
        time_us,
        iseq,
        (uint32_t)len};
    if (data)
    {
        std::memcpy(self->payload, data, len);
    }
    return self;
}

AudioPacketList::AudioPacketList()
    : head(nullptr), length(0)
{
    head = AudioPacket::make(0, false, 0, 0, nullptr, 0);
}

AudioPacketList::~AudioPacketList()
{
    delete head;
}

void AudioPacketList::push(AudioPacket *node)
{
    if (!node)
    {
        return;
    }

    node->next = head->next;
    node->prev = head;
    head->next = node;
    node->next->prev = node;
    length++;
}

AudioPacket *AudioPacketList::pop()
{
    if (head->prev == head)
    {
        return nullptr;
    }

    auto last = head->prev;
    last->prev->next = last->next;
    last->next->prev = last->prev;
    return last;
}

void AudioPacketList::insert(AudioPacket *node)
{
    if (!node)
    {
        return;
    }

    node->next = head->next;
    node->prev = head;
    head->next = node;
    node->next->prev = node;
    length++;
}

void AudioPacketList::remove(AudioPacket *node)
{
    if (!node)
    {
        return;
    }

    node->prev->next = node->next;
    node->next->prev = node->prev;
    length--;
}

size_t AudioPacketList::size() const
{
    return length;
}
