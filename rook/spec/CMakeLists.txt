# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: MIT

# The conformance suite (conformance/README.md): the executable reference of the language. It needs
# nothing but the compiler, so that the toolchain built on its own runs it too.
rookAddProgram(rookConformance SOURCES conformanceRunner.cpp DEPENDS rook::compiler)

add_test(NAME Rook.Conformance
    COMMAND rookConformance
        "${CMAKE_CURRENT_SOURCE_DIR}/conformance"
        "${CMAKE_CURRENT_SOURCE_DIR}/grammar.ebnf")
