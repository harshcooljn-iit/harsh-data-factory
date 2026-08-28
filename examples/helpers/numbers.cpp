// numbers -- deterministic data generator for the cpp_pipeline example.
//   numbers <output> <count>
// Writes <count> integers (a fixed pseudo-random sequence) one per line.
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: numbers <output> <count>\n";
        return 2;
    }
    std::ofstream out(argv[1]);
    if (!out) {
        std::cerr << "numbers: cannot write " << argv[1] << "\n";
        return 1;
    }
    const long count = std::strtol(argv[2], nullptr, 10);
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    for (long i = 0; i < count; ++i) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        out << (state % 100) << "\n";
    }
    std::cerr << "numbers: wrote " << count << " values\n";
    return 0;
}
