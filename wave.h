#ifndef AUDIO_WAVE_FILE_HEADER
#define AUDIO_WAVE_FILE_HEADER

#include <cstdint>
#include <fstream>
#include <vector>

#include <iostream>

class WAVFile
{
    struct alignas(4) Header
    {
        char riff[4] = {'R', 'I', 'F', 'F'};
        uint32_t chunkSize = 36;
        char wave[4] = {'W', 'A', 'V', 'E'};
        char fmt[4] = {'f', 'm', 't', ' '};
        uint32_t subchunk1Size = 16;
        uint16_t audioFormat = 3;
        uint16_t numChannels;
        uint32_t sampleRate;
        uint32_t byteRate;
        uint16_t blockAlign;
        uint16_t bitsPerSample;
        char data[4] = {'d', 'a', 't', 'a'};
        uint32_t subchunk2Size = 0;
    } header;

    std::vector<char> container;

public:
    WAVFile(uint16_t _numChannels, uint32_t _sampleRate, uint16_t _bitsPerSample)
    {
        header.numChannels = _numChannels;
        header.sampleRate = _sampleRate;
        header.bitsPerSample = _bitsPerSample;
        header.byteRate = _sampleRate * _numChannels * _bitsPerSample / 8;
        header.blockAlign = _numChannels * _bitsPerSample / 8;
        container.reserve(header.byteRate * 10);
    }

    size_t size() const
    {
        return container.size();
    }

    const char *data() const
    {
        return container.data();
    }

    void write(const char *data, size_t len)
    {
        container.insert(container.end(), data, data + len);
    }

    bool open(const std::string &filename)
    {
        std::ifstream inFile(filename, std::ios::binary);
        if (!inFile)
        {
            return false;
        }
        Header tmp_header;
        inFile.read(reinterpret_cast<char *>(&tmp_header), sizeof(Header));
        if (std::strncmp(header.riff, "RIFF", 4) != 0 || std::strncmp(header.wave, "WAVE", 4) != 0 ||
            std::strncmp(header.fmt, "fmt ", 4) != 0 || std::strncmp(header.data, "data", 4) != 0)
        {
            return false;
        }
        header = tmp_header;
        container.clear();
        container.resize(header.subchunk2Size);
        inFile.read(container.data(), header.subchunk2Size);
        return true;
    }

    bool save(const std::string &filename)
    {
        std::ofstream outFile(filename, std::ios::binary);
        if (!outFile)
        {
            return false;
        }
        header.subchunk2Size = container.size();
        header.chunkSize = 36 + header.subchunk2Size;
        outFile.write(reinterpret_cast<const char *>(&header), sizeof(Header));
        outFile.write(reinterpret_cast<const char *>(container.data()), header.subchunk2Size);
        return true;
    }
};

#endif