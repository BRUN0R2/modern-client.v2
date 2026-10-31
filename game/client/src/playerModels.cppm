// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The models other players are drawn with (docs/05-rendering.md, section 9): each character's, posed as
/// the server tells it plays and as the client turns its legs and its aim (cs::posePlayerBones), and the
/// player model of the weapon in its hands (models/p_<weapon>.mdl), whose bones take those of the
/// player's of the same name, as the original's engine merges them (StudioMergeBones); each lit where
/// the player is.
export module cs.client:playerModels;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.rhi;
import nx.render;
import cs.shared;
import :posedModels;

export namespace cs {

/// How another player shows in a frame: the character it plays as and the weapon in its hands; where it
/// is and how its model is turned (its legs' yaw); the sequence its torso plays and where in it, its legs'
/// gait sequence and where in it; the aim's yaw and pitch blends; and how opaque it is drawn, of 255 (a
/// corpse fading out).
struct ShownPlayer {
    CharacterIndex character{};
    std::optional<WeaponIndex> weapon{};
    nx::Vec3 origin{};
    nx::EulerAngles angles{};
    nx::assets::SequenceIndex sequence{};
    SequenceFrame frame{};
    nx::assets::SequenceIndex gaitSequence{};
    SequenceFrame gaitFrame{};
    float yawBlend = 0.0F;
    float pitchBlend = 0.0F;
    std::uint8_t amount = std::numeric_limits<std::uint8_t>::max();
};

/// The models on the GPU the players are drawn with: each character's, and each weapon's player model,
/// by their indices; none for one that could not be read.
struct PlayerGpuModels {
    std::vector<std::optional<nx::render::StudioModel>> characters{};
    std::vector<std::optional<nx::render::StudioModel>> weapons{};
};

/// The models of the game's characters and of its weapons' player models, read once.
class PlayerModels {
public:
    /// Reads the model of each of `characters` and the player model of each complete weapon of
    /// `weapons` from `vfs`, with their textures and the animations of their sequences; one that cannot
    /// be read is left out, and listed.
    [[nodiscard]] static auto read(const nx::assets::Vfs& vfs, const Characters& characters,
                                   const WeaponCatalog& weapons) -> PlayerModels;

    PlayerModels(PlayerModels&& other) noexcept = default;
    auto operator=(PlayerModels&& other) noexcept -> PlayerModels& = default;
    PlayerModels(const PlayerModels&) = delete;
    auto operator=(const PlayerModels&) -> PlayerModels& = delete;
    ~PlayerModels() = default;

    /// The models left out: missing or unreadable, with why.
    [[nodiscard]] inline auto missing() const noexcept -> std::span<const std::string> { return m_missing; }
    /// The model of `character`; none for one that could not be read.
    [[nodiscard]] auto characterModel(CharacterIndex character) const noexcept -> const nx::assets::MdlView*;

    /// Reads the player model of `weapon`, made or changed as the game runs, from `vfs`: none until its
    /// values make a weapon, and none, listed, when it cannot be read.
    void readWeapon(const nx::assets::Vfs& vfs, const WeaponCatalog& weapons, WeaponIndex weapon);

    /// The characters' models on the GPU of `renderer`, its uploads recorded in `commands`: the weapons'
    /// come with loadWeapon. Errors: those of StudioRenderer::load.
    [[nodiscard]] auto load(nx::render::StudioRenderer& renderer, nx::rhi::CommandList& commands) const
        -> nx::Result<PlayerGpuModels, nx::render::StudioError>;
    /// The player model of `weapon` on the GPU of `renderer` into `models`, its uploads recorded in
    /// `commands`: none for one it has none of. Errors: those of StudioRenderer::load.
    [[nodiscard]] auto loadWeapon(nx::render::StudioRenderer& renderer, nx::rhi::CommandList& commands,
                                  WeaponIndex weapon, PlayerGpuModels& models) const
        -> nx::Result<void, nx::render::StudioError>;

    /// `players` posed, drawn with `models`, what load gave, each lit where it is by `light`, the sky's
    /// `sky` and the styles' `styles`: each character's model, and the weapon in its hands merged onto it.
    [[nodiscard]] auto posed(const PlayerGpuModels& models, std::span<const ShownPlayer> players,
                             const nx::render::WorldLight& light, const nx::render::SkyLight& sky,
                             const nx::render::LightStyleValues& styles) const -> PosedModels;

private:
    PlayerModels() = default;

    /// The model at `path` read from `vfs`; none, listed, for one that cannot be read.
    [[nodiscard]] auto readFile(const nx::assets::Vfs& vfs, std::string_view path)
        -> std::optional<AnimatedModel>;

    std::vector<std::optional<AnimatedModel>> m_characters; ///< By CharacterIndex; none for one left out.
    std::vector<std::optional<AnimatedModel>> m_weapons;    ///< By WeaponIndex; none for one left out.
    std::vector<std::string> m_missing;
};

} // namespace cs
