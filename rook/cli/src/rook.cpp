// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

// rook: the toolchain's one command (docs/14-rook-language.md). Its first subcommand is `check`, which
// today parses; the others arrive with what they need.

import std;
import rook.commandLine;

namespace {

constexpr std::string_view kProgram = "rook";
constexpr std::string_view kCheckCommand = "check";
constexpr std::string_view kCurrentDirectory = ".";
constexpr std::string_view kUsage =
    "usage: rook check [<file or directory>...]\n"
    "Parses Rook sources, those under the current directory by default, and prints\n"
    "their diagnostics.\n";

} // namespace

auto main(int argumentCount, char** arguments) -> int {
    using namespace rook::commandLine;

    const std::vector<std::string_view> command = argumentsOf(argumentCount, arguments);
    if (command.empty() || command.front() == "--help") {
        printError(kUsage);
        return exitValue(command.empty() ? ExitCode::Failure : ExitCode::Clean);
    }
    if (command.front() != kCheckCommand) {
        printError("rook: unknown command\n");
        printError(kUsage);
        return exitValue(ExitCode::Failure);
    }
    const auto paths = std::span{command}.subspan(1);
    if (paths.empty()) {
        const std::array defaultPaths{kCurrentDirectory};
        return exitValue(checkSources(kProgram, defaultPaths));
    }
    return exitValue(checkSources(kProgram, paths));
}
