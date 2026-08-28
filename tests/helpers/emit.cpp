// emit -- deterministic output generator for integration tests and examples.
//
// Options (all optional):
//   --stdout-lines N   print N lines to stdout   (default 0)
//   --stderr-lines N   print N lines to stderr   (default 0)
//   --prefix S         line prefix               (default "line")
//   --sleep-ms N       sleep N ms before exiting (default 0)
//   --exit N           exit code                 (default 0)
//   --write PATH       create PATH with the text "ok\n" before exiting
//   --no-newline       omit trailing newline on the very last stdout line
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    long stdout_lines = 0;
    long stderr_lines = 0;
    long sleep_ms = 0;
    int exit_code = 0;
    bool no_newline = false;
    std::string prefix = "line";
    std::string write_path;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](long& out) {
            if (i + 1 < argc) {
                out = std::strtol(argv[++i], nullptr, 10);
            }
        };
        if (arg == "--stdout-lines") {
            next(stdout_lines);
        } else if (arg == "--stderr-lines") {
            next(stderr_lines);
        } else if (arg == "--sleep-ms") {
            next(sleep_ms);
        } else if (arg == "--exit") {
            long v = 0;
            next(v);
            exit_code = static_cast<int>(v);
        } else if (arg == "--prefix") {
            if (i + 1 < argc) {
                prefix = argv[++i];
            }
        } else if (arg == "--write") {
            if (i + 1 < argc) {
                write_path = argv[++i];
            }
        } else if (arg == "--no-newline") {
            no_newline = true;
        } else {
            std::cerr << "emit: unknown argument '" << arg << "'\n";
            return 2;
        }
    }

    for (long i = 0; i < stderr_lines; ++i) {
        std::cerr << prefix << " err " << i << "\n";
    }
    for (long i = 0; i < stdout_lines; ++i) {
        std::cout << prefix << " out " << i;
        if (no_newline && i + 1 == stdout_lines) {
            std::cout.flush();
        } else {
            std::cout << "\n";
        }
    }
    std::cout.flush();
    std::cerr.flush();

    if (!write_path.empty()) {
        std::ofstream out(write_path, std::ios::binary);
        out << "ok\n";
    }

    if (sleep_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
    return exit_code;
}
