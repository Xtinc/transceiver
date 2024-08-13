#ifndef PROTOCOL_HEADER
#define PROTOCOL_HEADER

#include <cstdlib>
#include <cinttypes>

/*                    Packet Frame Format
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| magica_number1| magica_number2|   sender id   | channel number|
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           reservered                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                            sequence                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                            length                             |
+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
|                            payload                            |
|                             ....                              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/

struct AudioPacket
{
public:
    AudioPacket *prev;
    AudioPacket *next;
    char *payload;

    struct Header
    {
        uint8_t mag1c_num;
        uint8_t mag2c_num;
        uint8_t sender_id;
        uint8_t chann_num;
        uint32_t reserved;
        uint64_t timestamp;
        uint32_t sequence;
        uint32_t data_len;
    } header;

    static void *operator new(size_t header_size, size_t body_size);

    static void operator delete(void *ptr, size_t body_size);

    static void operator delete(void *ptr);

    static void free();

    static AudioPacket *make(const char *data, size_t total_len);

    static AudioPacket *make(uint8_t sender, bool mono_chann, uint64_t time_us, uint32_t iseq, const char *data, size_t len);

private:
    static AudioPacket *free_list[8];

private:
    AudioPacket(const char *data, size_t len);
};

class AudioPacketList
{
public:
    AudioPacketList();
    ~AudioPacketList();

    void push(AudioPacket *node);

    AudioPacket *pop();

    void insert(AudioPacket *node);

    void remove(AudioPacket *node);

    size_t size() const;

private:
    AudioPacket *head;
    size_t length;
};
#endif