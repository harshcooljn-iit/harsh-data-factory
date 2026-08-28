# ---------------------------------------------------------------------------
# Test suite.
#
#   unit/         fast, no child processes, no filesystem side effects beyond
#                 temp dirs.
#   integration/  spawns real child processes / touches SQLite on disk.
#   performance/  benchmark-style tests, tagged so CI can run them separately.
#
# Helper programs used by the integration tests live in tests/helpers and are
# plain executables the engine shells out to.
# ---------------------------------------------------------------------------

add_library(flowforge_test_support INTERFACE)
target_include_directories(flowforge_test_support
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_link_libraries(flowforge_test_support INTERFACE
    FlowForge::core
    GTest::gtest
    GTest::gtest_main)

# Register a gtest executable built from the given sources.
# Usage: flowforge_add_test(NAME dag_test SOURCES unit/dag_test.cpp [LABELS unit])
function(flowforge_add_test)
    cmake_parse_arguments(FT "" "NAME" "SOURCES;LABELS;LIBS" ${ARGN})
    add_executable(${FT_NAME} ${FT_SOURCES})
    target_link_libraries(${FT_NAME} PRIVATE flowforge_test_support ${FT_LIBS})
    if(NOT FT_LABELS)
        set(FT_LABELS unit)
    endif()
    gtest_discover_tests(${FT_NAME}
        PROPERTIES LABELS "${FT_LABELS}"
        DISCOVERY_TIMEOUT 30)
endfunction()

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(performance)
