# Helpers that give every project target the same structure and warning policy.

include_guard(GLOBAL)

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
#     MODULES <module interface units (.cppm), relative to src/>...
#     [SOURCES <implementation units (.cpp)>...]
#     [DEPENDS <targets>...])
#
# Creates a static library whose public interface is a set of C++ named modules. Every
# project module may `import std;` (nx::std is linked publicly).
function(nxAddModuleLibrary target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "ALIAS" "MODULES;SOURCES;DEPENDS")

    if(NOT arg_MODULES)
        message(FATAL_ERROR "nxAddModuleLibrary(${target}): MODULES is required")
    endif()
    if(NOT arg_ALIAS)
        message(FATAL_ERROR "nxAddModuleLibrary(${target}): ALIAS is required")
    endif()

    add_library(${target} STATIC)
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

# nxAddTest(<target> SOURCES <files>... DEPENDS <targets>...)
#
# Creates a GoogleTest executable and registers each test case with CTest.
function(nxAddTest target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;DEPENDS")

    if(NOT arg_SOURCES)
        message(FATAL_ERROR "nxAddTest(${target}): SOURCES is required")
    endif()

    add_executable(${target} ${arg_SOURCES})
    target_link_libraries(${target} PRIVATE nx::std ${arg_DEPENDS} GTest::gtest GTest::gtest_main)
    nxTargetDefaults(${target})
    gtest_discover_tests(${target} DISCOVERY_MODE PRE_TEST)
endfunction()
