// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The bones of a studio model, posed at a frame of one of its sequences (docs/05-rendering.md,
/// section 9.1): the transforms the client draws a model with and the server places its hitboxes by.
/// They are posed as the original's engine poses them (R_StudioCalcBoneQuaterion,
/// R_StudioCalcBonePosition, R_StudioCalcBoneAdj, R_StudioSlerpBones, SV_StudioSetupBones), as the
/// GPL code that reconstructs it describes them, written anew (docs/20-legal-and-licensing.md,
/// section 3): their order of steps and the precision of each sum.
///
/// How an entity plays its model's sequences, as the original's server plays them (LookupActivity,
/// ResetSequenceInfo, StudioFrameAdvance) and its client shows them (R_StudioEstimateFrame).
///
/// The players' models, posed as the original's server poses them (its SV_StudioSetupBones for players):
/// the nine aim blends of their sequences, by the yaw and the pitch of the aim, and their legs below the
/// spine playing their gait sequence.
export module cs.shared:studio;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.collision;

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

/// Where `sequence` is `elapsed` after it started, at `framerate` times its own speed (the entity's
/// `framerate`), as the original estimates it (R_StudioEstimateFrame, from its first frame), in double
/// precision: a looping sequence wraps at its last frame, which is its first again; another stops 0.001
/// short of its last; one of a single frame stays at it, and so does any before it started.
[[nodiscard]] auto sequenceFrameAt(const nx::assets::StudioSequence& sequence, nx::Seconds elapsed,
                                   float framerate) noexcept -> SequenceFrame;

/// What an entity's frame counts its sequence in (pev->frame): 0 is its first frame, this its last.
inline constexpr float kFrameScale = 256.0F;

/// Where `sequence` is `elapsed` after an entity showed it at `frame` (of kFrameScale), at `framerate`
/// times its own speed, as the original's client estimates it from what the server last sent
/// (R_StudioEstimateFrame): `frame` as a frame of the sequence, then on as sequenceFrameAt goes.
[[nodiscard]] auto estimatedFrameOf(const nx::assets::StudioSequence& sequence, float frame,
                                    nx::Seconds elapsed, float framerate) noexcept -> SequenceFrame;

/// Where `sequence` is at `frame` of an entity (of kFrameScale), as the server poses its bones
/// (SV_StudioSetupBones): `frame` as a frame of the sequence, in single precision; its first frame for a
/// sequence of one.
[[nodiscard]] auto serverFrameOf(const nx::assets::StudioSequence& sequence, float frame) noexcept
    -> SequenceFrame;

/// What the game plays a model's sequences for, by the original's numbers (ACT_*): those it asks for.
enum class Activity : std::int32_t {
    Idle = 1,          ///< ACT_IDLE.
    Walk = 3,          ///< ACT_WALK.
    Run = 4,           ///< ACT_RUN.
    Swim = 6,          ///< ACT_SWIM.
    Hop = 7,           ///< ACT_HOP: a jump up.
    Leap = 8,          ///< ACT_LEAP: a long jump forward.
    Crouch = 17,       ///< ACT_CROUCH.
    CrouchIdle = 18,   ///< ACT_CROUCHIDLE.
    SmallFlinch = 26,  ///< ACT_SMALL_FLINCH.
    RangeAttack1 = 28, ///< ACT_RANGE_ATTACK1.
    RangeAttack2 = 29, ///< ACT_RANGE_ATTACK2.
    Reload = 32,       ///< ACT_RELOAD.
    DieSimple = 36,    ///< ACT_DIESIMPLE.
    DieBackward = 37,  ///< ACT_DIEBACKWARD.
    DieForward = 38,   ///< ACT_DIEFORWARD.
    Hover = 50,        ///< ACT_HOVER: still in water.
    DieHeadshot = 66,  ///< ACT_DIE_HEADSHOT.
    DieChestshot = 67, ///< ACT_DIE_CHESTSHOT.
    DieGutshot = 68,   ///< ACT_DIE_GUTSHOT.
    DieBackshot = 69,  ///< ACT_DIE_BACKSHOT.
    Flinch = 77,       ///< ACT_FLINCH.
    LargeFlinch = 78,  ///< ACT_LARGE_FLINCH.
};

/// The sequence of `sequences` labelled `label`, its case ignored, as the original's LookupSequence
/// finds it: the first; none.
[[nodiscard]] auto lookupSequence(std::span<const nx::assets::StudioSequence> sequences,
                                  std::string_view label) -> std::optional<nx::assets::SequenceIndex>;

/// The sequence of `sequences` the game plays for `activity`, as the original's LookupActivity picks
/// it: of those of the activity, a weight drawn from `random` below their weights' sum, the first whose
/// weights up to it pass it, each as likely as its weight says; each as likely when they weigh nothing,
/// and the first's weight drawn when they weigh less (as RANDOM_LONG draws a range it cannot). None
/// when no sequence is of it, or none passes.
[[nodiscard]] auto lookupActivity(std::span<const nx::assets::StudioSequence> sequences, Activity activity,
                                  nx::Random& random) -> std::optional<nx::assets::SequenceIndex>;

/// How an entity plays its sequence, as the original's server keeps it (pev->sequence, frame,
/// framerate and animtime; CBaseAnimating's m_flFrameRate, m_fSequenceLoops and m_fSequenceFinished).
struct Animation {
    nx::assets::SequenceIndex sequence{};
    float frame = 0.0F;        ///< Where it is, of kFrameScale.
    float frameSpeed = 0.0F;   ///< How far its frame goes a second at its own speed (m_flFrameRate).
    float framerate = 1.0F;    ///< Times its own speed it plays at (pev->framerate).
    nx::GameTime advancedAt{}; ///< When its frame was last advanced (pev->animtime).
    bool isLooping = false;
    bool isFinished = false; ///< Its frame went past its sequence's end.
};

