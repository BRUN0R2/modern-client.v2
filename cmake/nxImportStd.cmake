# Enables `import std;` for every target in the project.
#
# CMake still ships `import std` behind an experimental gate whose UUID changes with every
# CMake release. The table below lists the releases validated by this project. An unknown
# release fails fast with instructions instead of silently building without the std module.
#
# Must be included BEFORE project(): CMake reads the gate when it enables the CXX language.

include_guard(GLOBAL)

# CMake <major.minor> followed by the gate UUID shipped in that release.
set(NX_IMPORT_STD_GATES
    "4.3" "451f2fe2-a8a2-47c3-bc32-94786d8fc91b"  # Visual Studio 2026 bundled CMake
    "4.4" "f35a9ac6-8463-4d38-8eec-5d6008153e7d"
)

string(REGEX MATCH "^[0-9]+\\.[0-9]+" nxCmakeMinor "${CMAKE_VERSION}")
list(FIND NX_IMPORT_STD_GATES "${nxCmakeMinor}" nxGateIndex)

if(nxGateIndex EQUAL -1)
    message(FATAL_ERROR
        "CMake ${CMAKE_VERSION} is not in the validated `import std` gate table.\n"
        "Add its CMAKE_EXPERIMENTAL_CXX_IMPORT_STD UUID to cmake/nxImportStd.cmake "
        "(see docs/17-build-e-ferramentas.md, section 2) or use one of: 4.3, 4.4.")
endif()

math(EXPR nxGateUuidIndex "${nxGateIndex} + 1")
list(GET NX_IMPORT_STD_GATES ${nxGateUuidIndex} CMAKE_EXPERIMENTAL_CXX_IMPORT_STD)
unset(nxCmakeMinor)
unset(nxGateIndex)
unset(nxGateUuidIndex)

# Every target may `import std;` (CXX_MODULE_STD default).
set(CMAKE_CXX_MODULE_STD ON)
