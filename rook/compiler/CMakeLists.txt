# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: MIT

rookAddModuleLibrary(rookCompiler
    ALIAS rook::compiler
    MODULES
        src/rook.compiler.cppm
        src/source.cppm
        src/token.cppm
        src/diagnostic.cppm
        src/lexer.cppm
        src/syntaxTree.cppm
        src/parser.cppm
    SOURCES
        src/source.cpp
        src/token.cpp
        src/diagnostic.cpp
        src/lexer.cpp
        src/syntaxTree.cpp
        src/parser.cpp
        src/parserItems.cpp
        src/parserTypes.cpp
        src/parserStatements.cpp
        src/parserExpressions.cpp)

if(ROOK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(ROOK_BUILD_TESTS OR ROOK_FUZZ)
    add_subdirectory(fuzz)
endif()
