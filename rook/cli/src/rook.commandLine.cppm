// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

/// rook.commandLine — what the programs rookc and rook share (docs/14-rook-language.md, section 23).
export module rook.commandLine;

import std;

export namespace rook::commandLine {

/// What a program of the toolchain tells whoever ran it.
enum class ExitCode : std::uint8_t {
    Clean = 0,
    /// The sources have errors, which were printed.
    Diagnostics = 1,
    /// The program could not do what was asked: a bad command line, or a file it cannot read.
    Failure = 2,
};

/// The value `main` returns for `code`.
[[nodiscard]] constexpr auto exitValue(ExitCode code) noexcept -> int {
    return static_cast<int>(code);
}

/// The command line of `main`, without the program's own name.
[[nodiscard]] auto argumentsOf(int argumentCount, char** arguments) -> std::vector<std::string_view>;

/// Parses every source `paths` name, a directory standing for the `.rook` files under it, and prints
/// their diagnostics and a summary. `program` names who is speaking in a failure's message.
[[nodiscard]] auto checkSources(std::string_view program, std::span<const std::string_view> paths)
    -> ExitCode;

/// Prints `text` to the standard error stream, where a compiler's diagnostics go.
void printError(std::string_view text);

} // namespace rook::commandLine
