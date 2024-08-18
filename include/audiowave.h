#ifndef AUDIO_WAVE_FILE_HEADER
#define AUDIO_WAVE_FILE_HEADER
#include <cinttypes>
#include <fstream>
#include <vector>

enum class Error
{
    kNoError = 0,
    kFailedToOpen,
    kNotOpen,
    kInvalidFormat,
    kWriteError,
    kReadError,
    kInvalidSeek
};

enum OpenMode
{
    kIn,
    kOut
};

class HeaderList;

class WavFile
{
public:
    struct RIFFChunk
    {
        char chunk_id[4];
        uint32_t chunk_size;
        char format[4];
    };

    struct FMTChunk
    {
        char sub_chunk_1_id[4];
        uint32_t sub_chunk_1_size;
        uint16_t audio_format;
        uint16_t num_channel;
        uint32_t sample_rate;
        uint32_t byte_rate;
        uint16_t byte_per_block;
        uint16_t bits_per_sample;
    };

    struct DataChunk
    {
        char sub_chunk_2_id[4];
        uint32_t sub_chunk_2_size;
    };

    struct Header
    {
        RIFFChunk riff;
        FMTChunk fmt;
        DataChunk data;
    };

public:
    WavFile();
    ~WavFile();

    Error Open(const std::string &path, OpenMode mode);

    Error Read(std::vector<float> *output);

    Error Read(uint64_t frame_number, std::vector<float> *output);

    Error Write(const std::vector<float> &data, bool clip = false);

    Error Seek(uint64_t frame_index);

    uint64_t Tell() const;

    uint16_t channel_number() const;
    void set_channel_number(uint16_t channel_number);

    uint32_t sample_rate() const;
    void set_sample_rate(uint32_t sample_rate);

    uint16_t bits_per_sample() const;
    void set_bits_per_sample(uint16_t bits_per_sample);

    uint64_t frame_number() const;

private:
    Error WriteHeader(uint64_t data_size);

    Error ReadHeader(HeaderList *headers);

    uint64_t sample_number() const
    {
        auto bits_per_sample = header.fmt.bits_per_sample;
        auto bytes_per_sample = bits_per_sample / 8;

        auto total_data_size = header.data.sub_chunk_2_size;
        return total_data_size / bytes_per_sample;
    }

    uint64_t current_sample_index() const
    {
        auto bits_per_sample = header.fmt.bits_per_sample;
        auto bytes_per_sample = bits_per_sample / 8;
        uint64_t data_index = 0;
        if (ostream.is_open())
        {
            data_index = static_cast<uint64_t>(ostream.tellp()) - data_offset_;
        }
        else if (istream.is_open())
        {
            data_index = static_cast<uint64_t>(istream.tellg()) - data_offset_;
        }
        else
        {
            return 0;
        }
        return data_index / bytes_per_sample;
    }

    void set_current_sample_index(uint64_t sample_idx)
    {
        auto bits_per_sample = header.fmt.bits_per_sample;
        auto bytes_per_sample = bits_per_sample / 8;

        std::streampos stream_index =
            data_offset_ + (sample_idx * bytes_per_sample);
        if (ostream.is_open())
        {
            ostream.seekp(stream_index);
        }
        else if (istream.is_open())
        {
            istream.seekg(stream_index);
        }
    }

private:
    mutable std::ifstream istream;
    mutable std::ofstream ostream;
    Header header;
    uint64_t data_offset_;
};

#endif