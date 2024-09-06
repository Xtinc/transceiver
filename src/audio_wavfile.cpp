#include "audio_wavfile.h"
#include <cstring>

class Chunk
{
  public:
    WavErrorCode init(std::ifstream *stream, uint64_t position)
    {
        pos = position;
        if (!stream->is_open())
        {
            return WavErrorCode::NotOpen;
        }

        // read chunk ID
        const auto chunk_id_size = 4;
        stream->seekg(static_cast<std::streamsize>(pos), std::ios::beg);
        char result[chunk_id_size];
        stream->read(result, chunk_id_size * sizeof(char));
        id = std::string(result, chunk_id_size);

        // and size
        stream->read(reinterpret_cast<char *>(&size), sizeof(uint32_t));
        size += chunk_id_size * sizeof(char) + sizeof(uint32_t);

        return WavErrorCode::NoError;
    }

    std::string chunk_id() const
    {
        return id;
    }

    uint32_t chunk_size() const
    {
        if (chunk_id() == "RIFF")
        {
            return sizeof(WavFile::RIFFChunk);
        }
        return size;
    }

    uint64_t position() const
    {
        return pos;
    }

  private:
    std::string id;
    uint32_t size{};
    uint64_t pos{};
};

class ChunkList
{
  public:
    class iterator
    {
      public:
        iterator(std::ifstream *stream, uint64_t position) : stream_(stream), position_(position)
        {
        }
        iterator operator++()
        {
            Chunk h;
            h.init(stream_, position_);
            position_ += h.chunk_size();
            return *this;
        }
        iterator operator++(int)
        {
            iterator next = *this;
            operator++();
            return next;
        }
        Chunk operator*()
        {
            Chunk h;
            h.init(stream_, position_);
            return h;
        }
        bool operator==(const iterator &rhs)
        {
            return rhs.stream_ == stream_ && rhs.position_ == position_;
        }
        bool operator!=(const iterator &rhs)
        {
            return !operator==(rhs);
        }

      private:
        std::ifstream *stream_;
        uint64_t position_;
    };

    WavErrorCode init(const std::string &path)
    {
        ifs.open(path.c_str(), std::ios::binary);
        if (!ifs.is_open())
        {
            return WavErrorCode::FailedToOpen;
        }
        return WavErrorCode::NoError;
    }

    iterator begin()
    {
        return {&ifs, 0};
    }

    iterator end()
    {
        ifs.seekg(0, std::ios::end);
        uint64_t size = ifs.tellg();
        ifs.seekg(0, std::ios::beg);
        return {&ifs, size};
    }

    Chunk riff()
    {
        return header("RIFF");
    }

    Chunk fmt()
    {
        return header("fmt ");
    }

    Chunk data()
    {
        return header("data");
    }

  private:
    Chunk header(const std::string &header_id)
    {
        for (auto header : *this)
        {
            if (header.chunk_id() == header_id)
            {
                return header;
            }
        }
        return *begin();
    }

  private:
    std::ifstream ifs;
};

template <typename T> void cast_header(std::ifstream &ifs, const Chunk& generic_header, T *output)
{
    ifs.seekg(static_cast<std::streamsize>(generic_header.position()), std::ios::beg);
    ifs.read(reinterpret_cast<char *>(output), sizeof(T));
}

WavFile::WavFile()
{
    strncpy(header.riff.chunk_id, "RIFF", 4);
    strncpy(header.riff.format, "WAVE", 4);

    strncpy(header.fmt.sub_chunk_1_id, "fmt ", 4);
    header.fmt.sub_chunk_1_size = 16;
    // default values
    header.fmt.audio_format = 1; // PCM
    header.fmt.num_channel = 1;
    header.fmt.sample_rate = 44100;
    header.fmt.bits_per_sample = 16;
    header.fmt.byte_per_block = (header.fmt.bits_per_sample * header.fmt.num_channel) / 8;
    header.fmt.byte_rate = header.fmt.byte_per_block * header.fmt.sample_rate;

    strncpy(header.data.sub_chunk_2_id, "data", 4);
}

WavFile::~WavFile()
{
    if (istream.is_open())
    {
        ostream.flush();
    }
}

