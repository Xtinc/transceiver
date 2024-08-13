/*                    Packet Frame Format
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  sender id   |    channels   |  sample rate  | encoder format |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                            sequence                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           timestamp                           |
+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
|                            payload                            |
|                             ....                              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/

#ifndef AUDIO_PROTOCOL_HEADER
#define AUDIO_PROTOCOL_HEADER

#include <cstdlib>
#include <cinttypes>

enum class AudioBandWidth : int
{
    Narrow = 8'000,
    Wide = 16'000,
    SemiSuperWide = 24'000,
    SuperWide = 32'000,
    Full = 48'000
};

enum class AudioPeriodSize : int
{
    INR_05MS = 0x05,
    INR_10MS = 0x10,
    INR_15MS = 0x15,
    INR_20MS = 0x20,
    INR_25MS = 0x25,
    INR_30MS = 0x30
};

enum class AudioEncoderFormat : int
{
    PCM = 0,
    OPUS = 1
};

struct AudioPackHeader
{
    uint8_t sender;
    uint8_t channel;
    uint8_t fs_rate;
    uint8_t enc_fmt;
    uint32_t sequence;
    uint64_t timestamp;

    static bool validate(const char *data, size_t len);
};

#endif