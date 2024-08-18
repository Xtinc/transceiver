#include "protocol.h"
#include "audiostream.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <cassert>
#include "audiowave.h"

#define ASSERT_EQ(A, B) assert(A == B)
#define ASSERT_LT(A, B) assert(A < B)

void WaveRead()
{

    WavFile read_file;
    read_file.Open("/Untitled3.wav", OpenMode::kIn);

    ASSERT_EQ(read_file.sample_rate(), 44100);
    ASSERT_EQ(read_file.bits_per_sample(), 16);
    ASSERT_EQ(read_file.channel_number(), 2);

    std::vector<float> content;
    read_file.Read(&content);

    ASSERT_EQ(content.size() / read_file.channel_number(),
              5.558344671201814 * 44100);
}

void WaveChunkRead()
{
    // tested above
    WavFile read_file;
    read_file.Open("/Untitled3.wav", OpenMode::kIn);
    std::vector<float> content;
    read_file.Read(&content);

    WavFile chunk_read_file;
    chunk_read_file.Open("/Untitled3.wav", OpenMode::kIn);
    ASSERT_EQ(content.size() / read_file.channel_number(),
              chunk_read_file.frame_number());

    // Read in two parts
    std::vector<float> p1_content, p2_content;
    const uint64_t kFirstPartSize = 1000;
    auto err = chunk_read_file.Read(kFirstPartSize, &p1_content);
    ASSERT_EQ(err, Error::kNoError);
    err = chunk_read_file.Read(chunk_read_file.frame_number() - kFirstPartSize,
                               &p2_content);
    ASSERT_EQ(err, Error::kNoError);

    // check size
    ASSERT_EQ(content.size(), p1_content.size() + p2_content.size());

    // check if content is the same
    std::vector<float> chunk_content(p1_content.size() + p2_content.size());
    memcpy(chunk_content.data(), p1_content.data(),
           p1_content.size() * sizeof(float));
    memcpy(chunk_content.data() + p1_content.size(), p2_content.data(),
           p2_content.size() * sizeof(float));

    ASSERT_EQ(chunk_content, content);
}

void WaveWrite()
{
    // tested above
    WavFile read_file;
    read_file.Open("/Untitled3.wav", OpenMode::kIn);
    std::vector<float> content;
    read_file.Read(&content);

    {
        WavFile write_file;
        write_file.Open("/output.wav", OpenMode::kOut);
        write_file.set_sample_rate(read_file.sample_rate());
        write_file.set_bits_per_sample(read_file.bits_per_sample());
        write_file.set_channel_number(read_file.channel_number());
        write_file.Write(content);
    }

    // re read
    WavFile re_read_file;
    re_read_file.Open("/output.wav", OpenMode::kIn);
    std::vector<float> re_read_content;
    re_read_file.Read(&re_read_content);

    ASSERT_EQ(read_file.channel_number(), re_read_file.channel_number());
    ASSERT_EQ(read_file.sample_rate(), re_read_file.sample_rate());
    ASSERT_EQ(read_file.channel_number(), re_read_file.channel_number());
    ASSERT_EQ(read_file.bits_per_sample(), re_read_file.bits_per_sample());
    ASSERT_EQ(content, re_read_content);
}

void WaveChunkWrite()
{

    // tested above
    WavFile read_file;
    read_file.Open("/Untitled3.wav", OpenMode::kIn);
    std::vector<float> content;
    read_file.Read(&content);

    // write per chunk
    {
        WavFile write_file;
        write_file.Open("/output.wav", OpenMode::kOut);
        write_file.set_sample_rate(read_file.sample_rate());
        write_file.set_bits_per_sample(read_file.bits_per_sample());
        write_file.set_channel_number(read_file.channel_number());

        const uint64_t kChunkSize = 1000;
        std::vector<float> frames(kChunkSize * read_file.channel_number());
        uint64_t written_samples = 0;
        while (written_samples < content.size())
        {
            if (content.size() - written_samples < kChunkSize)
            {
                frames.resize(content.size() - written_samples);
            }
            memcpy(frames.data(), content.data() + written_samples,
                   frames.size() * sizeof(float));
            ASSERT_EQ(write_file.Write(frames), Error::kNoError);
            written_samples += frames.size();
        }
    }

    // re read
    WavFile re_read_file;
    re_read_file.Open("/output.wav", OpenMode::kIn);
    std::vector<float> re_read_content;
    re_read_file.Read(&re_read_content);

    ASSERT_EQ(read_file.channel_number(), re_read_file.channel_number());
    ASSERT_EQ(read_file.sample_rate(), re_read_file.sample_rate());
    ASSERT_EQ(read_file.channel_number(), re_read_file.channel_number());
    ASSERT_EQ(read_file.bits_per_sample(), re_read_file.bits_per_sample());
    ASSERT_EQ(content, re_read_content);
}

