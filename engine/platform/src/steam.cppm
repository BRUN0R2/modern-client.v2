// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Where Steam keeps its files on this computer: the start of the search for the user's own
/// Counter-Strike 1.6 installation (docs/06-assets-and-resources.md, section 2.3).
export module nx.platform:steam;

import std;

export namespace nx::platform {

/// The folders of the Steam installations of the current user, without duplicates: on Windows the
/// `SteamPath` of `HKCU\Software\Valve\Steam`; on Linux `~/.steam/steam`, `~/.local/share/Steam`
/// and the Flatpak `~/.var/app/com.valvesoftware.Steam/.local/share/Steam`. Only folders that
/// exist: empty when Steam is not installed, or when the registry value or the home folder cannot
/// be read (the user then names the installation, docs/06 section 2.3).
[[nodiscard]] auto steamFolders() -> std::vector<std::filesystem::path>;

} // namespace nx::platform
