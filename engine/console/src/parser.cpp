// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.console;

import std;
import nx.core;

namespace nx::console {

namespace {

constexpr std::string_view kCommentStart = "//";

/// A word of a command: its text, without quotes, and where it starts in the script.
struct ScannedWord {
    std::string_view text;
    std::size_t start = 0;
};

/// The one definition of the words of the language, shared by splitFirstCommand and tokenize: hands
/// each word of the first command of `script` to `onWord`, in order, and returns where the command
/// ends (its `\n` or `;`, or the end of the script).
template <class OnWord>
auto scanFirstCommand(std::string_view script, OnWord&& onWord) -> std::size_t {
    std::size_t position = 0;
    while (position < script.size()) {
        const char character = script[position];
        if (character == '\n' || character == ';') {
            return position;
        }
        if (isWhitespace(character)) {
            ++position;
        } else if (script.substr(position).starts_with(kCommentStart)) {
            // A comment runs to the end of the line, any `;` in it included.
            position = std::min(script.find('\n', position), script.size());
        } else if (character == '"') {
            // A quoted word runs to its closing quote, or to the end of the line without one.
            const std::size_t first = position + 1;
            const std::size_t last = std::min(script.find_first_of("\"\n", first), script.size());
            onWord(ScannedWord{.text = script.substr(first, last - first), .start = position});
            position = last < script.size() && script[last] == '"' ? last + 1 : last;
        } else {
            // Any other word runs to whitespace or a `;`; a `"` in it is an ordinary character.
            const std::size_t first = position;
            while (position < script.size() && !isWhitespace(script[position]) && script[position] != ';') {
                ++position;
            }
            onWord(ScannedWord{.text = script.substr(first, position - first), .start = first});
        }
    }
    return position;
}

} // namespace

auto splitFirstCommand(std::string_view script) noexcept -> SplitScript {
    const std::size_t end = scanFirstCommand(script, [](const ScannedWord& /*word*/) noexcept {});
    if (end == script.size()) {
        return SplitScript{.command = script, .remainder = script.substr(end)};
    }
    return SplitScript{.command = script.substr(0, end), .remainder = script.substr(end + 1)};
}

auto tokenize(std::string_view command) -> CommandLine {
    CommandLine line;
    std::optional<std::size_t> argumentsStart;
    const std::size_t end = scanFirstCommand(command, [&line, &argumentsStart](const ScannedWord& word) {
        if (line.words.size() == 1) {
            argumentsStart = word.start;
        }
        line.words.emplace_back(word.text);
    });
    if (argumentsStart) {
        line.argumentText = std::string{trimmed(command.substr(*argumentsStart, end - *argumentsStart))};
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
