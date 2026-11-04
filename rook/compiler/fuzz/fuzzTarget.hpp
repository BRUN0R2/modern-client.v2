// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

#pragma once

// The one function of a fuzz target (rook/cmake/rookTargets.cmake, rookAddFuzzer). Each target defines
// it once; libFuzzerEntry.cpp or corpusDriver.cpp calls it. Include after `import std;`.

namespace rook::fuzzing {

/// Feeds `input` to the code under test. Any input must end in a value or a diagnostic: a crash, a
/// hang or undefined behavior is a bug the fuzzer reports.
void fuzz(std::span<const std::byte> input);

} // namespace rook::fuzzing
