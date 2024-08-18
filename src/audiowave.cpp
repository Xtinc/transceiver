#include "audiowave.h"
#define INT24_MAX 8388607

class Header
{
public:
    Error Init(std::ifstream *stream, uint64_t position)
    {
        position_ = position;
        if (!stream->is_open())
        {
            return Error::kNotOpen;
        }

        // read chunk ID
        const auto chunk_id_size = 4;
        stream->seekg(position_, std::ios::beg);
        char result[chunk_id_size];
        stream->read(result, chunk_id_size * sizeof(char));
        id_ = std::string(result, chunk_id_size);

        // and size
        stream->read(reinterpret_cast<char *>(&size_), sizeof(uint32_t));
        size_ += chunk_id_size * sizeof(char) + sizeof(uint32_t);

        return Error::kNoError;
    }
    std::string chunk_id() const
    {
        return id_;
    }
    uint32_t chunk_size() const
    {
        if (chunk_id() == "RIFF")
        {
            return sizeof(WavFile::RIFFChunk);
        }
        return size_;
    }
    uint64_t position() const
    {
        return position_;
    }

private:
    std::string id_;
    uint32_t size_;
    uint64_t position_;
};

class HeaderList
{
public:
    class Iterator
    {
    public:
        Iterator(std::ifstream *stream, uint64_t position)
            : stream_(stream), position_(position) {}
        Iterator operator++()
        {
            Header h;
            h.Init(stream_, position_);
            position_ += h.chunk_size();
            return *this;
        }
        Iterator operator++(int)
        {
            Iterator next = *this;
            operator++();
            return next;
        }
        Header operator*()
        {
            Header h;
            h.Init(stream_, position_);
            return h;
        }
        bool operator==(const Iterator &rhs)
        {
            return rhs.stream_ == stream_ && rhs.position_ == position_;
        }
        bool operator!=(const Iterator &rhs)
        {
            return !operator==(rhs);
        }

    private:
        std::ifstream *stream_;
        uint64_t position_;
    };

    Error Init(const std::string &path)
    {
        stream_.open(path.c_str(), std::ios::binary);
        if (!stream_.is_open())
        {
            return Error::kFailedToOpen;
        }
        return Error::kNoError;
    }

    Iterator begin()
    {
        return Iterator(&stream_, 0);
    }

    Iterator end()
    {
        stream_.seekg(0, std::ios::end);
        uint64_t size = stream_.tellg();
        stream_.seekg(0, std::ios::beg);
        return Iterator(&stream_, size);
    }

    Header riff() { return header("RIFF"); }
    Header fmt() { return header("fmt "); }
    Header data() { return header("data"); }

private:
    Header header(const std::string &header_id)
    {
        for (auto iterator = begin(); iterator != end(); iterator++)
        {
            auto header = *iterator;
            if (header.chunk_id() == header_id)
            {
                return header;
            }
        }
        return *begin();
    }

private:
    std::ifstream stream_;
};

