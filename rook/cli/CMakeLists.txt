# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: MIT

if(NOT ROOK_BUILD_PROGRAMS)
    return()
endif()

# What rookc and rook share: both are thin programs over the compiler's library.
rookAddModuleLibrary(rookCommandLine
    ALIAS rook::commandLine
    MODULES
        src/rook.commandLine.cppm
    SOURCES
        src/check.cpp
    DEPENDS
        rook::compiler)

rookAddProgram(rookc SOURCES src/rookc.cpp DEPENDS rook::commandLine)
rookAddProgram(rook SOURCES src/rook.cpp DEPENDS rook::commandLine)

if(ROOK_BUILD_TESTS)
    # Each program as its user runs it. Its exit code tells a clean source from one with errors.
    set(cases "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_test(NAME Rook.RookcAcceptsACleanSource COMMAND rookc "${cases}/clean.rook")
    add_test(NAME Rook.RookcPrintsDiagnostics COMMAND rookc "${cases}/withErrors.rook")
    add_test(NAME Rook.RookCheckWalksADirectory COMMAND rook check "${cases}")
    add_test(NAME Rook.RookCheckAcceptsACleanFile COMMAND rook check "${cases}/clean.rook")
    add_test(NAME Rook.MissingFileIsNotADiagnostic COMMAND rookc "${cases}/noSuchFile.rook")
    add_test(NAME Rook.RookRefusesAnUnknownCommand COMMAND rook build)

    # A diagnostic with its code, where it is, a snippet with carets and a fix, in that order.
    set(rendered "error\\[R0030\\]: a condition goes in parentheses.*")
    string(APPEND rendered "withErrors\\.rook:5:8.*")
    string(APPEND rendered "5 \\|     if count > 0 \\{.*")
    string(APPEND rendered "\\| +\\^+ expected `\\(` here.*")
    string(APPEND rendered "= fix: write `if \\(condition\\) \\{ \\.\\.\\. \\}`")
    set_tests_properties(Rook.RookcPrintsDiagnostics PROPERTIES PASS_REGULAR_EXPRESSION "${rendered}")
    set_tests_properties(Rook.RookCheckWalksADirectory PROPERTIES PASS_REGULAR_EXPRESSION "error\\[R0030\\].*2 files")
    set_tests_properties(Rook.MissingFileIsNotADiagnostic PROPERTIES PASS_REGULAR_EXPRESSION "cannot read")
    set_tests_properties(Rook.RookRefusesAnUnknownCommand PROPERTIES PASS_REGULAR_EXPRESSION "unknown command")
endif()
