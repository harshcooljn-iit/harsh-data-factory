# ---------------------------------------------------------------------------
# Example pipelines.
#
# Each examples/<name>/ ships a pipeline.json.in template plus any data or
# scripts it needs. At configure time the template is expanded into
# ${CMAKE_BINARY_DIR}/examples/<name>/pipeline.json with absolute paths, and
# the supporting files are copied next to it so the example runs from a clean
# build with:
#
#   ./build/<preset>/bin/flowforge run build/<preset>/examples/<name>/pipeline.json
#
# scripts/run-examples.sh runs them all.
# ---------------------------------------------------------------------------

set(FF_EX_BIN "${CMAKE_BINARY_DIR}/examples/bin")
file(MAKE_DIRECTORY "${FF_EX_BIN}")

foreach(helper greeter numbers histogram flaky_counter)
    add_executable(ex_${helper} helpers/${helper}.cpp)
    set_target_properties(ex_${helper} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FF_EX_BIN}"
        OUTPUT_NAME "ex_${helper}")
    target_compile_features(ex_${helper} PRIVATE cxx_std_20)
endforeach()

find_program(FF_PYTHON3 NAMES python3 python)
if(NOT FF_PYTHON3)
    set(FF_PYTHON3 "python3")
endif()

# Expand one example: configure the pipeline and stage its extra files.
function(flowforge_example name)
    set(GEN "${CMAKE_BINARY_DIR}/examples/${name}")
    file(MAKE_DIRECTORY "${GEN}")

    set(FF_BIN "${FF_EX_BIN}")
    set(GEN "${GEN}")
    set(FF_PYTHON "${FF_PYTHON3}")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/${name}/pipeline.json.in"
        "${CMAKE_BINARY_DIR}/examples/${name}/pipeline.json"
        @ONLY)

    # Stage any non-template files (scripts, data) next to the pipeline.
    file(GLOB extras
        "${CMAKE_CURRENT_SOURCE_DIR}/${name}/*.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/${name}/*.csv")
    if(extras)
        file(COPY ${extras} DESTINATION "${CMAKE_BINARY_DIR}/examples/${name}")
    endif()
endfunction()

foreach(ex hello python_pipeline cpp_pipeline parallel_pipeline
           failure_pipeline retry_pipeline cache_pipeline)
    flowforge_example(${ex})
endforeach()

# cache_pipeline needs a small input file it can hash and later mutate.
file(WRITE "${CMAKE_BINARY_DIR}/examples/cache_pipeline/data.in" "cache example input v1\n")

add_custom_target(flowforge_examples DEPENDS
    ex_greeter ex_numbers ex_histogram ex_flaky_counter)
