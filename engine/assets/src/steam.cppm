// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Detection of the user's own Counter-Strike 1.6 installation in the Steam libraries of this
/// computer (docs/06-assets-and-resources.md, section 2.3).
export module nx.assets:steam;

import std;
import nx.core;
import :content;

export namespace nx::assets {

/// How deep the blocks of a Valve KeyValues file may nest: far beyond Steam's own files, and the
/// bound of the reader's recursion.
inline constexpr std::size_t kMaxKeyValuesDepth = 32;

/// The library folders that `text`, the content of a Steam `steamapps/libraryfolders.vdf` (Valve
/// KeyValues), lists, in file order. Reads both layouts Steam has written: `"1" "D:\\Library"` and
/// `"1" { "path" "D:\\Library" ... }`.
/// Errors: ConfigInvalid, with "<sourceName>:<line>: <problem>" as the detail.
[[nodiscard]] auto readSteamLibraries(std::string_view text, std::string_view sourceName)
    -> Result<std::vector<std::filesystem::path>, ContentError>;

/// Looks for the installation of `desc` in the Steam libraries of `steamFolders`
/// (nx::platform::steamFolders()): each Steam folder is a library, and its
/// `steamapps/libraryfolders.vdf` lists the others. A library has the game when its
/// `steamapps/appmanifest_<steamAppId>.acf` exists, and the installation is then its
/// `steamapps/common/<steamInstallFolder>`. The first installation that GoldsrcInstall::open
/// accepts, in library order, is the result.
/// Errors: InstallNotFound when no library has the game; ConfigInvalid when steamInstallFolder is
/// not one directory name or a libraryfolders.vdf cannot be read; when installations exist and
/// none is accepted, the failure of the first one.
[[nodiscard]] auto detectGoldsrcInstall(const GoldsrcInstallDesc& desc,
                                        std::span<const std::filesystem::path> steamFolders)
    -> Result<GoldsrcInstall, ContentError>;

} // namespace nx::assets
