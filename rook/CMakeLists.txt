# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: MIT

# The Rook toolchain (docs/14-rook-language.md). It configures and builds on its own, with a compiler
# and nothing else (`cmake -S rook -B <folder> -G Ninja`), and it imports nothing of engine/ or game/:
# the repository's build adds it with add_subdirectory and refuses such an edge
# (docs/adr/0033-rook-compiler-and-types.md, cmake/nxLayers.cmake).
cmake_minimum_required(VERSION 4.3)

project(Rook
    VERSION 0.1.0
    DESCRIPTION "Toolchain of the Rook plugin language"
    LANGUAGES CXX)

option(ROOK_BUILD_TESTS "Build the conformance suite and, where GoogleTest is found, the unit tests" ON)
option(ROOK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(ROOK_FUZZ "Link the fuzz targets with libFuzzer; off, they replay their corpus" OFF)
set(ROOK_FUZZ_SECONDS 60 CACHE STRING "How long each fuzz target runs with ROOK_FUZZ")
set(ROOK_FUZZ_INPUT_SECONDS 30 CACHE STRING "How long one input may run before libFuzzer reports it as a hang")

set(ROOK_SOURCE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(rookTargets)

if(PROJECT_IS_TOP_LEVEL)
    include(rookCompilerSettings)
    include(rookStdModule)
    rookAddStdModule(rookStd)
    add_library(rook::std ALIAS rookStd)
elseif(NOT TARGET rook::std)
    message(FATAL_ERROR "rook: the build that adds this directory names its std module rook::std")
endif()

if(ROOK_BUILD_TESTS OR ROOK_FUZZ)
    enable_testing()
endif()
if(ROOK_BUILD_TESTS)
    if(NOT TARGET GTest::gtest)
        find_package(GTest CONFIG QUIET)
    endif()
    include(GoogleTest)
endif()

add_subdirectory(compiler)
if(ROOK_BUILD_TESTS)
    add_subdirectory(spec)
endif()
