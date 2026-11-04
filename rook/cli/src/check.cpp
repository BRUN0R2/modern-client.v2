// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

module;

// stderr is a macro, which `import std;` does not export.
#include <cstdio>

module rook.commandLine;

import std;
import rook.compiler;

namespace rook::commandLine {

namespace {

constexpr std::string_view kSourceExtension = ".rook";

/// The diagnostics of one run a person can read: past these it prints how many more there are.
constexpr std::size_t kMaximumPrintedDiagnostics = 100;

/// `character` in UTF-8. It is a code point below U+110000, as a path's conversion leaves it.
[[nodiscard]] auto encodeUtf8(char32_t character) -> std::string {
    constexpr char32_t kOneByteLimit = 0x80;
    constexpr char32_t kTwoBytesLimit = 0x800;
    constexpr char32_t kThreeBytesLimit = 0x10000;
    constexpr unsigned kContinuationBits = 6;
    constexpr char32_t kContinuationMask = 0x3F;
    constexpr char32_t kContinuationMark = 0x80;
    constexpr char32_t kTwoBytesMark = 0xC0;
    constexpr char32_t kThreeBytesMark = 0xE0;
    constexpr char32_t kFourBytesMark = 0xF0;
    const auto byte = [](char32_t value) {
        return static_cast<char>(value);
    };
    const auto continuation = [&](unsigned shift) {
        return byte(((character >> shift) & kContinuationMask) | kContinuationMark);
    };
    if (character < kOneByteLimit) {
        return std::string{byte(character)};
    }
    if (character < kTwoBytesLimit) {
        return std::string{byte((character >> kContinuationBits) | kTwoBytesMark), continuation(0)};
    }
    if (character < kThreeBytesLimit) {
        return std::string{byte((character >> (2 * kContinuationBits)) | kThreeBytesMark),
                           continuation(kContinuationBits), continuation(0)};
    }
    return std::string{byte((character >> (3 * kContinuationBits)) | kFourBytesMark),
                       continuation(2 * kContinuationBits), continuation(kContinuationBits), continuation(0)};
}

/// The bytes of a path, UTF-8 everywhere. On Windows a path can hold what UTF-8 cannot say, and the
/// standard library's conversion would throw, which here would end the program: a lone surrogate
/// comes out as U+FFFD, and a diagnostic cleans what it prints.
template <class NativeText>
[[nodiscard]] auto nativePathText(const NativeText& native) -> std::string {
    if constexpr (std::is_same_v<typename NativeText::value_type, char>) {
        return native;
    } else {
        constexpr char32_t kFirstSurrogate = 0xD800;
        constexpr char32_t kFirstLowSurrogate = 0xDC00;
        constexpr char32_t kAfterLastSurrogate = 0xE000;
        constexpr char32_t kFirstSupplementary = 0x10000;
        constexpr char32_t kReplacement = 0xFFFD;
        constexpr unsigned kSurrogateBits = 10;
        std::string text;
        text.reserve(native.size());
        for (std::size_t index = 0; index < native.size(); ++index) {
            auto character = static_cast<char32_t>(native[index]);
            if (character >= kFirstSurrogate && character < kAfterLastSurrogate) {
                const bool isHigh = character < kFirstLowSurrogate;
                const char32_t next =
                    index + 1 < native.size() ? static_cast<char32_t>(native[index + 1]) : 0;
                if (isHigh && next >= kFirstLowSurrogate && next < kAfterLastSurrogate) {
                    character = kFirstSupplementary + ((character - kFirstSurrogate) << kSurrogateBits) +
                                (next - kFirstLowSurrogate);
                    index += 1;
                } else {
                    character = kReplacement;
                }
            }
            text += encodeUtf8(character);
        }
        return text;
    }
}

[[nodiscard]] auto pathText(const std::filesystem::path& path) -> std::string {
    return nativePathText(path.native());
}

/// What was asked for could not be read.
struct ReadFailure {
    std::string path;
    std::string reason;
};

/// The source at `path`: at most one byte over the compiler's limit, which is enough for it to refuse.
[[nodiscard]] auto readSource(const std::filesystem::path& path) -> std::expected<std::string, ReadFailure> {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::unexpected(ReadFailure{.path = pathText(path), .reason = "no such file, or not allowed"});
    }
    std::string bytes(compiler::kMaximumSourceBytes + 1, '\0');
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (file.bad()) {
        return std::unexpected(ReadFailure{.path = pathText(path), .reason = "reading failed"});
    }
    bytes.resize(static_cast<std::size_t>(file.gcount()));
    return bytes;
}

/// The `.rook` files under `directory`, in an order that is the same on every machine.
[[nodiscard]] auto sourcesUnder(const std::filesystem::path& directory)
    -> std::expected<std::vector<std::filesystem::path>, ReadFailure> {
    std::error_code error;
    std::filesystem::recursive_directory_iterator walker{directory, error};
    std::vector<std::filesystem::path> sources;
    const std::filesystem::recursive_directory_iterator end;
    while (!error && walker != end) {
        std::error_code entryError;
        if (walker->is_regular_file(entryError) && !entryError &&
            walker->path().extension() == kSourceExtension) {
            sources.push_back(walker->path());
        }
        walker.increment(error);
    }
    if (error) {
        return std::unexpected(ReadFailure{.path = pathText(directory), .reason = "walking it failed"});
    }
    std::ranges::sort(sources);
    return sources;
}

/// The files `paths` name, a directory replaced by what is under it.
[[nodiscard]] auto sourcesNamedBy(std::span<const std::string_view> paths)
    -> std::expected<std::vector<std::filesystem::path>, ReadFailure> {
    std::vector<std::filesystem::path> sources;
    for (const std::string_view text : paths) {
        const std::filesystem::path path{text}; // in the encoding of the command line
        std::error_code error;
        if (!std::filesystem::is_directory(path, error)) {
            sources.push_back(path);
            continue;
        }
        auto under = sourcesUnder(path);
        if (!under) {
            return std::unexpected(std::move(under.error()));
        }
        sources.append_range(*under);
    }
    return sources;
}

/// How many errors a run found, and how many of them it has printed.
struct Tally {
    std::size_t errors = 0;
    std::size_t printed = 0;
    std::size_t filesWithErrors = 0;
};

/// Prints what is wrong with one source, up to what a person can read in one run.
void printDiagnostics(const std::string& path, const compiler::ParseResult& result, Tally& tally) {
    const compiler::LineMap lines{result.tree.source()};
    for (const compiler::Diagnostic& diagnostic : result.diagnostics) {
        tally.errors += 1;
        if (tally.printed < kMaximumPrintedDiagnostics) {
            tally.printed += 1;
            printError(compiler::renderDiagnostic(diagnostic, path, result.tree.source(), lines));
            printError("\n");
        }
    }
    tally.filesWithErrors += result.diagnostics.empty() ? 0U : 1U;
}

[[nodiscard]] auto plural(std::size_t count, std::string_view word) -> std::string {
    return std::format("{} {}{}", count, word, count == 1 ? "" : "s");
}

} // namespace

