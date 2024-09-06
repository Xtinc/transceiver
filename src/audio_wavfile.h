#ifndef AUDIO_WAVEFILE_HEADER
#define AUDIO_WAVEFILE_HEADER

#include <cinttypes>
#include <fstream>
#include <vector>

enum class WavErrorCode : int
{
    NoError = 0,
    FailedToOpen,
    NotOpen,
    InvalidFormat,
    WriteError,
    ReadError,
    InvalidSeek
};

inline const char *WavError2str(const WavErrorCode &wav_ec)
{
    switch (wav_ec)
    {
    case WavErrorCode::NoError:
        return "No Error";
    case WavErrorCode::FailedToOpen:
        return "Failed To Open";
    case WavErrorCode::NotOpen:
        return "Not Open";
    case WavErrorCode::InvalidFormat:
        return "Invalid Format";
    case WavErrorCode::WriteError:
        return "Write Error";
    case WavErrorCode::ReadError:
        return "Read Error";
    case WavErrorCode::InvalidSeek:
        return "Invalid Seek";
    default:
        return nullptr;
    }
}

class ChunkList;

class WavFile
{
  public:
    enum mode
    {
        in,
        out
    };

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

    WavErrorCode open(const std::string &path, mode mode);

    // output should have a length more than frame_number*channel_number
    WavErrorCode read(int16_t *output, uint64_t frame_number);

    WavErrorCode read(std::vector<int16_t> &output, uint64_t frame_number);

    WavErrorCode read(std::vector<int16_t> &output);

    WavErrorCode write(const int16_t *input, uint64_t frame_number);

    WavErrorCode write(const std::vector<int16_t> &input, uint64_t frame_number);

    WavErrorCode write(const std::vector<int16_t> &input);

    WavErrorCode seek(uint64_t frame_index);

    uint64_t tell() const;

    uint16_t channel_number() const;
    void set_channel_number(uint16_t channel_number);

    uint32_t sample_rate() const;
    void set_sample_rate(uint32_t sample_rate);

    uint16_t bits_per_sample() const;
    void set_bits_per_sample(uint16_t bits_per_sample);

    uint64_t frame_number() const;

  private:
    WavErrorCode write_header(uint64_t data_size);

    WavErrorCode read_header(ChunkList *headers);

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
        uint64_t data_index;
        if (ostream.is_open())
        {
            data_index = static_cast<uint64_t>(ostream.tellp()) - data_offset;
        }
        else if (istream.is_open())
        {
            data_index = static_cast<uint64_t>(istream.tellg()) - data_offset;
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

        std::streampos stream_index = data_offset + (sample_idx * bytes_per_sample);
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
    Header header{};
    uint64_t data_offset{};
};

#endif