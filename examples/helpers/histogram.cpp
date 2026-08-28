// histogram -- cpp_pipeline example's second task.
//   histogram <input> <output>
// Reads integers (one per line), writes a bucketed count table.
#include <array>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: histogram <input> <output>\n";
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "histogram: cannot read " << argv[1] << "\n";
        return 1;
    }
    std::array<long, 10> buckets{};
    long value = 0;
    long total = 0;
    while (in >> value) {
        const int b = static_cast<int>(value / 10);
        if (b >= 0 && b < 10) {
            ++buckets[static_cast<std::size_t>(b)];
        }
        ++total;
    }
    std::ofstream out(argv[2]);
    out << "# histogram of " << total << " values\n";
    for (std::size_t i = 0; i < buckets.size(); ++i) {
        out << (i * 10) << "-" << (i * 10 + 9) << " : " << buckets[i] << "\n";
    }
    std::cout << "histogram: processed " << total << " values into 10 buckets\n";
    return 0;
}
