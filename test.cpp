#include <fstream>
#include <vector>
#include <string>

#include "src/audio_process.h"

int main(int argc, char **argv)
{
    std::vector<double> data_in, data_out;
    std::ifstream ifs("test_signals.txt");
    std::string line;

    while (std::getline(ifs, line))
    {
        data_in.emplace_back(std::stod(line));
    }

    int order = 64;
    double rt = 480.0 / 441.0;
    double cutoff = 0.91;
    int fs = 48000;
    int precision = 10000;

    int n_input = data_in.size();
    int n_output = int(n_input / rt) + 1;
    data_out.resize(n_output);

    SincInterpolator SSR(order, precision, cutoff, rt);
    for (size_t i = 0; i < n_input / 480; i++)
    {
        SSR(data_in.data() + i * 480, 480, data_out.data() + i * 441, 441, 1);
    }

    std::ofstream ofs("results.txt");
    for (auto i : data_out)
    {
        ofs << i << "\n";
    }
    return 0;
}