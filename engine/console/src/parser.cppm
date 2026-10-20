// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The console language of the original, as configs (.cfg), the console and binds write it.
///
/// A script is a sequence of commands. A command ends at a line break, or at a `;` that is outside
/// double quotes and not after a `//` on the same line. A command is a sequence of words separated
/// by whitespace; a word that starts with `"` runs to the next `"` (or to the end of the command)
/// and may hold whitespace and `;`. `//` at the start of a word comments out the rest of the
/// command. There are no escape sequences. So:
///
///     sv_downloadurl "http://example.com/cs"   // one word, the URL; the comment is ignored
///     alias jump "+jump; wait; -jump"          // three words; the alias body has three commands
///     echo one; echo two                       // two commands
export module nx.console:parser;

import std;
import nx.core;
import :text;

export namespace nx::console {

/// One command, split into words with the quotes removed.
struct CommandLine {
    std::vector<std::string> words; ///< The command name, then its arguments.
    std::string argumentText;       ///< What followed the name, as written, trimmed (`say` takes it whole).

    /// Empty for a command without words (a blank line or a comment).
    [[nodiscard]] auto name() const noexcept -> std::string_view {
        return words.empty() ? std::string_view{} : std::string_view{words.front()};
    }

    /// The words after the name.
    [[nodiscard]] auto arguments() const noexcept -> std::span<const std::string> {
        return words.empty() ? std::span<const std::string>{} : std::span{words}.subspan(1);
    }
};

/// The first command of `script` and the text after it.
struct SplitScript {
    std::string_view command;   ///< Without its separator.
    std::string_view remainder; ///< After the separator; empty at the end of the script.
};

/// Splits off the first command of `script` (see the module documentation).
[[nodiscard]] auto splitFirstCommand(std::string_view script) noexcept -> SplitScript;

/// The words of one command, as split by splitFirstCommand.
[[nodiscard]] auto tokenize(std::string_view command) -> CommandLine;

/// Every command of `script` that has words, in order.
[[nodiscard]] auto parseScript(std::string_view script) -> std::vector<CommandLine>;

} // namespace nx::console
