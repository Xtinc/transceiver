#ifndef AUDIO_PROTOCOL_HEADER
#define AUDIO_PROTOCOL_HEADER

struct AudioPacket
{
public:
    AudioPacket *prev;
    AudioPacket *next;

    char *payload;
    size_t length;

    static void *operator new(size_t header_size, size_t body_size);

    static void operator delete(void *ptr);

    static void free();

private:
    static AudioPacket *free_list0;
};
#endif