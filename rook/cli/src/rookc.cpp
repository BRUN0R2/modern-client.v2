// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

// rookc: the compiler (docs/14-rook-language.md, section 23). Today it parses its sources and prints
// their diagnostics: it writes no bytecode yet, which takes the rest of the compiler.

import std;
import rook.commandLine;

namespace {

constexpr std::string_view kProgram = "rookc";
constexpr std::string_view kUsage =
    "usage: rookc <file or directory>...\n"
    "Parses Rook sources and prints their diagnostics. It writes no bytecode yet.\n";

} // namespace

auto main(int argumentCount, char** arguments) -> int {
    using namespace rook::commandLine;

    const std::vector<std::string_view> paths = argumentsOf(argumentCount, arguments);
    if (paths.empty() || paths.front() == "--help") {
        printError(kUsage);
        return exitValue(paths.empty() ? ExitCode::Failure : ExitCode::Clean);
    }
    return exitValue(checkSources(kProgram, paths));
}
