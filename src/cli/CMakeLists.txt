# ---------------------------------------------------------------------------
# flowforge — the command line front end.
#
# This target is a thin presentation layer over FlowForge::core. It must not
# contain scheduling / execution logic: everything testable lives in the core
# library so the CLI and the tests exercise the same code paths. Sources are
# added as commands land.
# ---------------------------------------------------------------------------

add_executable(flowforge
    main.cpp)

target_include_directories(flowforge PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(flowforge
    PRIVATE
        FlowForge::core
        spdlog::spdlog
        FlowForge::warnings)

set_target_properties(flowforge PROPERTIES OUTPUT_NAME "flowforge")
