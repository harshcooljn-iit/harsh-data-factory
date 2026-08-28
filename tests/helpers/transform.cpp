// transform -- tiny deterministic data step for artifact / pipeline tests.
//
//   transform <input> <output> [--upper] [--fail-missing-input]
//
// Reads <input>, writes a transformed copy to <output>. Default transform is
// "reverse each line". With --upper it upper-cases instead. Exits non-zero if
// the input file does not exist.
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: transform <input> <output> [--upper]\n";
        return 2;
    }
    const std::string in_path = argv[1];
    const std::string out_path = argv[2];
    bool upper = false;
    for (int i = 3; i < argc; ++i) {
        if (std::string(argv[i]) == "--upper") {
            upper = true;
        }
    }

    std::ifstream in(in_path, std::ios::binary);
    if (!in) {
        std::cerr << "transform: cannot open input '" << in_path << "'\n";
        return 3;
    }
    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        std::cerr << "transform: cannot open output '" << out_path << "'\n";
        return 4;
    }

    std::string line;
    long count = 0;
    while (std::getline(in, line)) {
        if (upper) {
            std::transform(line.begin(), line.end(), line.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        } else {
            std::reverse(line.begin(), line.end());
        }
        out << line << "\n";
        ++count;
    }
    std::cerr << "transform: wrote " << count << " lines to " << out_path << "\n";
    return 0;
}
