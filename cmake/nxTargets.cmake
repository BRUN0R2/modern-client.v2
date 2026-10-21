# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# Helpers that give every project target the same structure and warning policy.

include_guard(GLOBAL)

include(nxLayers)

# Applies the project warning policy (docs/02, section 12) to a target we own.
# Warnings are per-target so third-party code is never compiled with them.
function(nxTargetDefaults target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /w14242       # conversion with possible loss of data
            /w14254       # conversion between bit-fields of different widths
            /w14263       # member function hides a base class virtual function
            /w14265       # class with virtual functions has a non-virtual destructor
            /w14287       # unsigned/negative constant mismatch
            /we4289       # loop control variable used outside the loop
            /w14296       # expression is always true or always false
            /w14311       # pointer truncation
            /w14545 /w14546 /w14547 /w14549  # malformed or ineffective comma expressions
            /w14555       # expression has no effect
            /w14619       # unknown warning number in #pragma warning
            /w14640       # thread-unsafe construction of a local static
            /w14826       # sign-extending conversion
            /w14905 /w14906  # string literal cast to a different character type
            /w14928       # illegal copy-initialization
            /wd4324       # structure padded due to alignas: intentional (cache-line separation)
            $<$<BOOL:${NX_WARNINGS_AS_ERRORS}>:/WX>)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wconversion -Wsign-conversion -Wdouble-promotion
            -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual
            -Wold-style-cast -Wcast-align -Wnull-dereference
            -Wformat=2 -Wimplicit-fallthrough
            $<$<BOOL:${NX_WARNINGS_AS_ERRORS}>:-Werror>)
    endif()
endfunction()

# nxAddModuleLibrary(<target>
#     ALIAS   <namespace::name>
#     LAYER   <layer of docs/01-architecture.md, section 1.1>
#     MODULES <module interface units (.cppm), relative to src/>...
#     [SOURCES <implementation units (.cpp)>...]
#     [DEPENDS <targets>...]
#     [LATERAL <the DEPENDS of the same layer that docs/01 declares>...])
#
# Creates a static library whose public interface is a set of C++ named modules. Every
# project module may `import std;` (nx::std is linked publicly). The layer is checked against
# the layers of the project modules in DEPENDS once every module exists (cmake/nxLayers.cmake).
function(nxAddModuleLibrary target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "ALIAS;LAYER" "MODULES;SOURCES;DEPENDS;LATERAL")

    if(NOT arg_MODULES)
        message(FATAL_ERROR "nxAddModuleLibrary(${target}): MODULES is required")
    endif()
    if(NOT arg_ALIAS)
        message(FATAL_ERROR "nxAddModuleLibrary(${target}): ALIAS is required")
    endif()

    add_library(${target} STATIC)
    nxDeclareModuleLayer(${target} "${arg_LAYER}" DEPENDS ${arg_DEPENDS} LATERAL ${arg_LATERAL})
    target_sources(${target}
        PUBLIC
            FILE_SET CXX_MODULES
            BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
            FILES ${arg_MODULES})

    if(arg_SOURCES)
        target_sources(${target} PRIVATE ${arg_SOURCES})
    endif()
    target_link_libraries(${target} PUBLIC nx::std ${arg_DEPENDS})

    target_compile_features(${target} PUBLIC cxx_std_${NX_CXX_STANDARD})
    nxTargetDefaults(${target})
    add_library(${arg_ALIAS} ALIAS ${target})
endfunction()

