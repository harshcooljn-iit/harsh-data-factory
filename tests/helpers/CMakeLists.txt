# Helper programs the integration tests (and some examples) shell out to.
# Built as plain executables into a single directory; their absolute paths are
# exported to tests through a generated header.

set(FLOWFORGE_HELPER_DIR "${CMAKE_BINARY_DIR}/tests/helpers/bin")
file(MAKE_DIRECTORY "${FLOWFORGE_HELPER_DIR}")

set(_helpers emit sleeper flaky transform)
foreach(h IN LISTS _helpers)
    add_executable(ff_helper_${h} ${CMAKE_CURRENT_SOURCE_DIR}/${h}.cpp)
    set_target_properties(ff_helper_${h} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FLOWFORGE_HELPER_DIR}"
        OUTPUT_NAME "${h}")
    target_compile_features(ff_helper_${h} PRIVATE cxx_std_20)
endforeach()

add_custom_target(flowforge_helpers DEPENDS
    ff_helper_emit ff_helper_sleeper ff_helper_flaky ff_helper_transform)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/helper_paths.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/flowforge_test/helper_paths.hpp"
    @ONLY)

# Make the generated header visible to the test-support interface library.
target_include_directories(flowforge_test_support
    INTERFACE "${CMAKE_BINARY_DIR}/generated")
