// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.settings;

import std;
import nx.core;
import nx.platform;
import nx.console;

namespace nx::settings {

namespace {

constexpr std::string_view kConfigExtension = ".cfg";

/// What config.cfg says of itself on its first lines.
constexpr std::string_view kConfigHeader =
    "// Modern-Client.v2 writes this file when it ends: your settings as you left them.\n"
    "// Put your own in userconfig.cfg, which runs after it.\n";

/// Whether `path`, a path `exec` gave, names a `.cfg` file inside the folder.
[[nodiscard]] auto isServedPath(const std::filesystem::path& path) -> bool {
    if (path.empty() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const std::filesystem::path& part : path) {
        if (part == "..") {
            return false;
        }
    }
    return equalsIgnoringCase(platform::toUtf8(path.extension()), kConfigExtension);
}

} // namespace

UserConfigFiles::UserConfigFiles(std::filesystem::path directory) noexcept
    : m_directory{std::move(directory)} {}

auto UserConfigFiles::read(std::string_view path) -> Result<std::string, console::ConfigFileError> {
    const std::filesystem::path relative = platform::fromUtf8(path);
    if (!isServedPath(relative)) {
        return fail(console::ConfigFileError::InvalidPath, std::string{path});
    }
    auto text = platform::readTextFile(m_directory / relative);
    if (!text) {
        return fail(text.error().code == platform::FileError::NotFound ? console::ConfigFileError::NotFound
                                                                       : console::ConfigFileError::Unreadable,
                    std::string{path}, std::move(text.error()));
    }
    return std::move(*text);
}

void runStartupFiles(console::Console& console, UserConfigFiles& files) {
    for (const std::string_view name : kStartupFiles) {
        auto text = files.read(name);
        if (!text) {
            if (text.error().code != console::ConfigFileError::NotFound) {
                console.print(std::format("couldn't exec {}: {}", name, describe(text.error())));
            }
            continue;
        }
        console.addText(*text);
    }
    // Whether a `wait` left commands buffered matters to no one here: the game's frames run them.
    std::ignore = console.execute();
}

auto saveConfig(const console::Console& console, const KeyBindings& bindings, const UserConfigFiles& files)
    -> Result<void, SettingsError> {
    if (auto made = platform::createDirectories(files.directory()); !made) {
        return fail(SettingsError::SaveFailed, platform::toUtf8(files.directory()), std::move(made.error()));
    }
    const std::string text =
        std::format("{}\n{}{}", kConfigHeader, bindings.configText(), console.archivedCvars());
    const std::filesystem::path path = files.directory() / kConfigFile;
    if (auto written = platform::writeFile(path, std::as_bytes(std::span{text})); !written) {
        return fail(SettingsError::SaveFailed, platform::toUtf8(path), std::move(written.error()));
    }
    return {};
}

} // namespace nx::settings
