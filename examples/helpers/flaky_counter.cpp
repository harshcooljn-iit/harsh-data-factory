// flaky_counter -- retry_pipeline example's task.
//   flaky_counter <counter-file> <succeed-on-attempt>
// Increments a persisted counter; exits 0 once it reaches the threshold,
// exits 1 before that. Deterministic across a run so retries are observable.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: flaky_counter <counter-file> <succeed-on-attempt>\n";
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
        std::ofstream(path, std::ios::trunc) << attempt << "\n";
    }

    if (attempt >= threshold) {
        std::cout << "attempt " << attempt << ": succeeded\n";
        return 0;
    }
    std::cerr << "attempt " << attempt << ": transient failure, will retry\n";
    return 1;
}
