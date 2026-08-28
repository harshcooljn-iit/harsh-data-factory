// sleeper -- sleeps for a fixed duration.
//
//   sleeper <ms> [--ignore-term]
//
// With --ignore-term it installs a SIG_IGN handler for SIGTERM so a
// well-behaved terminate is not enough to stop it -- the runner must escalate
// to SIGKILL. Used by timeout / cancellation tests.
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: sleeper <ms> [--ignore-term]\n";
        return 2;
    }
    const long ms = std::strtol(argv[1], nullptr, 10);
    bool ignore_term = false;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ignore-term") == 0) {
            ignore_term = true;
        }
    }
    if (ignore_term) {
        std::signal(SIGTERM, SIG_IGN);
    }
    std::cout << "sleeping " << ms << "ms\n";
    std::cout.flush();

    // Sleep in small slices so SIGKILL is observed promptly.
    long remaining = ms;
    while (remaining > 0) {
        const long slice = remaining < 50 ? remaining : 50;
        std::this_thread::sleep_for(std::chrono::milliseconds(slice));
        remaining -= slice;
    }
    std::cout << "done\n";
    return 0;
}
