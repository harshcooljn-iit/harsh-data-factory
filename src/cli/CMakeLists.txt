# ---------------------------------------------------------------------------
# flowforge -- the command line front end.
#
# A thin presentation layer over FlowForge::core. No scheduling / execution
# logic lives here: the CLI and the tests drive the same engine library.
# ---------------------------------------------------------------------------

add_executable(flowforge
    main.cpp
    cli_app.cpp
    args.cpp
    console_reporter.cpp)

target_include_directories(flowforge PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(flowforge
    PRIVATE
        FlowForge::core
        spdlog::spdlog
        FlowForge::warnings)

set_target_properties(flowforge PROPERTIES OUTPUT_NAME "flowforge")
# Install rules for `flowforge` live in the top-level CMakeLists.
