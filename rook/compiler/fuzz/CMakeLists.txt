# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: MIT

# The parser under fuzzing (docs/18-testing-and-quality.md, "Sanitizers and analysis"). Its corpus holds
# what only a fuzzer finds; the conformance suite's sources are seeds too.
rookAddFuzzer(fuzzRookParser
    SOURCES
        parserFuzzer.cpp
    DEPENDS
        rook::compiler
    CORPUS
        "${CMAKE_CURRENT_SOURCE_DIR}/corpus/parser"
        "${ROOK_SOURCE_DIRECTORY}/spec/conformance")