void WaveWrite24bits()
{
    // tested above
    WavFile read_file;
    read_file.Open("/Untitled3.wav", OpenMode::kIn);
    std::vector<float> content;
    read_file.Read(&content);

    {
        WavFile write_file;
        write_file.Open("/output.wav", OpenMode::kOut);
        write_file.set_sample_rate(read_file.sample_rate());
        write_file.set_bits_per_sample(24);
        write_file.set_channel_number(read_file.channel_number());
        write_file.Write(content);
    }

    // re read
    WavFile re_read_file;
    re_read_file.Open("/output.wav", OpenMode::kIn);
    std::vector<float> re_read_content;
    re_read_file.Read(&re_read_content);

    ASSERT_EQ(read_file.channel_number(), re_read_file.channel_number());
    ASSERT_EQ(read_file.sample_rate(), re_read_file.sample_rate());
    ASSERT_EQ(read_file.channel_number(), re_read_file.channel_number());
    ASSERT_EQ(re_read_file.bits_per_sample(), 24);
    for (int i = 0; i < read_file.frame_number(); i++)
    {
        ASSERT_LT(fabs(content[i] - re_read_content[i]), pow(10, -6));
    }
}

void WaveSeekIn()
{
    WavFile read_file;
    read_file.Open("/Untitled3.wav", OpenMode::kIn);
    std::vector<float> reference;
    read_file.Read(&reference);

    WavFile read_seek;
    read_seek.Open("/Untitled3.wav", OpenMode::kIn);
    std::vector<float> p1;
    read_seek.Read(20, &p1);
    ASSERT_EQ(read_seek.Tell(), 20);

    // check p1 is 20 first frames of reference
    for (size_t idx = 0; idx < p1.size(); idx++)
    {
        ASSERT_EQ(p1[idx], reference[idx]);
    }

    uint64_t first_frame_idx = 10;
    read_seek.Seek(first_frame_idx);
    ASSERT_EQ(read_seek.Tell(), first_frame_idx);
    std::vector<float> p2;
    read_seek.Read(20, &p2);
    ASSERT_EQ(read_seek.Tell(), 30);
    // check that p2 is frame 10 to 30 of reference
    auto first_sample_idx = first_frame_idx * read_file.channel_number();
    for (size_t idx = 0; idx < p2.size(); idx++)
    {
        ASSERT_EQ(p2[idx], reference[idx + first_sample_idx]);
    }
}

void WaveSeekOut()
{

    WavFile read_file;
    read_file.Open("/Untitled3.wav", OpenMode::kIn);
    std::vector<float> p1, p2;
    // read first 20
    read_file.Read(20, &p1);
    ASSERT_EQ(read_file.Tell(), 20);
    // read another 20 frames
    read_file.Read(20, &p2);
    ASSERT_EQ(read_file.Tell(), 40);

    {
        WavFile write_file;
        write_file.Open("/output.wav", OpenMode::kOut);
        write_file.set_sample_rate(read_file.sample_rate());
        write_file.set_bits_per_sample(read_file.bits_per_sample());
        write_file.set_channel_number(read_file.channel_number());
        // write
        write_file.Write(p1);
        ASSERT_EQ(write_file.Tell(), 20);
        // seek back 10 frames
        write_file.Seek(10);
        ASSERT_EQ(write_file.Tell(), 10);
        // write another 20
        write_file.Write(p2);
        ASSERT_EQ(write_file.Tell(), 30);
    }

    // re read file
    WavFile re_read_file;
    re_read_file.Open("/output.wav", OpenMode::kIn);
    std::vector<float> re_read_content;
    re_read_file.Read(&re_read_content);

    // check content
    for (size_t idx = 0; idx < re_read_content.size(); idx++)
    {
        if (idx < 10 * read_file.channel_number())
        {
            ASSERT_EQ(p1[idx], re_read_content[idx]);
        }
        else
        {
            ASSERT_EQ(p2[idx - 10 * read_file.channel_number()],
                      re_read_content[idx]);
        }
    }
}

void WaveExtraHeaders()
{
    WavFile file;
    ASSERT_EQ(file.Open("/extra-header.wav", OpenMode::kIn), Error::kNoError);

    std::error_code err;
    std::vector<float> data;
    ASSERT_EQ(file.Read(&data), Error::kNoError);
    WavFile write_file;
    write_file.Open("/extra-header-out.wav", OpenMode::kOut);
    write_file.set_channel_number(file.channel_number());
    write_file.set_sample_rate(file.sample_rate());
    write_file.Write(data);
}

int main(int, char **)
{
    WaveRead();
    WaveChunkRead();
    WaveWrite();
    WaveChunkWrite();
    WaveWrite24bits();
    WaveSeekIn();
    WaveSeekOut();
    WaveExtraHeaders();
    return 0;
}
