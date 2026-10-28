# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

cmake_minimum_required(VERSION 4.3)

# Declared before project() because the vcpkg toolchain installs manifest features there.
option(NX_BUILD_TESTS "Build the unit tests (installs the vcpkg 'tests' feature)" ON)
option(NX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(NX_BUILD_BENCHMARKS "Build the benchmarks (installs the vcpkg 'benchmarks' feature)" ON)
option(NX_BUILD_CLIENT "Build the client, next (apps/client/)" ON)
option(NX_BUILD_TOOLS "Build the tools (tools/)" ON)
option(NX_FUZZ "Link the fuzz targets with libFuzzer (the fuzz presets); off, they replay their corpus" OFF)
set(NX_FUZZ_SECONDS 60 CACHE STRING "How long each fuzz target runs with NX_FUZZ (the nightly job runs longer)")
set(NX_FUZZ_INPUT_SECONDS 30 CACHE STRING "How long one input may run before libFuzzer reports it as a hang, keeping it")

if(NX_BUILD_TESTS)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()
if(NX_BUILD_BENCHMARKS)
    list(APPEND VCPKG_MANIFEST_FEATURES "benchmarks")
endif()

project(ModernClientV2
    VERSION 0.1.0
    DESCRIPTION "Open-source recreation of Counter-Strike 1.6 on a modern C++26 engine"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(nxCompilerSettings)
include(nxStdModule)
include(nxTargets)

# `import std;` for every project target (see cmake/nxStdModule.cmake).
nxAddStdModuleLibrary()

# Runtime dependencies (vcpkg.json; docs/17-build-and-tools.md, section 5).
find_package(tomlplusplus CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
# SDL3 marks its headers as not the system's (its SDL3Config.cmake), so that its macros would be held
# to the project's warnings where they expand: SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK casts in C's style.
# They are a third party's code, whose headers are the system's here as every other library's are.
set_property(TARGET SDL3::Headers PROPERTY SYSTEM TRUE)
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(Freetype REQUIRED)
find_package(RecastNavigation CONFIG REQUIRED)

if(NX_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)
    find_package(xxHash CONFIG REQUIRED) # reference implementation for nx::hash64 tests
    include(GoogleTest)
endif()

if(NX_BUILD_BENCHMARKS)
    find_package(benchmark CONFIG REQUIRED)
endif()

if(NX_FUZZ)
    enable_testing()
endif()

add_subdirectory(shaders)
add_subdirectory(engine)
add_subdirectory(game)
add_subdirectory(mods/zombies)
add_subdirectory(apps)

if(NX_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(NX_BUILD_BENCHMARKS)
    add_subdirectory(tests/bench)
endif()
if(NX_BUILD_TESTS OR NX_FUZZ)
    add_subdirectory(tests/fuzz)
endif()
