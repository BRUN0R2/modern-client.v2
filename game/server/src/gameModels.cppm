// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The studio models the game of a map plays (docs/10-counter-strike-gameplay.md, sections 4.2 and 5): its
/// hostages' models and its players' characters', each file read once with the animations of its
/// sequences, without the textures only what draws them needs; the game plays them, choosing what they
/// play by their sequences and posing their bones for its traces.
export module cs.server:gameModels;

import std;
import nx.core;
import nx.assets;
import cs.shared;

export namespace cs {

/// The studio models the game of a map plays, the map's hostages that play them, and the characters'.
class GameModels {
public:
    /// Reads the model of each hostage of `map` (mapHostagesOf) and of each of `characters` from `vfs`,
    /// each file once; a hostage or a character whose model or animations cannot be read is left out, and
    /// listed.
    [[nodiscard]] static auto read(const nx::assets::Vfs& vfs, const nx::assets::BspView& map,
                                   const Characters& characters) -> GameModels;

    GameModels(GameModels&& other) noexcept = default;
    auto operator=(GameModels&& other) noexcept -> GameModels& = default;
    GameModels(const GameModels&) = delete;
    auto operator=(const GameModels&) -> GameModels& = delete;
    ~GameModels() = default;

    /// The models, by the index of their file: each file's and the frames of each of its sequences. They
    /// point into the models' files, which stay where they are.
    [[nodiscard]] inline auto played() const noexcept -> std::span<const PlayedModel> { return m_played; }
    /// The map's hostages of models it read, in the map's order, for the game to make (spawnHostages).
    [[nodiscard]] inline auto hostageSpawns() const noexcept -> std::span<const HostageSpawn> {
        return m_hostageSpawns;
    }
    /// The model `character` plays; none for one whose model could not be read.
    [[nodiscard]] auto characterModel(CharacterIndex character) const noexcept -> const PlayedModel*;
    /// The models left out: missing or unreadable, with why.
    [[nodiscard]] inline auto missing() const noexcept -> std::span<const std::string> { return m_missing; }

private:
    /// A model's files, and the decoded frames of each of its sequences, by index.
    struct File {
        std::unique_ptr<ModelFiles> files{};
        std::vector<nx::assets::AnimationFrames> animations{};
    };

    GameModels() = default;

    /// The file of the model at `path`, read once among `files`, the files by path; none for one that
    /// cannot be read, listed.
    [[nodiscard]] auto fileOf(const nx::assets::Vfs& vfs, std::string_view path,
                              std::map<std::string, std::optional<std::uint32_t>, std::less<>>& files)
        -> std::optional<std::uint32_t>;

    std::vector<std::unique_ptr<File>> m_files;
    std::vector<PlayedModel> m_played; ///< One for each file.
    std::vector<HostageSpawn> m_hostageSpawns;
    std::vector<std::optional<std::uint32_t>> m_characterFiles; ///< By CharacterIndex.
    std::vector<std::string> m_missing;
};

} // namespace cs
