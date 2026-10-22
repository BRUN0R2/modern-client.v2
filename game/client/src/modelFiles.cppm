// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The files of a studio model (not exported): the model, its `*T.mdl` when its textures are not in it,
/// and the animations of its sequences, from the file each is in.
export module cs.client:modelFiles;

import std;
import nx.core;
import nx.assets;

namespace cs {

/// Why a model's files cannot be used.
enum class ModelFileError : std::uint8_t {
    ReadFailed, ///< A file cannot be read (cause: the VfsError).
    Invalid,    ///< The reader rejected a file (cause: the MdlError).
};

/// A studio model's files: their bytes, which the views point into.
struct ModelFiles {
    std::string path{};
    std::vector<std::byte> bytes{};
    std::vector<std::byte> texturesBytes{}; ///< Its `*T.mdl`'s; empty when its textures are in it.
    std::optional<nx::assets::MdlView> view{};
    std::optional<nx::assets::MdlView> textures{}; ///< Its `*T.mdl`; none when its textures are in it.

    /// Where its textures and skins are: its `*T.mdl`, or itself.
    [[nodiscard]] auto texturesView() const -> const nx::assets::MdlView&;
};

/// Reads the model at `path`, a path as GoldSrc content writes it (a leading slash is the game's folder),
/// and its `*T.mdl` when its textures are not in it. Errors: ReadFailed, Invalid.
[[nodiscard]] auto readModelFiles(const nx::assets::Vfs& vfs, std::string_view path)
    -> nx::Result<std::unique_ptr<ModelFiles>, ModelFileError>;

/// The decoded frames of sequence `sequence` of the model of `files`, from its file or its sequence
/// group's. Errors: ReadFailed, Invalid.
[[nodiscard]] auto animationOf(const nx::assets::Vfs& vfs, const ModelFiles& files,
                               nx::assets::SequenceIndex sequence)
    -> nx::Result<nx::assets::AnimationFrames, ModelFileError>;

} // namespace cs