WavErrorCode WavFile::open(const std::string &path, mode mode)
{
    if (mode == mode::out)
    {
        ostream.open(path.c_str(), std::ios::binary | std::ios::trunc);
        if (!ostream.is_open())
        {
            return WavErrorCode::FailedToOpen;
        }
        return write_header(0);
    }
    istream.open(path.c_str(), std::ios::binary);
    if (!istream.is_open())
    {
        return WavErrorCode::FailedToOpen;
    }
    ChunkList headers;
    auto error = headers.init(path);
    if (error != WavErrorCode::NoError)
    {
        return error;
    }
    return read_header(&headers);
}

WavErrorCode WavFile::read(int16_t *output, uint64_t frame_number)
{
    if (!istream.is_open())
    {
        return WavErrorCode::NotOpen;
    }
    auto requested_samples = frame_number * channel_number();
    if (sample_number() < requested_samples + current_sample_index())
    {
        return WavErrorCode::InvalidFormat;
    }

    switch (header.fmt.bits_per_sample)
    {
    case 8:
        for (size_t i = 0; i < requested_samples; i++)
        {
            int8_t value;
            istream.read(reinterpret_cast<char *>(&value), sizeof(value));
            *output++ = static_cast<int16_t>(value * 256);
        }
        break;
    case 16:
        istream.read(reinterpret_cast<char *>(output), (std::streamsize)(requested_samples * sizeof(*output)));
        break;
    case 24:
        for (size_t i = 0; i < requested_samples; i++)
        {
            unsigned char value[3];
            istream.read(reinterpret_cast<char *>(&value), sizeof(value));
            int integer_value;
            // check if value is negative
            if (value[2] & 0x80)
            {
                integer_value = (0xff << 24) | (value[2] << 16) | (value[1] << 8) | (value[0] << 0);
            }
            else
            {
                integer_value = (value[2] << 16) | (value[1] << 8) | (value[0] << 0);
            }
            *output++ = static_cast<int16_t>(integer_value / 256);
        }
        break;
    case 32:
        for (size_t i = 0; i < requested_samples; i++)
        {
            int32_t value;
            istream.read(reinterpret_cast<char *>(&value), sizeof(value));
            *output++ = static_cast<int16_t>(value / 65536);
        }
        break;
    default:
        return WavErrorCode::InvalidFormat;
    }
    return WavErrorCode::NoError;
}

WavErrorCode WavFile::read(std::vector<int16_t> &output, uint64_t frame_number)
{
    output.resize(frame_number * channel_number());
    return read(output.data(), frame_number);
}

WavErrorCode WavFile::read(std::vector<int16_t> &output)
{
    return read(output, frame_number());
}

WavErrorCode WavFile::write(const int16_t *input, uint64_t frame_number)
{
    if (!ostream.is_open())
    {
        return WavErrorCode::NotOpen;
    }

    auto current_data_size = current_sample_index();
    auto requested_samples = frame_number * channel_number();

    switch (header.fmt.bits_per_sample)
    {
    case 8:
        for (size_t i = 0; i < requested_samples; i++)
        {
            auto value = static_cast<int8_t>(*input++ / 256);
            ostream.write(reinterpret_cast<const char *>(&value), sizeof(value));
        }
        break;
    case 16:
        ostream.write(reinterpret_cast<const char *>(input), (std::streamsize)(requested_samples * sizeof(*input)));
        break;
    case 24:
        for (auto i = 0; i < requested_samples; ++i)
        {
            int v = (*input++) / 256;
            int8_t value[3];
            value[0] = reinterpret_cast<char *>(&v)[0];
            value[1] = reinterpret_cast<char *>(&v)[1];
            value[2] = reinterpret_cast<char *>(&v)[2];

            ostream.write(reinterpret_cast<const char *>(&value), sizeof(value));
        }
        break;
    case 32:
        for (size_t i = 0; i < requested_samples; i++)
        {
            auto value = static_cast<int32_t>(*(input + i) * 65536);
            ostream.write(reinterpret_cast<const char *>(&value), sizeof(value));
        }
        break;
    default:
        return WavErrorCode::InvalidFormat;
    }

    write_header(current_data_size + requested_samples);
    return WavErrorCode::NoError;
}

WavErrorCode WavFile::write(const std::vector<int16_t> &input, uint64_t frame_number)
{
    return write(input.data(), frame_number);
}

WavErrorCode WavFile::write(const std::vector<int16_t> &input)
{
    return write(input.data(), input.size() / channel_number());
}

