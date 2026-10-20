// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// The one function of a fuzz target (cmake/nxTargets.cmake, nxAddFuzzer). Each target defines it
// once; libFuzzerEntry.cpp or corpusDriver.cpp calls it. Include after `import std;`.

namespace nx::test {

/// Feeds `input` to the code under test. Any input must end in a value or a typed failure: a
/// crash, a contract violation or undefined behavior is a bug the fuzzer reports.
void fuzz(std::span<const std::byte> input);

} // namespace nx::test
