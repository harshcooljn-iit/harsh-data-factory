# Integration tests spawn real child processes. They depend on the helper
# programs in tests/helpers being built first.
add_subdirectory(${CMAKE_SOURCE_DIR}/tests/helpers ${CMAKE_BINARY_DIR}/tests/helpers)

flowforge_add_test(NAME process_runner_test
    SOURCES process_runner_test.cpp
    LABELS integration)
add_dependencies(process_runner_test flowforge_helpers)
