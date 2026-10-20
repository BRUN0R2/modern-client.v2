// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The entry point libFuzzer calls, in the fuzz presets (NX_FUZZ).

import std;

#include "fuzzTarget.hpp"

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int {
    nx::test::fuzz(std::as_bytes(std::span{data, size}));
    return 0; // libFuzzer reserves other values
}
