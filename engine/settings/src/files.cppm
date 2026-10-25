// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The configuration files of the player (docs/12-ui-hud-and-console.md, section 5.2), in the user's
/// folder (nx::platform::userDirectory): config.cfg, which the game writes when it ends; then
/// userconfig.cfg, the player's own; then autoexec.cfg, run when the game starts.
export module nx.settings:files;

import std;
import nx.core;
import nx.console;
import :binds;

export namespace nx::settings {

/// Why the settings could not be saved.
enum class SettingsError : std::uint8_t {
    SaveFailed, ///< config.cfg could not be written (cause: the FileError).
};

/// The file the game writes the settings to when it ends, and the files it runs when it starts, in
/// the order it runs them.
inline constexpr std::string_view kConfigFile = "config.cfg";
inline constexpr std::array<std::string_view, 3> kStartupFiles{"config.cfg", "userconfig.cfg",
                                                               "autoexec.cfg"};

/// The `.cfg` files of a folder, which `exec` reads: a path is relative to it, names a `.cfg` file
/// (any case) and stays inside it.
class UserConfigFiles final : public console::ConfigFiles {
public:
    explicit UserConfigFiles(std::filesystem::path directory) noexcept;

    /// Errors: InvalidPath (absolute, out of the folder, not a `.cfg` file), NotFound, Unreadable.
    [[nodiscard]] auto read(std::string_view path) -> Result<std::string, console::ConfigFileError> override;

    [[nodiscard]] inline auto directory() const noexcept -> const std::filesystem::path& {
        return m_directory;
    }

private:
    std::filesystem::path m_directory;
};

/// Runs the player's configuration as the game starts: each of kStartupFiles that `files` has, in
/// that order, then the commands they queued, up to a `wait`, which leaves the rest for the console's
/// next execute() (the game's next frame). A file that is missing is not a failure; one that cannot
/// be read, or a command of one that fails, goes to the console's output.
void runStartupFiles(console::Console& console, UserConfigFiles& files);

/// Writes config.cfg in the folder of `files`, making the folder when it is missing: lines that say
/// what the file is, then `bindings` (KeyBindings::configText), then the archived cvars of `console`
/// (Console::archivedCvars), in the original's order. The file holds the old settings or the new
/// ones, never a part. Errors: SaveFailed.
[[nodiscard]] auto saveConfig(const console::Console& console, const KeyBindings& bindings,
                              const UserConfigFiles& files) -> Result<void, SettingsError>;

} // namespace nx::settings