/// `animation` playing `sequence`, its own, from `now` (ResetSequenceInfo): its frame's speed the
/// sequence's (GetSequenceInfo: kFrameScale a second for a sequence of one frame, else its frames a
/// second over the frames after its first), looping as the sequence does, at its own speed, not
/// finished; its frame left where it is.
void resetSequence(Animation& animation, const nx::assets::StudioSequence& sequence,
                   nx::GameTime now) noexcept;

/// `animation`'s frame advanced to `now`, at `rate` (StudioFrameAdvance): by the time since it last was
/// at its frame's speed and its framerate, unless that is a thousandth of a second or less; past its
/// sequence's end it wraps if it loops, else it stays at 255 (0 before its start), and it is finished.
void advanceFrame(Animation& animation, nx::GameTime now, nx::TickRate rate) noexcept;

/// A studio model as the game plays it: its file, and the decoded frames of each of its sequences, by
/// index. Its owner keeps both.
struct PlayedModel {
    const nx::assets::MdlView* view = nullptr;
    std::span<const nx::assets::AnimationFrames> animations{};
};

/// The setting, from 0 to 255, that turns or moves `controller` by `value` (degrees for a rotation),
/// as the game sets an entity's controller (SetController): a rotation's value reversed for a range
/// that runs backward, brought into a range of a whole turn or more, else within half a turn of the
/// range's middle; then mapped onto the range, cut to a whole number and held to it.
[[nodiscard]] auto controllerSettingOf(const nx::assets::BoneController& controller, float value) noexcept
    -> std::uint8_t;

/// The bones of `skeleton` posed as `query` asks in `sequence`, whose decoded frames are `animation`:
/// each bone's rest values plus its animation at the frame, interpolated towards the next one by the
/// frame's fraction (the angles through quaternions), plus what its controllers add; the first two
/// blends mixed by the first blending; the motion bone kept or removed as `query.motion` says. The
/// transform of each bone, by its index, from its space to the space of `query.placement`'s parent.
[[nodiscard]] auto poseBones(const Skeleton& skeleton, const nx::assets::StudioSequence& sequence,
                             const nx::assets::AnimationFrames& animation, const PoseQuery& query)
    -> std::vector<nx::Mat3x4>;

/// The blends of a sequence of a player's model that aims (NUM_BLENDING): three of yaw by three of
/// pitch.
inline constexpr std::uint32_t kAimBlends = 9;
/// The first death sequence of a player's model, and its swimming ones: the legs of none of them play
/// the gait sequence (ANIM_FIRST_DEATH_SEQUENCE, ANIM_SWIM_1, ANIM_SWIM_2).
inline constexpr nx::assets::SequenceIndex kFirstDeathSequence{101};
inline constexpr std::array kSwimSequences{nx::assets::SequenceIndex{8}, nx::assets::SequenceIndex{9}};

/// A pose of a player's model: its sequence at a frame, its legs' gait sequence at a frame of its own,
/// the aim's yaw and pitch blends, each 0 to 255 (m_flYaw and m_flPitch, the yaw's with a fraction on the
/// server), and where it is: turned by the gait's yaw, not the view's.
struct PlayerPoseQuery {
    SequenceFrame frame{};
    nx::assets::SequenceIndex gaitSequence{};
    /// From 0 to the gait sequence's frame count, not reaching it (Gait::frame): the server's is the first
    /// (SV_StudioSetupBones). Its last frame, played towards none after it.
    SequenceFrame gaitFrame{};
    float yawBlend = 0.0F;
    float pitchBlend = 0.0F;
    nx::Mat3x4 placement{};
};

/// The bones of the player's `model`, whose decoded frames are `animations` by sequence, posed as
/// `query` asks in `sequence` (SV_StudioSetupBones for a player), its motion bone kept: a sequence of
/// nine blends (kAimBlends) by the four of them around the yaw's and the pitch's blends, the yaw's
/// shares first (0 to 127 between the left and the middle, 128 to 255 between the middle and the right,
/// as the pitch's between up, the middle and down); another by poseBones's blending, the first by the
/// yaw's. A sequence of nine blends below kFirstDeathSequence, but the swimming ones, has the bones from
/// the root to the spine and from each child of the pelvis on (its legs) play the gait sequence
/// instead.
[[nodiscard]] auto posePlayerBones(const nx::assets::MdlView& model,
                                   std::span<const nx::assets::AnimationFrames> animations,
                                   nx::assets::SequenceIndex sequence, const PlayerPoseQuery& query)
    -> std::vector<nx::Mat3x4>;

/// The bones of `model` drawn on another model's posed bones, `onto`, of `ontoSkeleton`, as the original's
/// engine merges the model of a player's weapon onto the player's (StudioMergeBones): a bone named as one
/// of the other's takes its transform; another is posed at the first frame of `model`'s first sequence,
/// whose decoded frames are `firstSequence`, in its parent, or in `placement` for a root.
[[nodiscard]] auto mergeBones(const nx::assets::MdlView& model,
                              const nx::assets::AnimationFrames& firstSequence, const Skeleton& ontoSkeleton,
                              std::span<const nx::Mat3x4> onto, const nx::Mat3x4& placement)
    -> std::vector<nx::Mat3x4>;

/// The `hitboxes` of a model on its bones posed as `bones` (poseBones's, by index), as the server places
/// them for its traces (R_StudioHull): each its bounds along its bone's axes.
[[nodiscard]] auto placedHitboxesOf(std::span<const nx::assets::Hitbox> hitboxes,
                                    std::span<const nx::Mat3x4> bones)
    -> std::vector<nx::collision::PlacedBox>;

} // namespace cs