WavErrorCode WavFile::seek(uint64_t frame_index)
{
    if (!ostream.is_open() && !istream.is_open())
    {
        return WavErrorCode::NotOpen;
    }
    if (frame_index > frame_number())
    {
        return WavErrorCode::InvalidSeek;
    }

    set_current_sample_index(frame_index * channel_number());
    return WavErrorCode::NoError;
}

uint64_t WavFile::tell() const
{
    if (!ostream.is_open() && !istream.is_open())
    {
        return 0;
    }

    return current_sample_index() / channel_number();
}

uint16_t WavFile::channel_number() const
{
    return header.fmt.num_channel;
}

void WavFile::set_channel_number(uint16_t channel_number)
{
    header.fmt.num_channel = channel_number;
}

uint32_t WavFile::sample_rate() const
{
    return header.fmt.sample_rate;
}

void WavFile::set_sample_rate(uint32_t sample_rate)
{
    header.fmt.sample_rate = sample_rate;
}

uint16_t WavFile::bits_per_sample() const
{
    return header.fmt.bits_per_sample;
}

void WavFile::set_bits_per_sample(uint16_t bits_per_sample)
{
    header.fmt.bits_per_sample = bits_per_sample;
}

uint64_t WavFile::frame_number() const
{
    return sample_number() / channel_number();
}

WavErrorCode WavFile::write_header(uint64_t data_size)
{
    if (!ostream.is_open())
    {
        return WavErrorCode::NotOpen;
    }
    auto original_position = ostream.tellp();
    // Position to beginning of file
    ostream.seekp(0);

    // make header
    auto bits_per_sample = header.fmt.bits_per_sample;
    auto bytes_per_sample = bits_per_sample / 8;
    auto channel_number = header.fmt.num_channel;
    auto sample_rate = header.fmt.sample_rate;

    header.riff.chunk_size = sizeof(Header) + (data_size * (bits_per_sample / 8)) - 8;
    // fmt header
    header.fmt.byte_per_block = bytes_per_sample * channel_number;
    header.fmt.byte_rate = sample_rate * header.fmt.byte_per_block;
    // data header
    header.data.sub_chunk_2_size = data_size * bytes_per_sample;

    ostream.write(reinterpret_cast<char *>(&header), sizeof(Header));
    if (ostream.fail())
    {
        return WavErrorCode::WriteError;
    }

    // reposition to old position if was > to current position
    if (ostream.tellp() < original_position)
    {
        ostream.seekp(original_position);
    }

    // the offset of data will be right after the headers
    data_offset = sizeof(Header);
    return WavErrorCode::NoError;
}

WavErrorCode WavFile::read_header(ChunkList *headers)
{
    if (!istream.is_open())
    {
        return WavErrorCode::NotOpen;
    }
    istream.seekg(0, std::ios::end);
    auto file_size = istream.tellg();
    // If not enough data
    if (file_size < sizeof(Header))
    {
        return WavErrorCode::InvalidFormat;
    }
    istream.seekg(0, std::ios::beg);

    // read headers
    cast_header(istream, headers->riff(), &header.riff);
    cast_header(istream, headers->fmt(), &header.fmt);
    cast_header(istream, headers->data(), &header.data);
    // data offset is right after data header's ID and size
    auto data_header = headers->data();
    data_offset =
        data_header.position() + sizeof(data_header.chunk_size()) + (data_header.chunk_id().size() * sizeof(char));

    // check headers ids (make sure they are set)
    if (std::string(header.riff.chunk_id, 4) != "RIFF")
    {
        return WavErrorCode::InvalidFormat;
    }
    if (std::string(header.riff.format, 4) != "WAVE")
    {
        return WavErrorCode::InvalidFormat;
    }
    if (std::string(header.fmt.sub_chunk_1_id, 4) != "fmt ")
    {
        return WavErrorCode::InvalidFormat;
    }
    if (std::string(header.data.sub_chunk_2_id, 4) != "data")
    {
        return WavErrorCode::InvalidFormat;
    }

    // only support 16 bit per sample
    auto bps = header.fmt.bits_per_sample;
    if (bps != 16)
    {
        return WavErrorCode::InvalidFormat;
    }

    // And only support uncompressed PCM format
    if (header.fmt.audio_format != 1)
    {
        return WavErrorCode::InvalidFormat;
    }

    return WavErrorCode::NoError;
}