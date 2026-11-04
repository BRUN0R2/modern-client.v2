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
    SOURCES
        src/source.cpp
        src/token.cpp
        src/diagnostic.cpp
        src/lexer.cpp)

if(ROOK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
