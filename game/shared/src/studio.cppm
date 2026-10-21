// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The bones of a studio model, posed at a frame of one of its sequences (docs/05-rendering.md,
/// section 9.1): the transforms the client draws a model with and the server places its hitboxes by.
/// They are posed as the original's engine poses them (R_StudioCalcBoneQuaterion,
/// R_StudioCalcBonePosition, R_StudioCalcBoneAdj, R_StudioSlerpBones, SV_StudioSetupBones), as the
/// GPL code that reconstructs it describes them, written anew (docs/20-legal-and-licensing.md,
/// section 3): their order of steps and the precision of each sum.
///
/// Today: one or two blends, the bone controllers and the mouth. The nine aim blends of the players'
/// models and their legs' gait sequence come with player animation (TODO.md).
export module cs.shared:studio;

import std;
import nx.core;
import nx.math;
import nx.assets;

export namespace cs {

/// A place in a sequence, counted in its frames from 0: the whole part is a frame, the fraction how
/// far the pose is towards the next.
using SequenceFrame = nx::Quantity<float, struct SequenceFrameTag>;

/// The entity's controllers the original sends with it, which a model's bone controllers follow by
/// their channel: four, then the mouth (channel 4 and above).
inline constexpr std::size_t kControllerChannels = 4;

/// The settings of an entity's bone controllers, each 0 to 255 as the original sends them.
struct ControllerSettings {
    std::array<std::uint8_t, kControllerChannels> channels{};
    std::uint8_t mouth = 0; ///< How far the mouth opens: fully from 64 on.
};

/// What becomes of the motion of a sequence's motion bone along the axes the sequence moves the
/// entity on (its motion type).
enum class SequenceMotion : std::uint8_t {
    Kept,    ///< The bone moves: the server places hitboxes so (SV_StudioSetupBones).
    Removed, ///< The bone stays: the client draws so, the entity moving instead.
};

/// A pose of a model.
struct PoseQuery {
    SequenceFrame frame{}; ///< From 0 to the sequence's last frame.
    ControllerSettings controllers{};
    /// The blends of a sequence of two or more, 0 to 255: the first mixes its first two blends.
    std::array<std::uint8_t, nx::assets::kSequenceBlendAxes> blending{};
    SequenceMotion motion = SequenceMotion::Removed;
    /// Where the model is, and how it is turned: what the root bones are placed in.
    nx::Mat3x4 placement{};
};

/// The bones of a model and the controllers that move them.
struct Skeleton {
    std::span<const nx::assets::StudioBone> bones{};
    std::span<const nx::assets::BoneController> controllers{};
};

/// The skeleton of `model`, a view of it.
[[nodiscard]] auto skeletonOf(const nx::assets::MdlView& model) noexcept -> Skeleton;

/// The bones of `skeleton` posed as `query` asks in `sequence`, whose decoded frames are `animation`:
/// each bone's rest values plus its animation at the frame, interpolated towards the next one by the
/// frame's fraction (the angles through quaternions), plus what its controllers add; the first two
/// blends mixed by the first blending; the motion bone kept or removed as `query.motion` says. The
/// transform of each bone, by its index, from its space to the space of `query.placement`'s parent.
[[nodiscard]] auto poseBones(const Skeleton& skeleton, const nx::assets::StudioSequence& sequence,
                             const nx::assets::AnimationFrames& animation, const PoseQuery& query)
    -> std::vector<nx::Mat3x4>;

} // namespace cs
