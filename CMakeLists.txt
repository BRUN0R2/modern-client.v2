# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 4.3)

# Declared before project() because the vcpkg toolchain installs manifest features there.
option(NX_BUILD_TESTS "Build the unit tests (installs the vcpkg 'tests' feature)" ON)
option(NX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

if(NX_BUILD_TESTS)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
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

if(NX_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)
    find_package(xxHash CONFIG REQUIRED) # reference implementation for nx::hash64 tests
    include(GoogleTest)
endif()

add_subdirectory(engine)
