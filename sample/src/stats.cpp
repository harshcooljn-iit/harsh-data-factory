// Reads a two-column CSV (region,amount); writes count/sum/min/max/mean.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: stats <input.csv> <output.txt>\n";
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) { std::cerr << "stats: cannot open " << argv[1] << "\n"; return 1; }

    std::string line;
    std::getline(in, line);                                 // header
    long count = 0, sum = 0;
    long lo = std::numeric_limits<long>::max();
    long hi = std::numeric_limits<long>::min();
    while (std::getline(in, line)) {
        const auto comma = line.find(',');
        if (comma == std::string::npos) continue;
        const long v = std::stol(line.substr(comma + 1));
        sum += v; ++count;
        lo = std::min(lo, v); hi = std::max(hi, v);
    }
    std::ofstream out(argv[2]);
    out << "count=" << count << "\nsum=" << sum << "\n";
    if (count > 0)
        out << "min=" << lo << "\nmax=" << hi << "\nmean="
        << (static_cast<double>(sum) / static_cast<double>(count)) << "\n";
    std::cout << "stats: " << count << " rows summarised\n";
    return 0;
}