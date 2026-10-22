// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The command line of `next`, the original's (`next +map de_dust2 +sensitivity 2`): `-basedir`, the
/// `+` commands it runs as it starts, and the options it adds.
export module app.client:commandLine;

import std;

export namespace app {

/// What `--help` prints, and what a command line that is not valid shows.
inline constexpr std::string_view kClientUsage =
    R"(usage: next [-basedir <folder>] [--data <folder>] [--validation] [+<command> [<argument>...]]...

Plays Counter-Strike 1.6. Each word that starts with + runs a console command as the client starts,
after the player's config.cfg, userconfig.cfg and autoexec.cfg, with the words after it up to the
next one that starts with + or - as its arguments, as the original reads its command line:
`next +map de_dust2` plays de_dust2. Until the menus come, Escape ends it, as does `quit`.

  -basedir <folder>  the Counter-Strike 1.6 installation: the folder with cstrike/ and valve/
                     (default: the one Steam installed)
  --data <folder>    the data/ folder of the project (default: the one next to next)
  --validation       the Khronos validation layer, when it is installed: its messages are logged
                     and next exits with 1 when it reported an error
)";

/// How `next` was started.
struct ClientOptions {
    std::optional<std::filesystem::path> install; ///< `-basedir`.
    std::optional<std::filesystem::path> data;    ///< `--data`.
    /// The `+` commands, one a line, without their `+`: `map de_dust2\n`.
    std::string bootCommands;
    bool isValidationEnabled = false; ///< `--validation`.
};

/// The options of `arguments`, the command line without the program. A word that starts with `+`
/// starts a command, and the words after it up to the next that starts with `+` or `-` are its
/// arguments, joined by spaces as the original's stuffcmds joins them. None for an option that is
/// not one of the above, one given twice, or one without its folder.
[[nodiscard]] auto parseClientOptions(std::span<const std::string_view> arguments)
    -> std::optional<ClientOptions>;

} // namespace app
