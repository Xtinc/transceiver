#include <fstream>
#include <vector>
#include <string>

#include "src/audio_process.h"

int main(int argc, char **argv)
{
    std::vector<int16_t> data_in, data_out;
    std::ifstream ifs("signals.txt");
    std::string line;

    while (std::getline(ifs, line))
    {
        data_in.emplace_back(static_cast<int16_t>(std::stoi(line)));
    }
    data_out.resize(data_in.size() / 3);

    double *buffer = new double[data_in.size()];
    int32_t st[8]{};

    // decimator_2(data_in.data(), data_in.size(), data_out.data());
    decimator_3(data_in.data(), data_in.size(), data_out.data(), st, buffer);

    std::ofstream ofs("results.txt");
    for (auto i : data_out)
    {
        ofs << i << "\n";
    }
    delete[] buffer;
    return 0;
}