auto argumentsOf(int argumentCount, char** arguments) -> std::vector<std::string_view> {
    const auto all = std::span{arguments, static_cast<std::size_t>(argumentCount)};
    std::vector<std::string_view> result;
    if (!all.empty()) {
        for (const char* argument : all.subspan(1)) {
            result.emplace_back(argument);
        }
    }
    return result;
}

void printError(std::string_view text) {
    std::print(stderr, "{}", text);
}

auto checkSources(std::string_view program, std::span<const std::string_view> paths) -> ExitCode {
    const auto failed = [program](const ReadFailure& failure) {
        printError(std::format("{}: cannot read {}: {}\n", program, compiler::textForTerminal(failure.path),
                               failure.reason));
        return ExitCode::Failure;
    };

    const auto sources = sourcesNamedBy(paths);
    if (!sources) {
        return failed(sources.error());
    }

    Tally tally;
    for (const std::filesystem::path& source : *sources) {
        const auto bytes = readSource(source);
        if (!bytes) {
            return failed(bytes.error());
        }
        printDiagnostics(pathText(source), compiler::parse(*bytes), tally);
    }

    if (tally.errors > tally.printed) {
        printError(std::format("... and {} more\n", plural(tally.errors - tally.printed, "error")));
    }
    if (tally.errors == 0) {
        std::println("{}: no errors", plural(sources->size(), "file"));
        return ExitCode::Clean;
    }
    printError(std::format("{} in {} of {}\n", plural(tally.errors, "error"), tally.filesWithErrors,
                           plural(sources->size(), "file")));
    return ExitCode::Diagnostics;
}

} // namespace rook::commandLine
