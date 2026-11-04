// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

// A fuzz target without libFuzzer: runs rook::fuzzing::fuzz once on every file under the corpus
// directories named on the command line. It keeps the seeds and every crash ever found as a test of
// every build (rook/cmake/rookTargets.cmake, rookAddFuzzer).

// stderr is a macro, which `import std;` does not export.
#include <cstdio>

import std;

#include "fuzzTarget.hpp"

namespace {

constexpr int kSuccess = 0;
constexpr int kFailure = 1;

[[nodiscard]] auto readFile(const std::filesystem::path& path) -> std::optional<std::vector<std::byte>> {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    const std::string bytes{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    const auto view = std::as_bytes(std::span{bytes});
    return std::vector<std::byte>{view.begin(), view.end()};
}

/// Runs every file under `directory`, sorted. Returns how many ran, or nothing when one cannot be read.
[[nodiscard]] auto runCorpus(const std::filesystem::path& directory) -> std::optional<std::size_t> {
    std::error_code error;
    std::vector<std::filesystem::path> files;
    std::filesystem::recursive_directory_iterator walker{directory, error};
    const std::filesystem::recursive_directory_iterator end;
    while (!error && walker != end) {
        std::error_code entryError;
        if (walker->is_regular_file(entryError) && !entryError) {
            files.push_back(walker->path());
        }
        walker.increment(error);
    }
    if (error) {
        return std::nullopt;
    }
    std::ranges::sort(files);
    for (const std::filesystem::path& file : files) {
        const auto input = readFile(file);
        if (!input) {
            return std::nullopt;
        }
        rook::fuzzing::fuzz(*input);
    }
    return files.size();
}

} // namespace

auto main(int argumentCount, char** arguments) -> int {
    std::size_t inputs = 0;
    for (const char* directory : std::span{arguments, static_cast<std::size_t>(argumentCount)}.subspan(1)) {
        const auto ran = runCorpus(std::filesystem::path{directory}); // in the encoding of the command line
        if (!ran) {
            std::println(stderr, "cannot read the corpus at {}", directory);
            return kFailure;
        }
        inputs += *ran;
    }
    std::println("{} inputs", inputs);
    return inputs == 0 ? kFailure : kSuccess; // an empty corpus tests nothing
}
