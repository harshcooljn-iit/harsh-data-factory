# Integration tests spawn real child processes. They depend on the helper
# programs in tests/helpers being built first.
add_subdirectory(${CMAKE_SOURCE_DIR}/tests/helpers ${CMAKE_BINARY_DIR}/tests/helpers)

flowforge_add_test(NAME process_runner_test
    SOURCES process_runner_test.cpp
    LABELS integration)
add_dependencies(process_runner_test flowforge_helpers)

flowforge_add_test(NAME engine_test
    SOURCES engine_test.cpp
    LABELS integration)
add_dependencies(engine_test flowforge_helpers)

flowforge_add_test(NAME cli_test
    SOURCES cli_test.cpp
    LABELS integration)
add_dependencies(cli_test flowforge)
target_compile_definitions(cli_test PRIVATE
    FLOWFORGE_CLI_BIN="$<TARGET_FILE:flowforge>")

if(TARGET flowforge_examples)
    flowforge_add_test(NAME examples_test
        SOURCES examples_test.cpp
        LABELS integration)
    add_dependencies(examples_test flowforge_examples)
    target_compile_definitions(examples_test PRIVATE
        FLOWFORGE_EXAMPLES_DIR="${CMAKE_BINARY_DIR}/examples")
endif()
