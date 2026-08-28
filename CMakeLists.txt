cmake_minimum_required(VERSION 3.24)

# ---------------------------------------------------------------------------
# Version: single source of truth lives here and is propagated to C++ via a
# generated header (see include/flowforge/version.hpp.in).
# ---------------------------------------------------------------------------
project(FlowForge
    VERSION 0.1.0
    DESCRIPTION "Local-first DAG pipeline orchestration engine"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global project settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

# Keep runtime artifacts predictable for examples / manual testing.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(FLOWFORGE_BUILD_TESTS "Build the FlowForge test suite" ON)
option(FLOWFORGE_BUILD_EXAMPLES "Build example helper programs" ON)
option(FLOWFORGE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# ---------------------------------------------------------------------------
# Dependencies (provided by vcpkg manifest mode via the toolchain file)
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Warning interface library — attached per-target, never globally.
# ---------------------------------------------------------------------------
add_library(flowforge_warnings INTERFACE)
add_library(FlowForge::warnings ALIAS flowforge_warnings)
target_compile_options(flowforge_warnings INTERFACE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused
        -Woverloaded-virtual -Wdouble-promotion -Wformat=2>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->)
if(FLOWFORGE_WARNINGS_AS_ERRORS)
    target_compile_options(flowforge_warnings INTERFACE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/WX>)
endif()

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/cli)

if(FLOWFORGE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(FLOWFORGE_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install / packaging
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS flowforge flowforge_core
    EXPORT FlowForgeTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp")
install(FILES "${CMAKE_BINARY_DIR}/generated/flowforge/version.hpp"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/flowforge)

set(CPACK_PACKAGE_NAME "flowforge")
set(CPACK_PACKAGE_VENDOR "FlowForge contributors")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "TGZ")
include(CPack)