# nxAddProgram(<target> SOURCES <files>... [DEPENDS <targets>...])
#
# Creates a program of the project (an application or a tool) in bin/, next to the data/ it reads:
# a copy of the repository's data/ that every build refreshes. With MSVC its manifest
# (cmake/nxProgram.manifest) makes UTF-8 the code page of its command line and of the Windows
# narrow-character functions, and lets it use paths longer than MAX_PATH where Windows allows them.
function(nxAddProgram target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;DEPENDS")

    if(NOT arg_SOURCES)
        message(FATAL_ERROR "nxAddProgram(${target}): SOURCES is required")
    endif()

    add_executable(${target} ${arg_SOURCES})
    if(MSVC)
        target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/cmake/nxProgram.manifest")
    endif()
    target_link_libraries(${target} PRIVATE nx::std ${arg_DEPENDS})
    nxTargetDefaults(${target})

    if(NOT TARGET nxProgramData)
        add_custom_target(nxProgramData
            COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
                "${PROJECT_SOURCE_DIR}/data" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data"
            COMMENT "Copying data/ next to the programs"
            VERBATIM)
    endif()
    add_dependencies(${target} nxProgramData)
endfunction()

# nxAddBenchmark(<target> SOURCES <files>... DEPENDS <targets>...)
#
# Creates a Google Benchmark executable. Benchmarks are not CTest tests: they are run by hand, on
# release builds (the nightly bench job is planned, docs/17-build-and-tools.md, section 7).
function(nxAddBenchmark target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;DEPENDS")

    if(NOT arg_SOURCES)
        message(FATAL_ERROR "nxAddBenchmark(${target}): SOURCES is required")
    endif()

    add_executable(${target} ${arg_SOURCES})
    target_link_libraries(${target} PRIVATE nx::std ${arg_DEPENDS} benchmark::benchmark benchmark::benchmark_main)
    nxTargetDefaults(${target})
endfunction()

# nxAddFuzzer(<target> SOURCES <files>... DEPENDS <targets>... CORPUS <directory>)
#
# Creates a fuzz target (tests/fuzz): SOURCES define nx::test::fuzz (tests/fuzz/fuzzTarget.hpp).
# Its CTest test depends on the build:
#   * with NX_FUZZ (the fuzz presets), libFuzzer fuzzes it for NX_FUZZ_SECONDS from CORPUS; new
#     inputs go to the build tree and the input of a crash to fuzzArtifacts/;
#   * otherwise it runs once on every file of CORPUS, so the seeds and every crash ever found stay
#     tests of every build.
function(nxAddFuzzer target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "CORPUS" "SOURCES;DEPENDS")

    if(NOT arg_SOURCES OR NOT arg_CORPUS)
        message(FATAL_ERROR "nxAddFuzzer(${target}): SOURCES and CORPUS are required")
    endif()

    set(fuzzDirectory "${PROJECT_SOURCE_DIR}/tests/fuzz")
    set(corpus "${CMAKE_CURRENT_SOURCE_DIR}/${arg_CORPUS}")
    add_executable(${target} ${arg_SOURCES})
    target_include_directories(${target} PRIVATE "${fuzzDirectory}")
    target_link_libraries(${target} PRIVATE nx::std ${arg_DEPENDS})
    nxTargetDefaults(${target})

    if(NX_FUZZ)
        # The preset instruments every unit; libFuzzer supplies main and calls the entry point.
        target_sources(${target} PRIVATE "${fuzzDirectory}/libFuzzerEntry.cpp")
        if(NOT MSVC)
            target_link_options(${target} PRIVATE -fsanitize=fuzzer)
        endif()
        set(workCorpus "${CMAKE_CURRENT_BINARY_DIR}/${target}Corpus")
        set(artifacts "${CMAKE_BINARY_DIR}/fuzzArtifacts")
        file(MAKE_DIRECTORY "${workCorpus}" "${artifacts}")
        add_test(NAME ${target}
            COMMAND ${target} "${workCorpus}" "${corpus}"
                -max_total_time=${NX_FUZZ_SECONDS} "-artifact_prefix=${artifacts}/")
        math(EXPR timeout "${NX_FUZZ_SECONDS} * 2 + 60") # libFuzzer stops itself; this is a backstop
        set_tests_properties(${target} PROPERTIES TIMEOUT ${timeout})
    else()
        target_sources(${target} PRIVATE "${fuzzDirectory}/corpusDriver.cpp")
        target_link_libraries(${target} PRIVATE nx::core nx::platform)
        add_test(NAME ${target}.corpus COMMAND ${target} "${corpus}")
    endif()
endfunction()

# nxAddTest(<target> SOURCES <files>... DEPENDS <targets>... [ENVIRONMENT <NAME=value>...])
#
# Creates a GoogleTest executable and registers each test case with CTest, run with ENVIRONMENT.
# The helpers shared by all tests (engine/testSupport, nx::testSupport) are always linked.
function(nxAddTest target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;DEPENDS;ENVIRONMENT")

    if(NOT arg_SOURCES)
        message(FATAL_ERROR "nxAddTest(${target}): SOURCES is required")
    endif()

    add_executable(${target} ${arg_SOURCES})
    target_link_libraries(${target} PRIVATE nx::std ${arg_DEPENDS} nx::testSupport GTest::gtest GTest::gtest_main)
    nxTargetDefaults(${target})
    gtest_discover_tests(${target} DISCOVERY_MODE PRE_TEST PROPERTIES ENVIRONMENT "${arg_ENVIRONMENT}")
endfunction()
