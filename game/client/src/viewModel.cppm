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

/// What a frame shows of the view model: the weapon's, its sequence, when it started, and how far
/// into it.
struct ViewModelPose {
    WeaponIndex weapon{};
    std::uint32_t sequence{};
    nx::GameTime start{}; ///< A sequence started again (a shot after a shot) plays its events again.
    nx::Seconds intoIt{};

    /// Whether `other` is the same play of the same sequence, further into it or not.
    [[nodiscard]] inline auto isSamePlay(const ViewModelPose& other) const noexcept -> bool {
        return weapon == other.weapon && sequence == other.sequence && start == other.start;
    }
};

/// The first number of the client's events; the game's own come before.
inline constexpr std::int32_t kFirstClientEvent = 5000;
/// The client event that plays a sound at full volume, its options the sound's name, a path under
/// `sound/` (the reloads' and the draws' sounds of the original's view models).
inline constexpr std::int32_t kSoundEvent = 5004;

/// The client events of `sequence` its play passes from frame `from`, left out, to frame `to`,
/// included, in their order.
[[nodiscard]] auto clientEventsBetween(const nx::assets::StudioSequence& sequence, float from, float to)
    -> std::vector<const nx::assets::StudioEvent*>;

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

    /// Its sequence `sequence`, which must be one of its.
    [[nodiscard]] auto sequence(std::uint32_t sequence) const -> const nx::assets::StudioSequence&;

    /// Where its attachment `attachment` is, its bones posed as `bones` says (what pose gave); none
    /// when it has no such attachment.
    [[nodiscard]] auto attachmentAt(std::span<const nx::Mat3x4> bones, std::uint32_t attachment) const
        -> std::optional<nx::Vec3>;

private:
    ViewModel(std::unique_ptr<ModelFiles> files, std::vector<nx::assets::AnimationFrames> animations,
              bool isMirrored) noexcept;

    std::unique_ptr<ModelFiles> m_files;
    std::vector<nx::assets::AnimationFrames> m_animations; ///< Of each of its sequences, in their order.
    bool m_isMirrored;
};

/// The client events the view model passes, frame after frame, as the original's client plays them
/// (Xash3D's R_StudioClientEvents, GPL, consulted: docs/20 section 3): those of the sequence it plays
/// from the frame it was at in the last frame, or from before its first frame when a play starts.
class ViewModelEvents {
public:
    /// The client events the view model of `pose`, among `viewModels`, passed since the last call's
    /// pose; none without one.
    [[nodiscard]] auto passed(std::span<const ViewModel> viewModels, const std::optional<ViewModelPose>& pose)
        -> std::vector<const nx::assets::StudioEvent*>;

private:
    std::optional<ViewModelPose> m_lastPose; ///< The last call's.
};

} // namespace cs
