// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// A fuzz target without libFuzzer: runs nx::test::fuzz once on every file of the corpus
// directories named on the command line. It keeps the seeds and every crash ever found as a test
// of every build (cmake/nxTargets.cmake, nxAddFuzzer).

// stderr is a macro, which `import std;` does not export; the std::ostream overload of
// std::println is unavailable without exceptions.
#include <cstdio>

import std;
import nx.core;
import nx.platform;

#include "fuzzTarget.hpp"

namespace {

constexpr int kSuccess = 0;
constexpr int kFailure = 1;

/// Runs every file of `directory`, sorted. Returns how many ran, or the failure to read one.
[[nodiscard]] auto runCorpus(const std::filesystem::path& directory)
    -> nx::Result<std::size_t, nx::platform::FileError> {
    const auto files = nx::platform::listFiles(directory);
    if (!files) {
        return std::unexpected(files.error());
    }
    for (const std::filesystem::path& file : *files) {
        const auto input = nx::platform::readFile(directory / file);
        if (!input) {
            return std::unexpected(input.error());
        }
        nx::test::fuzz(*input);
    }
    return files->size();
}

} // namespace

auto main(int argumentCount, char** arguments) -> int {
    std::size_t inputs = 0;
    for (const char* directory : std::span{arguments, static_cast<std::size_t>(argumentCount)}.subspan(1)) {
        const auto ran = runCorpus(std::filesystem::path{directory}); // in the encoding of the command line
        if (!ran) {
            std::println(stderr, "{}", nx::describe(ran.error()));
            return kFailure;
        }
        inputs += *ran;
    }
    std::println("{} inputs", inputs);
    return inputs == 0 ? kFailure : kSuccess; // an empty corpus tests nothing
}
