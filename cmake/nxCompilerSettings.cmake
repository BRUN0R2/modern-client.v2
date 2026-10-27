# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# Project-wide compiler settings. Included right after project().

include_guard(GLOBAL)

# --- Language standard --------------------------------------------------------------------
# C++26 wherever CMake knows the switch. MSVC has no /std:c++26 yet: CMake maps C++23 to
# /std:c++latest for MSVC, which is where MSVC ships its C++26 features first.
if("cxx_std_26" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    set(NX_CXX_STANDARD 26)
elseif(MSVC)
    set(NX_CXX_STANDARD 23)
    message(STATUS "nx: MSVC has no C++26 switch yet, building with /std:c++latest")
else()
    message(FATAL_ERROR
        "nx: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} has no C++26 support "
        "(see the toolchain matrix in docs/17-build-and-tools.md)")
endif()

set(CMAKE_CXX_STANDARD ${NX_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Output layout ------------------------------------------------------------------------
# Libraries in lib/, programs in bin/: one runnable directory per build tree.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# --- ABI-affecting flags (global) ---------------------------------------------------------
# These must be identical for every translation unit, including the std module (nx::std,
# cmake/nxStdModule.cmake), so they live in CMAKE_CXX_FLAGS instead of per-target options. Policy:
#   * no exceptions and no RTTI (ADR-0005), matching the vcpkg triplets in cmake/triplets;
#   * deterministic floating point: no contraction, no fast-math (docs/07, section 6);
#   * strict conformance and UTF-8 sources.
if(MSVC)
    string(REGEX REPLACE "/EH[a-z]*" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "/GR( |$)" " " CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(APPEND CMAKE_CXX_FLAGS
        " /EHs-c- /D_HAS_EXCEPTIONS=0 /GR-"
        " /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor"
        " /fp:precise"
        " /external:anglebrackets /external:W0")
else()
    string(APPEND CMAKE_CXX_FLAGS " -fno-exceptions -fno-rtti -ffp-contract=off")
endif()

# --- Static reflection (docs/02, section 1.2) ---------------------------------------------
# nx.meta describes reflected types explicitly on every compiler. Where the compiler implements
# C++26 reflection (P2996: GCC 16 with -freflection), the native backend also checks each
# description against its type at compile time. auto picks native when the compiler has it;
# native fails the configuration when it does not. The flag is global: every module unit, the
# std module included, must be compiled in the same language dialect.
set(NX_REFLECTION_BACKEND "auto" CACHE STRING "Static reflection: auto, native (P2996) or fallback")
set_property(CACHE NX_REFLECTION_BACKEND PROPERTY STRINGS auto native fallback)
if(NOT NX_REFLECTION_BACKEND MATCHES "^(auto|native|fallback)$")
    message(FATAL_ERROR "nx: NX_REFLECTION_BACKEND must be auto, native or fallback, not '${NX_REFLECTION_BACKEND}'")
endif()

set(NX_NATIVE_REFLECTION OFF)
if(NOT NX_REFLECTION_BACKEND STREQUAL "fallback" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    include(CheckCXXSourceCompiles)
    include(CMakePushCheckState)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_FLAGS "-freflection")
    # Reflection values can only be compared during constant evaluation.
    check_cxx_source_compiles("#include <meta>
static_assert(^^int == ^^int);
int main() {}"
        nxCompilerHasNativeReflection)
    cmake_pop_check_state()
    set(NX_NATIVE_REFLECTION ${nxCompilerHasNativeReflection})
endif()

if(NX_NATIVE_REFLECTION)
    string(APPEND CMAKE_CXX_FLAGS " -freflection")
    message(STATUS "nx: static reflection backend: native (P2996)")
elseif(NX_REFLECTION_BACKEND STREQUAL "native")
    message(FATAL_ERROR
        "nx: NX_REFLECTION_BACKEND=native, but ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} "
        "does not implement C++26 reflection (see docs/17-build-and-tools.md, section 2.1)")
else()
    message(STATUS "nx: static reflection backend: fallback (descriptions as written)")
endif()
