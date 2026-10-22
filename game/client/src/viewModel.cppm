// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// A weapon in its holder's view (docs/05-rendering.md, section 9): the view model its data names
/// (cs::Weapon), each of its sequences decoded, posed where the player looks from, as the view's own
/// model is (cs::poseBones), in the sequence the player's weapons play (cs::Inventory); mirrored when
/// it was modeled in the other hand than the player's, the right one (cl_righthand's default), as
/// the original draws Counter-Strike's guns (Xash3D's R_AllowFlipViewModel, GPL, consulted: docs/20
/// section 3; the original's, observed: docs/05 section 9).
export module cs.client:viewModel;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.rhi;
import nx.render;
import cs.shared;
import :modelFiles;

export namespace cs {

/// How far from the middle the knife's crosshair is when the player stands, before the crosshair's
/// scale (measured: docs/12-ui-hud-and-console.md, section 3.2).
inline constexpr std::uint32_t kKnifeCrosshairDistance = 7;

/// Why a weapon's view model cannot be held.
enum class ViewModelError : std::uint8_t {
    FilesFailed, ///< A file of it cannot be read or used (cause: the model files' error).
    NoSequence,  ///< It lacks a sequence its weapon's data plays.
};

/// The hand the player holds its weapons in: cl_righthand's default, until the cvar comes.
inline constexpr Hand kPlayerHand = Hand::Right;

/// What a frame shows of the view model: the weapon's, its sequence, and how far into it.
struct ViewModelPose {
    WeaponIndex weapon{};
    std::uint32_t sequence{};
    nx::Seconds intoIt{};
};

/// A weapon's view model, its textures and the animations of its sequences.
class ViewModel {
public:
    /// Reads the view model of `weapon` from `vfs`, its `*T.mdl` when its textures are not in it, and
    /// the animations of each of its sequences, from their sequence group files. Errors: FilesFailed,
    /// NoSequence (one the weapon's view plays).
    [[nodiscard]] static auto read(const nx::assets::Vfs& vfs, const Weapon& weapon)
        -> nx::Result<ViewModel, ViewModelError>;

    /// Its model on the GPU of `renderer`, its uploads recorded in `commands`. Errors: those of
    /// StudioRenderer::load.
    [[nodiscard]] auto load(nx::render::StudioRenderer& renderer, nx::rhi::CommandList& commands) const
        -> nx::Result<nx::render::StudioModel, nx::render::StudioError>;

    /// Its bones `intoIt` into its sequence `sequence`, which must be one of its, played from its
    /// first frame (cs::sequenceFrameAt), in the hand of a player whose eye and view are `camera`'s:
    /// from each bone's space to the world, mirrored across the view's right when isMirrored.
    [[nodiscard]] auto pose(const nx::render::Camera& camera, std::uint32_t sequence,
                            nx::Seconds intoIt) const -> std::vector<nx::Mat3x4>;

    /// Whether it is drawn mirrored: modeled in the other hand than the player's.
    [[nodiscard]] inline auto isMirrored() const noexcept -> bool { return m_isMirrored; }

private:
    ViewModel(std::unique_ptr<ModelFiles> files, std::vector<nx::assets::AnimationFrames> animations,
              bool isMirrored) noexcept;

    std::unique_ptr<ModelFiles> m_files;
    std::vector<nx::assets::AnimationFrames> m_animations; ///< Of each of its sequences, in their order.
    bool m_isMirrored;
};

} // namespace cs
