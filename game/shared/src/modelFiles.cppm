// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The files of a studio model: the model, its `*T.mdl` when its textures are not in it and what reads
/// it draws it, and the animations of its sequences, from the file each is in. The server reads what it
/// plays, a client what it draws.
export module cs.shared:modelFiles;

import std;
import nx.core;
import nx.assets;

export namespace cs {

/// Why a model's files cannot be used.
enum class ModelFileError : std::uint8_t {
    ReadFailed, ///< A file cannot be read (cause: the VfsError).
    Invalid,    ///< The reader rejected a file (cause: the MdlError).
};

/// Whether a model's `*T.mdl` is read with it: what draws it needs its textures, what only plays it
/// (the server) does not.
enum class ModelTextures : std::uint8_t {
    Read,
    Skipped,
};

/// A studio model's files: their bytes, which the views point into.
struct ModelFiles {
    std::string path{};
    std::vector<std::byte> bytes{};
    std::vector<std::byte> texturesBytes{}; ///< Its `*T.mdl`'s; empty when its textures are in it.
    std::optional<nx::assets::MdlView> view{};
    /// Its `*T.mdl`; none when its textures are in it, or when they were not read (ModelTextures).
    std::optional<nx::assets::MdlView> textures{};

    /// Where its textures and skins are, of files read with them: its `*T.mdl`, or itself.
    [[nodiscard]] auto texturesView() const -> const nx::assets::MdlView&;
};

/// Reads the model at `path`, a path as GoldSrc content writes it (a leading slash is the game's folder),
/// and, as `textures` says, its `*T.mdl` when its textures are not in it. Errors: ReadFailed, Invalid.
[[nodiscard]] auto readModelFiles(const nx::assets::Vfs& vfs, std::string_view path, ModelTextures textures)
    -> nx::Result<std::unique_ptr<ModelFiles>, ModelFileError>;

/// The decoded frames of every sequence of the model of `files`, by index, each sequence group's file
/// read once. Errors: ReadFailed, Invalid.
[[nodiscard]] auto animationsOf(const nx::assets::Vfs& vfs, const ModelFiles& files)
    -> nx::Result<std::vector<nx::assets::AnimationFrames>, ModelFileError>;

/// A studio model read to be played: its files, and the decoded frames of each of its sequences, by
/// index. What plays it points into them, which stay where they are as it moves.
struct AnimatedModel {
    std::unique_ptr<ModelFiles> files{};
    std::vector<nx::assets::AnimationFrames> animations{};
};

/// Reads the model at `path` and, as `textures` says, its `*T.mdl` (readModelFiles), and the decoded
/// frames of its sequences (animationsOf). Errors: ReadFailed, Invalid.
[[nodiscard]] auto readAnimatedModel(const nx::assets::Vfs& vfs, std::string_view path,
                                     ModelTextures textures) -> nx::Result<AnimatedModel, ModelFileError>;

} // namespace cs
