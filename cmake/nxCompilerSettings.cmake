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
        "(see the toolchain matrix in docs/17-build-e-ferramentas.md)")
endif()

set(CMAKE_CXX_STANDARD ${NX_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Output layout ------------------------------------------------------------------------
# Archives go to lib/ on purpose: CMake names the synthesized standard library module target
# "@cmake_cxx_std", and link.exe reads any argument that starts with '@' as a response file,
# so "@cmake_cxx_std.lib" would never be linked. A directory prefix keeps it a plain path.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# --- ABI-affecting flags (global) ---------------------------------------------------------
# These must be identical for every translation unit, including the std module CMake builds
# for us, so they live in CMAKE_CXX_FLAGS instead of per-target options. Policy:
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
