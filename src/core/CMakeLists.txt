# ---------------------------------------------------------------------------
# flowforge_core — the engine library.
#
# The core library is intentionally free of any CLI / presentation concerns.
# It is consumed identically by the CLI, the unit tests and the integration
# tests. Source files are added subsystem-by-subsystem as they land.
# ---------------------------------------------------------------------------

configure_file(
    "${CMAKE_SOURCE_DIR}/include/flowforge/version.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/flowforge/version.hpp"
    @ONLY)

add_library(flowforge_core STATIC
    util/string_utils.cpp
    util/time_utils.cpp)

add_library(FlowForge::core ALIAS flowforge_core)

target_include_directories(flowforge_core
    PUBLIC
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_BINARY_DIR}/generated")

target_link_libraries(flowforge_core
    PUBLIC
        nlohmann_json::nlohmann_json
        Threads::Threads
    PRIVATE
        spdlog::spdlog
        unofficial::sqlite3::sqlite3
        FlowForge::warnings)

target_compile_features(flowforge_core PUBLIC cxx_std_20)
