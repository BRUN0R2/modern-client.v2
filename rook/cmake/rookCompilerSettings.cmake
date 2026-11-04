# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: MIT

# The compiler settings of the toolchain when it builds on its own (rook/CMakeLists.txt). Inside the
# repository's build these come from there, so that one `std` module serves every target.

include_guard(GLOBAL)

# C++26 wherever CMake knows the switch. MSVC has no /std:c++26 yet: CMake maps C++23 to
# /std:c++latest for MSVC, which is where MSVC ships its C++26 features first.
if("cxx_std_26" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    set(CMAKE_CXX_STANDARD 26)
elseif(MSVC)
    set(CMAKE_CXX_STANDARD 23)
else()
    message(FATAL_ERROR
        "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} has no C++26 support "
        "(see the toolchain matrix in docs/17-build-and-tools.md)")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Libraries in lib/, programs in bin/.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ABI-affecting flags live in CMAKE_CXX_FLAGS, because they must be identical for every translation
# unit, the `std` module included: no exceptions and no RTTI (docs/adr/0005), UTF-8 sources and strict
# conformance.
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
