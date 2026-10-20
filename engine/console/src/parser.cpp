// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.console;

import std;
import nx.core;

namespace nx::console {

namespace {

constexpr std::string_view kCommentStart = "//";

} // namespace

auto splitFirstCommand(std::string_view script) noexcept -> SplitScript {
    bool insideQuotes = false;
    bool insideComment = false;
    for (std::size_t index = 0; index < script.size(); ++index) {
        const char character = script[index];
        if (character == '\n' || (character == ';' && !insideQuotes && !insideComment)) {
            return SplitScript{.command = script.substr(0, index), .remainder = script.substr(index + 1)};
        }
        if (character == '"') {
            insideQuotes = !insideQuotes;
        } else if (!insideQuotes && script.substr(index).starts_with(kCommentStart)) {
            insideComment = true;
        }
    }
    return SplitScript{.command = script, .remainder = script.substr(script.size())};
}

auto tokenize(std::string_view command) -> CommandLine {
    CommandLine line;
    std::size_t position = 0;
    const auto skipWhitespace = [&] {
        while (position < command.size() && isWhitespace(command[position])) {
            ++position;
        }
    };

    while (true) {
        skipWhitespace();
        if (position == command.size() || command.substr(position).starts_with(kCommentStart)) {
            break;
        }
        if (line.words.size() == 1) {
            line.argumentText = std::string{trimmed(command.substr(position))};
        }
        if (command[position] == '"') {
            // A quoted word runs to the closing quote, or to the end of the command without one.
            const std::size_t start = position + 1;
            const std::size_t closing = std::min(command.find('"', start), command.size());
            line.words.emplace_back(command.substr(start, closing - start));
            position = std::min(closing + 1, command.size());
        } else {
            const std::size_t start = position;
            while (position < command.size() && !isWhitespace(command[position])) {
                ++position;
            }
            line.words.emplace_back(command.substr(start, position - start));
        }
    }
    return line;
}

auto parseScript(std::string_view script) -> std::vector<CommandLine> {
    std::vector<CommandLine> commands;
    while (!script.empty()) {
        const auto [command, remainder] = splitFirstCommand(script);
        if (CommandLine line = tokenize(command); !line.words.empty()) {
            commands.push_back(std::move(line));
        }
        script = remainder;
    }
    return commands;
}

} // namespace nx::console
