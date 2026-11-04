# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: MIT

rookAddTest(rookCompilerTests
    SOURCES
        lexerTests.cpp
        parserTests.cpp
        diagnosticTests.cpp
    DEPENDS
        rook::compiler)
