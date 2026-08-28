#include <cstdio>
#include <cstring>

#include "flowforge/version.hpp"

// Minimal entry point. The real command dispatch is introduced alongside the
// engine; for the bootstrap milestone this proves the toolchain, the vcpkg
// dependency wiring and the generated version header all work end to end.
int main(int argc, char** argv) {
    const bool wants_version =
        argc >= 2 && (std::strcmp(argv[1], "version") == 0 ||
                      std::strcmp(argv[1], "--version") == 0 ||
                      std::strcmp(argv[1], "-V") == 0);

    if (wants_version || argc < 2) {
        std::printf("%s %s\n", flowforge::version::kName, flowforge::version::kString);
        return 0;
    }

    if (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0 ||
        std::strcmp(argv[1], "help") == 0) {
        std::printf(
            "FlowForge %s - local-first DAG pipeline orchestration engine\n\n"
            "usage: flowforge <command> [options]\n\n"
            "Commands are added as the engine is implemented. Run\n"
            "  flowforge version\n"
            "to print the version.\n",
            flowforge::version::kString);
        return 0;
    }

    std::fprintf(stderr, "flowforge: unknown command '%s'\n", argv[1]);
    return 2;
}
