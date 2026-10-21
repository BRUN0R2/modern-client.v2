// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nxcheck — reads every file of a content source through the VFS with the readers of nx.assets,
/// and the files they join (a model's `*T.mdl` and sequence group files), as the engine will load
/// them. It validates the readers on real content, such as the user's own Counter-Strike 1.6
/// installation (docs/18-testing-and-quality.md, "Real content"), before anything draws it.
export module tool.nxcheck;

import std;
import nx.core;
import nx.jobs;
import nx.assets;

export namespace nxcheck {

/// The formats with a reader in nx.assets.
enum class Format : std::uint8_t {
    Bsp,           ///< A map: BspView, which reads every lump and its embedded textures.
    Wad,           ///< A WAD3: WadView, and readTexture on every mip texture.
    Mdl,           ///< A model: MdlView; the `*T.mdl` its textures are in, when they are not in it;
                   ///< and the animation of every sequence, from its sequence group file too.
    SequenceGroup, ///< The `*0N.mdl` of a model's animations: checked through the models that use it.
    Spr,           ///< A sprite: SpriteView.
    Tga,           ///< An image (the skies, the VGUI menus): readTga.
    Wav,           ///< A sound: parseWav.
    Count,
};

/// Why a file failed its check.
enum class CheckError : std::uint8_t {
    ReadFailed,       ///< The VFS could not read it (cause: the VfsError).
    Invalid,          ///< Its reader rejected it (cause: the reader's error).
    DependencyFailed, ///< A file it joins is missing, unreadable or rejected (cause: that failure).
};

/// What checking one file found.
struct FileResult {
    std::string path;             ///< Its logical path.
    std::optional<Format> format; ///< nullopt when no reader reads its extension yet.
    std::uint64_t bytes = 0;      ///< What its reader read; 0 without one.
    std::optional<nx::Failure<CheckError>> failure{};
};

/// Checks every file of `vfs`, in the order of Vfs::paths(), on the workers of `jobs`.
[[nodiscard]] auto checkContent(const nx::assets::Vfs& vfs, nx::jobs::JobSystem& jobs)
    -> std::vector<FileResult>;

} // namespace nxcheck
