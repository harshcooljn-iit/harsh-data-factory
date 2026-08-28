// flaky -- fails a fixed number of times, then succeeds.
//
//   flaky <counter-file> <succeed-on-attempt>
//
// Reads an integer from <counter-file> (0 if absent), increments it, writes it
// back, and exits 0 iff the new value >= <succeed-on-attempt>. Otherwise exits
// 1. Deterministic across a run so retry behaviour can be asserted exactly.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: flaky <counter-file> <succeed-on-attempt>\n";
        return 2;
    }
    const std::string path = argv[1];
    const long threshold = std::strtol(argv[2], nullptr, 10);

    long attempt = 0;
    {
        std::ifstream in(path);
        if (in) {
            in >> attempt;
        }
    }
    ++attempt;
    {
        std::ofstream out(path, std::ios::trunc);
        out << attempt << "\n";
    }

    if (attempt >= threshold) {
        std::cout << "attempt " << attempt << ": success\n";
        return 0;
    }
    std::cerr << "attempt " << attempt << ": transient failure\n";
    return 1;
}
