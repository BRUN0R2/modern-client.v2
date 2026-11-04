// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

// The entry point libFuzzer calls, with ROOK_FUZZ.

import std;

#include "fuzzTarget.hpp"

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int {
    rook::fuzzing::fuzz(std::as_bytes(std::span{data, size}));
    return 0; // libFuzzer reserves other values
}