template <typename T>
void cast_header(std::ifstream &ifs, Header generic_header, T *output)
{
    ifs.seekg(generic_header.position(), std::ios::beg);
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

Error WavFile::Open(const std::string &path, OpenMode mode)
{
    if (mode == OpenMode::kOut)
    {
        ostream.open(path.c_str(), std::ios::binary | std::ios::trunc);
        if (!ostream.is_open())
        {
            return Error::kFailedToOpen;
        }
        return WriteHeader(0);
    }
    istream.open(path.c_str(), std::ios::binary);
    if (!istream.is_open())
    {
        return Error::kFailedToOpen;
    }
    HeaderList headers;
    auto error = headers.Init(path);
    if (error != Error::kNoError)
    {
        return error;
    }
    return ReadHeader(&headers);
}

Error WavFile::Read(std::vector<float> *output)
{
    return Read(frame_number(), output);
}

Error WavFile::Read(uint64_t frame_number, std::vector<float> *output)
{
    if (!istream.is_open())
    {
        return Error::kNotOpen;
    }
    auto requested_samples = frame_number * channel_number();

    // check if we have enough data available
    if (sample_number() <
        requested_samples + current_sample_index())
    {
        return Error::kInvalidFormat;
    }
    // resize output to desired size
    output->resize(requested_samples);

    // read every sample one after another
    for (size_t sample_idx = 0; sample_idx < output->size(); sample_idx++)
    {
        if (header.fmt.bits_per_sample == 8)
        {
            // 8bits case
            int8_t value;
            istream.read(reinterpret_cast<char *>(&value), sizeof(value));
            (*output)[sample_idx] =
                static_cast<float>(value) / std::numeric_limits<int8_t>::max();
        }
        else if (header.fmt.bits_per_sample == 16)
        {
            // 16 bits
            int16_t value;
            istream.read(reinterpret_cast<char *>(&value), sizeof(value));
            (*output)[sample_idx] =
                static_cast<float>(value) / std::numeric_limits<int16_t>::max();
        }
        else if (header.fmt.bits_per_sample == 24)
        {
            // 24bits int doesn't exist in c++. We create a 3 * 8bits struct to
            // simulate
            unsigned char value[3];
            istream.read(reinterpret_cast<char *>(&value), sizeof(value));
            int integer_value;
            // check if value is negative
            if (value[2] & 0x80)
            {
                integer_value =
                    (0xff << 24) | (value[2] << 16) | (value[1] << 8) | (value[0] << 0);
            }
            else
            {
                integer_value = (value[2] << 16) | (value[1] << 8) | (value[0] << 0);
            }
            (*output)[sample_idx] = static_cast<float>(integer_value) / INT24_MAX;
        }
        else if (header.fmt.bits_per_sample == 32)
        {
            // 32bits
            int32_t value;
            istream.read(reinterpret_cast<char *>(&value), sizeof(value));
            (*output)[sample_idx] =
                static_cast<float>(value) / std::numeric_limits<int32_t>::max();
        }
        else
        {
            return Error::kInvalidFormat;
        }
    }
    return Error::kNoError;
}

Error WavFile::Write(const std::vector<float> &data, bool clip)
{
    if (!ostream.is_open())
    {
        return Error::kNotOpen;
    }

    auto current_data_size = current_sample_index();
    auto bits_per_sample = header.fmt.bits_per_sample;

    // write each samples
    for (auto sample : data)
    {
        // hard-clip if asked
        if (clip)
        {
            if (sample > 1.f)
            {
                sample = 1.f;
            }
            else if (sample < -1.f)
            {
                sample = -1.f;
            }
        }
        if (bits_per_sample == 8)
        {
            // 8bits case
            int8_t value =
                static_cast<int8_t>(sample * std::numeric_limits<int8_t>::max());
            ostream.write(reinterpret_cast<char *>(&value), sizeof(value));
        }
        else if (bits_per_sample == 16)
        {
            // 16 bits
            int16_t value =
                static_cast<int16_t>(sample * std::numeric_limits<int16_t>::max());
            ostream.write(reinterpret_cast<char *>(&value), sizeof(value));
        }
        else if (bits_per_sample == 24)
        {
            // 24bits int doesn't exist in c++. We create a 3 * 8bits struct to
            // simulate
            int v = sample * INT24_MAX;
            int8_t value[3];
            value[0] = reinterpret_cast<char *>(&v)[0];
            value[1] = reinterpret_cast<char *>(&v)[1];
            value[2] = reinterpret_cast<char *>(&v)[2];

            ostream.write(reinterpret_cast<char *>(&value), sizeof(value));
        }
        else if (bits_per_sample == 32)
        {
            // 32bits
            int32_t value =
                static_cast<int32_t>(sample * std::numeric_limits<int32_t>::max());
            ostream.write(reinterpret_cast<char *>(&value), sizeof(value));
        }
        else
        {
            return Error::kInvalidFormat;
        }
    }

    // update header to show the right data size
    WriteHeader(current_data_size + data.size());

    return Error::kNoError;
}

Error WavFile::Seek(uint64_t frame_index)
{
    if (!ostream.is_open() && !istream.is_open())
    {
        return Error::kNotOpen;
    }
    if (frame_index > frame_number())
    {
        return Error::kInvalidSeek;
    }

    set_current_sample_index(frame_index * channel_number());
    return Error::kNoError;
}

uint64_t WavFile::Tell() const
{
    if (!ostream.is_open() && !istream.is_open())
    {
        return 0;
    }

    auto sample_position = current_sample_index();
    return sample_position / channel_number();
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

Error WavFile::WriteHeader(uint64_t data_size)
{
    if (!ostream.is_open())
    {
        return Error::kNotOpen;
    }
    auto original_position = ostream.tellp();
    // Position to beginning of file
    ostream.seekp(0);

    // make header
    auto bits_per_sample = header.fmt.bits_per_sample;
    auto bytes_per_sample = bits_per_sample / 8;
    auto channel_number = header.fmt.num_channel;
    auto sample_rate = header.fmt.sample_rate;

    header.riff.chunk_size =
        sizeof(Header) + (data_size * (bits_per_sample / 8)) - 8;
    // fmt header
    header.fmt.byte_per_block = bytes_per_sample * channel_number;
    header.fmt.byte_rate = sample_rate * header.fmt.byte_per_block;
    // data header
    header.data.sub_chunk_2_size = data_size * bytes_per_sample;

    ostream.write(reinterpret_cast<char *>(&header), sizeof(Header));
    if (ostream.fail())
    {
        return Error::kWriteError;
    }

    // reposition to old position if was > to current position
    if (ostream.tellp() < original_position)
    {
        ostream.seekp(original_position);
    }

    // the offset of data will be right after the headers
    data_offset_ = sizeof(Header);
    return Error::kNoError;
}

Error WavFile::ReadHeader(HeaderList *headers)
{
    if (!istream.is_open())
    {
        return Error::kNotOpen;
    }
    istream.seekg(0, std::ios::end);
    auto file_size = istream.tellg();
    // If not enough data
    if (file_size < sizeof(Header))
    {
        return Error::kInvalidFormat;
    }
    istream.seekg(0, std::ios::beg);

    // read headers
    cast_header(istream, headers->riff(), &header.riff);
    cast_header(istream, headers->fmt(), &header.fmt);
    cast_header(istream, headers->data(), &header.data);
    // data offset is right after data header's ID and size
    auto data_header = headers->data();
    data_offset_ = data_header.position() + sizeof(data_header.chunk_size()) + (data_header.chunk_id().size() * sizeof(char));

    // check headers ids (make sure they are set)
    if (std::string(header.riff.chunk_id, 4) != "RIFF")
    {
        return Error::kInvalidFormat;
    }
    if (std::string(header.riff.format, 4) != "WAVE")
    {
        return Error::kInvalidFormat;
    }
    if (std::string(header.fmt.sub_chunk_1_id, 4) != "fmt ")
    {
        return Error::kInvalidFormat;
    }
    if (std::string(header.data.sub_chunk_2_id, 4) != "data")
    {
        return Error::kInvalidFormat;
    }

    // we only support 8 / 16 / 32  bit per sample
    auto bps = header.fmt.bits_per_sample;
    if (bps != 8 && bps != 16 && bps != 32)
    {
        return Error::kInvalidFormat;
    }

    // And only support uncompressed PCM format
    if (header.fmt.audio_format != 1)
    {
        return Error::kInvalidFormat;
    }

    return Error::kNoError;
}