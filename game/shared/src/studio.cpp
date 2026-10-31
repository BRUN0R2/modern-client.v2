// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.math;
import nx.assets;

namespace cs {

namespace {

using nx::Quat;
using nx::Vec3;
using nx::assets::BoneAxis;
using nx::assets::BoneController;
using nx::assets::StudioBone;

// A sequence's motion type: the axes it moves the entity along, whose motion its motion bone's
// position then leaves out (STUDIO_X, STUDIO_Y, STUDIO_Z).
constexpr std::uint32_t kMotionX = 0x1;
constexpr std::uint32_t kMotionY = 0x2;
constexpr std::uint32_t kMotionZ = 0x4;

/// The channel of the mouth: a bone controller of this channel or above follows the mouth.
constexpr std::uint32_t kMouthChannel = 4;
/// The mouth opens fully at this setting.
constexpr double kMouthFullyOpen = 64.0;
/// The highest setting of a controller, and the settings of a turn of a cyclic one.
constexpr double kControllerTop = 255.0;
constexpr double kControllerTurn = 256.0;
constexpr double kDegreesPerTurn = 360.0;
constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;
/// The highest blending.
constexpr float kBlendingTop = 255.0F;

/// Beyond this, the spherical interpolation of two rotations divides by too small a sine: the
/// original interpolates their quaternions linearly instead.
constexpr double kNearlyParallel = 0.000001;

/// SetController: a range this much wider than its start is a whole turn or more, and the turn and
/// half turn it brings a value within.
constexpr double kWholeTurnLess = 359.0;
constexpr float kWholeTurn = 360.0F;
constexpr float kHalfTurn = 180.0F;

/// How far short of its last frame a sequence that does not loop stops.
constexpr double kShortOfTheLastFrame = 1.001;
/// StudioFrameAdvance leaves a frame where it is when less time than this passed, or as much.
constexpr double kLeastFrameAdvance = 0.001;
/// Where a frame that does not loop stops past its sequence's end: its last whole 256th.
constexpr float kLastFrameOfScale = 255.0F;

/// The position and the rotation of every bone of a pose, in its parent's space.
struct LocalPose {
    std::vector<Vec3> positions;
    std::vector<Quat> rotations;
};

[[nodiscard]] auto isRotation(BoneAxis axis) noexcept -> bool {
    return axis == BoneAxis::RotationX || axis == BoneAxis::RotationY || axis == BoneAxis::RotationZ;
}

/// What each bone controller adds to its axis, by its index (R_StudioCalcBoneAdj): the controller's
/// setting mapped from 0-255 onto its range (a cyclic one turning past its end), the mouth's from
/// 0-64; in radians for a rotation. Each value is rounded to float as the original rounds it.
[[nodiscard]] auto adjustmentsOf(std::span<const BoneController> controllers,
                                 const ControllerSettings& settings) -> std::vector<float> {
    std::vector<float> adjustments;
    adjustments.reserve(controllers.size());
    for (const BoneController& controller : controllers) {
        const double start = double{controller.start};
        const double end = double{controller.end};
        float value = 0.0F;
        if (controller.channel >= kMouthChannel) {
            value = std::min(static_cast<float>(static_cast<double>(settings.mouth) / kMouthFullyOpen), 1.0F);
            value = static_cast<float>(((1.0 - double{value}) * start) + (double{value} * end));
        } else if (const std::uint8_t setting = settings.channels.at(controller.channel);
                   controller.isCyclic) {
            value = static_cast<float>((static_cast<double>(setting) * (kDegreesPerTurn / kControllerTurn)) +
                                       start);
        } else {
            value = std::clamp(static_cast<float>(static_cast<double>(setting) / kControllerTop), 0.0F, 1.0F);
            value = static_cast<float>(((1.0 - double{value}) * start) + (double{value} * end));
        }
        adjustments.push_back(
            isRotation(controller.axis) ? static_cast<float>(double{value} * kRadiansPerDegree) : value);
    }
    return adjustments;
}

/// The original's QuaternionSlerp: `q` taken to `p`'s side of the sphere first, by which of p - q and
/// p + q is shorter; then spherical interpolation, or linear when the two are nearly the same; the
/// result not normalized. The alignment leaves their dot at 0 or more, so they are never opposite.
[[nodiscard]] auto slerpAsTheOriginal(Quat p, Quat q, float t) noexcept -> Quat {
    const std::array pComponents{p.x, p.y, p.z, p.w};
    const std::array qComponents{q.x, q.y, q.z, q.w};
    float apart = 0.0F;
    float together = 0.0F;
    for (std::size_t index = 0; index < pComponents.size(); ++index) {
        apart +=
            (pComponents.at(index) - qComponents.at(index)) * (pComponents.at(index) - qComponents.at(index));
        together +=
            (pComponents.at(index) + qComponents.at(index)) * (pComponents.at(index) + qComponents.at(index));
    }
    if (apart > together) {
        q = -q;
    }
    const float cosine = (p.x * q.x) + (p.y * q.y) + (p.z * q.z) + (p.w * q.w);
    float weightP = 1.0F - t;
    float weightQ = t;
    if (1.0 - double{cosine} > kNearlyParallel) {
        const auto angle = static_cast<float>(std::acos(double{cosine}));
        const auto sine = static_cast<float>(std::sin(double{angle}));
        weightP = static_cast<float>(std::sin((1.0 - double{t}) * double{angle}) / double{sine});
        weightQ = static_cast<float>(std::sin(double{t} * double{angle}) / double{sine});
    }
    return Quat{.x = (weightP * p.x) + (weightQ * q.x),
                .y = (weightP * p.y) + (weightQ * q.y),
                .z = (weightP * p.z) + (weightQ * q.z),
                .w = (weightP * p.w) + (weightQ * q.w)};
}

/// The rotation of a bone's three angles (radians about X, Y and Z).
[[nodiscard]] auto rotationOf(const std::array<float, 3>& angles) noexcept -> Quat {
    return Quat::fromRotations(nx::Radians{angles[0]}, nx::Radians{angles[1]}, nx::Radians{angles[2]});
}

/// The positions, then the angles, of a bone's axes.
constexpr std::array kPositionAxes{BoneAxis::X, BoneAxis::Y, BoneAxis::Z};
constexpr std::array kAngleAxes{BoneAxis::RotationX, BoneAxis::RotationY, BoneAxis::RotationZ};

/// The bones posed at `frame` of `blend` of `animation`, `fraction` of the way to the next frame,
/// each axis's rest value plus its value times its scale, plus what its controller adds: a position
/// (R_StudioCalcBonePosition) with its two values interpolated first, when they differ; the angles
/// (R_StudioCalcBoneQuaterion) at both frames, then their rotations interpolated, when they differ.
[[nodiscard]] auto localPoseOf(const Skeleton& skeleton, const nx::assets::AnimationFrames& animation,
                               std::uint32_t blend, std::uint32_t frame, float fraction,
                               std::span<const float> adjustments) -> LocalPose {
    const std::uint32_t next = std::min(frame + 1, animation.frameCount - 1); // the last frame has no next
    LocalPose pose;
    pose.positions.reserve(skeleton.bones.size());
    pose.rotations.reserve(skeleton.bones.size());
    for (std::uint32_t index = 0; index < skeleton.bones.size(); ++index) {
        const StudioBone& bone = skeleton.bones[index];
        const nx::assets::BoneIndex boneIndex{index};
        // A value plus what the controller of its axis adds, when it has one.
        const auto adjusted = [&](float value, BoneAxis axis) {
            const std::optional<nx::assets::BoneControllerIndex> controller =
                bone.controllers.at(std::to_underlying(axis));
            return controller ? value + adjustments[controller->value] : value;
        };
        const auto valueAt = [&](std::uint32_t at, BoneAxis axis) {
            return static_cast<float>(animation.value(blend, at, boneIndex, axis));
        };
        std::array<float, kPositionAxes.size()> position{};
        for (std::size_t slot = 0; slot < kPositionAxes.size(); ++slot) {
            const BoneAxis axis = kPositionAxes.at(slot);
            const float from = valueAt(frame, axis);
            const float to = valueAt(next, axis);
            const float value = from == to ? from : (from * (1.0F - fraction)) + (fraction * to);
            const auto at = std::to_underlying(axis);
            position.at(slot) = adjusted(bone.values.at(at) + (value * bone.scales.at(at)), axis);
        }
        std::array<float, kAngleAxes.size()> angles{};
        std::array<float, kAngleAxes.size()> nextAngles{};
        for (std::size_t slot = 0; slot < kAngleAxes.size(); ++slot) {
            const BoneAxis axis = kAngleAxes.at(slot);
            const auto at = std::to_underlying(axis);
            angles.at(slot) =
                adjusted(bone.values.at(at) + (valueAt(frame, axis) * bone.scales.at(at)), axis);
            nextAngles.at(slot) =
                adjusted(bone.values.at(at) + (valueAt(next, axis) * bone.scales.at(at)), axis);
        }
        pose.positions.push_back(Vec3{.x = position[0], .y = position[1], .z = position[2]});
        pose.rotations.push_back(
            angles == nextAngles ? rotationOf(angles)
                                 : slerpAsTheOriginal(rotationOf(angles), rotationOf(nextAngles), fraction));
    }
    return pose;
}

/// `pose` moved `share` of the way to `other` (R_StudioSlerpBones), `share` held to 0-1.
void blendPoses(LocalPose& pose, const LocalPose& other, float share) {
    const float s = std::clamp(share, 0.0F, 1.0F);
    const float rest = 1.0F - s;
    for (std::size_t bone = 0; bone < pose.positions.size(); ++bone) {
        pose.rotations[bone] = slerpAsTheOriginal(pose.rotations[bone], other.rotations[bone], s);
        const Vec3 from = pose.positions[bone];
        const Vec3 to = other.positions[bone];
        pose.positions[bone] = Vec3{.x = (from.x * rest) + (to.x * s),
                                    .y = (from.y * rest) + (to.y * s),
                                    .z = (from.z * rest) + (to.z * s)};
    }
}

/// The names of the bones of a player's model its gait does not play from (Bip01 Spine) and plays from
/// again the children of (Bip01 Pelvis).
constexpr std::string_view kSpineBone = "Bip01 Spine";
constexpr std::string_view kPelvisBone = "Bip01 Pelvis";
/// Where the aim's blends turn from their first half to their second, and how the halves are counted.
constexpr double kBlendMiddle = 127.0;
constexpr double kBlendHalves = 2.0;

/// The transform of a bone, its rotation `q` and its position `position` in its parent's space, as the
/// original's QuaternionMatrix builds it: each entry in float, in its order.
[[nodiscard]] auto boneTransformOf(Quat q, Vec3 position) noexcept -> nx::Mat3x4 {
    constexpr float kTwo = 2.0F;
    return nx::Mat3x4{.rows = {
                          nx::Vec4{.x = 1.0F - (kTwo * q.y * q.y) - (kTwo * q.z * q.z),
                                   .y = (kTwo * q.x * q.y) - (kTwo * q.w * q.z),
                                   .z = (kTwo * q.x * q.z) + (kTwo * q.w * q.y),
                                   .w = position.x},
                          nx::Vec4{.x = (kTwo * q.x * q.y) + (kTwo * q.w * q.z),
                                   .y = 1.0F - (kTwo * q.x * q.x) - (kTwo * q.z * q.z),
                                   .z = (kTwo * q.y * q.z) - (kTwo * q.w * q.x),
                                   .w = position.y},
                          nx::Vec4{.x = (kTwo * q.x * q.z) - (kTwo * q.w * q.y),
                                   .y = (kTwo * q.y * q.z) + (kTwo * q.w * q.x),
                                   .z = 1.0F - (kTwo * q.x * q.x) - (kTwo * q.y * q.y),
                                   .w = position.z},
                      }};
}

/// The bones of `skeleton` in `pose`, each after its parent, as the reader checked: placed in it, or the
/// roots in `placement`.
[[nodiscard]] auto placedBonesOf(const Skeleton& skeleton, const LocalPose& pose, const nx::Mat3x4& placement)
    -> std::vector<nx::Mat3x4> {
    std::vector<nx::Mat3x4> transforms;
    transforms.reserve(skeleton.bones.size());
    for (std::size_t bone = 0; bone < skeleton.bones.size(); ++bone) {
        const std::optional<nx::assets::BoneIndex> parent = skeleton.bones[bone].parent;
        const nx::Mat3x4& placedIn = parent ? transforms[parent->value] : placement;
        transforms.push_back(placedIn * boneTransformOf(pose.rotations[bone], pose.positions[bone]));
    }
    return transforms;
}

/// The whole frame of `frame` of `animation` and its fraction, as the original splits it; a frame
/// outside the sequence's is a broken contract, held to it.
[[nodiscard]] auto splitFrame(const nx::assets::AnimationFrames& animation, SequenceFrame frame) noexcept
    -> std::pair<std::uint32_t, float> {
    const float last = static_cast<float>(animation.frameCount - 1);
    float value = frame.value();
    if (!(value >= 0.0F && value <= last)) {
        nx::expects(false, "a frame of the sequence");
        value = value > last ? last : 0.0F; // not a number too
    }
    const auto whole = static_cast<std::uint32_t>(value);
    return {whole, value - static_cast<float>(whole)};
}

} // namespace

auto lookupSequence(std::span<const nx::assets::StudioSequence> sequences, std::string_view label)
    -> std::optional<nx::assets::SequenceIndex> {
    const auto found = std::ranges::find_if(sequences, [label](const nx::assets::StudioSequence& sequence) {
        return nx::equalsIgnoringCase(sequence.label, label);
    });
    if (found == sequences.end()) {
        return std::nullopt;
    }
    return nx::assets::SequenceIndex{nx::narrow<std::uint32_t>(found - sequences.begin())};
}

auto skeletonOf(const nx::assets::MdlView& model) noexcept -> Skeleton {
    return Skeleton{.bones = model.bones(), .controllers = model.boneControllers()};
}

auto sequenceFrameAt(const nx::assets::StudioSequence& sequence, nx::Seconds elapsed,
                     float framerate) noexcept -> SequenceFrame {
    return estimatedFrameOf(sequence, 0.0F, elapsed, framerate);
}

auto estimatedFrameOf(const nx::assets::StudioSequence& sequence, float frame, nx::Seconds elapsed,
                      float framerate) noexcept -> SequenceFrame {
    if (sequence.frameCount <= 1) {
        return SequenceFrame{0.0F};
    }
    const auto lastFrames = static_cast<float>(sequence.frameCount - 1);
    const auto last = static_cast<double>(lastFrames);
    // The frame sent, of the sequence in single precision, then what passed since, in double.
    double estimated = static_cast<double>(frame * lastFrames / kFrameScale) +
                       (std::max(static_cast<double>(elapsed.count()), 0.0) * static_cast<double>(framerate) *
                        static_cast<double>(sequence.fps));
    if (sequence.isLooping) {
        estimated -= std::trunc(estimated / last) * last;
        if (estimated < 0.0) {
            estimated += last;
        }
    } else {
        estimated =
            std::clamp(estimated, 0.0, static_cast<double>(sequence.frameCount) - kShortOfTheLastFrame);
    }
    return SequenceFrame{static_cast<float>(estimated)};
}

auto serverFrameOf(const nx::assets::StudioSequence& sequence, float frame) noexcept -> SequenceFrame {
    return SequenceFrame{
        sequence.frameCount > 1 ? static_cast<float>(sequence.frameCount - 1) * frame / kFrameScale : 0.0F};
}

auto lookupActivity(std::span<const nx::assets::StudioSequence> sequences, Activity activity,
                    nx::Random& random) -> std::optional<nx::assets::SequenceIndex> {
    const std::int32_t wanted = std::to_underlying(activity);
    const auto isOfIt = [wanted](const nx::assets::StudioSequence& sequence) {
        return sequence.activity == wanted;
    };
    std::int64_t weights = 0;
    std::uint64_t count = 0;
    for (const nx::assets::StudioSequence& sequence : sequences | std::views::filter(isOfIt)) {
        weights += sequence.activityWeight;
        ++count;
    }
    if (count == 0) {
        return std::nullopt;
    }
    if (weights == 0) {
        std::uint64_t chosen = random.below(count);
        for (std::size_t index = 0; index < sequences.size(); ++index) {
            if (isOfIt(sequences[index]) && chosen-- == 0) {
                return nx::assets::SequenceIndex{nx::narrow<std::uint32_t>(index)};
            }
        }
        return std::nullopt;
    }
    const auto drawn =
        weights > 0 ? static_cast<std::int64_t>(random.below(static_cast<std::uint64_t>(weights))) : 0;
    std::int64_t passed = 0;
    for (std::size_t index = 0; index < sequences.size(); ++index) {
        if (isOfIt(sequences[index])) {
            passed += sequences[index].activityWeight;
            if (passed > drawn) {
                return nx::assets::SequenceIndex{nx::narrow<std::uint32_t>(index)};
            }
        }
    }
    return std::nullopt;
}

void resetSequence(Animation& animation, const nx::assets::StudioSequence& sequence,
                   nx::GameTime now) noexcept {
    animation.frameSpeed = sequence.frameCount <= 1
                               ? kFrameScale
                               : sequence.fps * kFrameScale / static_cast<float>(sequence.frameCount - 1);
    animation.isLooping = sequence.isLooping;
    animation.advancedAt = now;
    animation.framerate = 1.0F;
    animation.isFinished = false;
}

void advanceFrame(Animation& animation, nx::GameTime now, nx::TickRate rate) noexcept {
    const float interval = now.since(animation.advancedAt, rate).count();
    animation.advancedAt = now;
    if (static_cast<double>(interval) <= kLeastFrameAdvance) {
        return;
    }
    animation.frame += interval * animation.frameSpeed * animation.framerate;
    if (animation.frame < 0.0F || animation.frame >= kFrameScale) {
        if (animation.isLooping) {
            const auto scale = static_cast<double>(kFrameScale);
            animation.frame =
                static_cast<float>(static_cast<double>(animation.frame) -
                                   (std::trunc(static_cast<double>(animation.frame) / scale) * scale));
        } else {
            animation.frame = animation.frame < 0.0F ? 0.0F : kLastFrameOfScale;
        }
        animation.isFinished = true;
    }
}

auto poseBones(const Skeleton& skeleton, const nx::assets::StudioSequence& sequence,
               const nx::assets::AnimationFrames& animation, const PoseQuery& query)
    -> std::vector<nx::Mat3x4> {
    nx::expects(animation.boneCount == skeleton.bones.size() && animation.frameCount == sequence.frameCount &&
                    animation.blendCount == sequence.blendCount,
                "poseBones: the animation of the sequence, for the skeleton's bones");
    const auto [whole, fraction] = splitFrame(animation, query.frame);
    const std::vector<float> adjustments = adjustmentsOf(skeleton.controllers, query.controllers);

    LocalPose pose = localPoseOf(skeleton, animation, 0, whole, fraction, adjustments);
    if (animation.blendCount > 1) {
        const LocalPose second = localPoseOf(skeleton, animation, 1, whole, fraction, adjustments);
        blendPoses(pose, second, static_cast<float>(query.blending[0]) / kBlendingTop);
    }
    if (query.motion == SequenceMotion::Removed && sequence.motionBone.value < pose.positions.size()) {
        Vec3& motion = pose.positions[sequence.motionBone.value];
        motion.x = (sequence.motionType & kMotionX) != 0 ? 0.0F : motion.x;
        motion.y = (sequence.motionType & kMotionY) != 0 ? 0.0F : motion.y;
        motion.z = (sequence.motionType & kMotionZ) != 0 ? 0.0F : motion.z;
    }

    return placedBonesOf(skeleton, pose, query.placement);
}

auto posePlayerBones(const nx::assets::MdlView& model,
                     std::span<const nx::assets::AnimationFrames> animations,
                     nx::assets::SequenceIndex sequence, const PlayerPoseQuery& query)
    -> std::vector<nx::Mat3x4> {
    const std::span<const nx::assets::StudioSequence> sequences = model.sequences();
    nx::expects(animations.size() == sequences.size(),
                "posePlayerBones: the animations of the model's sequences");
    // As the original bounds them: a sequence the model lacks is its first.
    if (sequence.value >= sequences.size()) {
        sequence = nx::assets::SequenceIndex{};
    }
    const Skeleton skeleton = skeletonOf(model);
    const nx::assets::AnimationFrames& animation = animations[sequence.value];
    const auto [whole, fraction] = splitFrame(animation, query.frame);
    // A player's controllers are none: their adjustments are each one's at 0.
    const std::vector<float> adjustments = adjustmentsOf(skeleton.controllers, ControllerSettings{});
    const auto poseOf = [&](std::uint32_t blend) {
        return localPoseOf(skeleton, animation, blend, whole, fraction, adjustments);
    };
    LocalPose pose;
    if (animation.blendCount != kAimBlends) {
        pose = poseOf(0);
        if (animation.blendCount > 1) {
            blendPoses(pose, poseOf(1), static_cast<float>(query.blending) / kBlendingTop);
        }
    } else {
        // The four blends around the aim, three by three, and each blend's share of its half, in double.
        double yaw = static_cast<double>(query.yawBlend);
        double pitch = static_cast<double>(query.pitchBlend);
        const std::uint32_t right = yaw <= kBlendMiddle ? 0 : 1;
        const std::uint32_t down = pitch <= kBlendMiddle ? 0 : 1;
        yaw = right == 0 ? yaw * kBlendHalves : kBlendHalves * (yaw - kBlendMiddle);
        pitch = down == 0 ? pitch * kBlendHalves : kBlendHalves * (pitch - kBlendMiddle);
        constexpr std::uint32_t kRow = 3;
        const std::uint32_t first = right + (down * kRow);
        pose = poseOf(first);
        const LocalPose second = poseOf(first + 1);
        LocalPose third = poseOf(first + kRow);
        const LocalPose fourth = poseOf(first + kRow + 1);
        const auto yawShare = static_cast<float>(yaw / double{kBlendingTop});
        const auto pitchShare = static_cast<float>(pitch / double{kBlendingTop});
        blendPoses(pose, second, yawShare);
        blendPoses(third, fourth, yawShare);
        blendPoses(pose, third, pitchShare);
    }
    const bool playsGait = animation.blendCount == kAimBlends && sequence.value < kFirstDeathSequence.value &&
                           !std::ranges::contains(kSwimSequences, sequence);
    if (playsGait) {
        const nx::assets::SequenceIndex gait =
            query.gaitSequence.value < sequences.size() ? query.gaitSequence : nx::assets::SequenceIndex{};
        const nx::assets::AnimationFrames& gaitAnimation = animations[gait.value];
        // The gait's frame wraps at its sequence's frame count: the part past its last frame stays on it.
        const auto lastGaitFrame = static_cast<float>(gaitAnimation.frameCount - 1);
        const auto [gaitWhole, gaitFraction] =
            splitFrame(gaitAnimation, SequenceFrame{std::min(query.gaitFrame.value(), lastGaitFrame)});
        const LocalPose legs = localPoseOf(skeleton, gaitAnimation, 0, gaitWhole, gaitFraction, adjustments);
        bool isCopied = true;
        for (std::size_t bone = 0; bone < skeleton.bones.size(); ++bone) {
            const std::optional<nx::assets::BoneIndex> parent = skeleton.bones[bone].parent;
            if (skeleton.bones[bone].name == kSpineBone) {
                isCopied = false;
            } else if (parent && skeleton.bones[parent->value].name == kPelvisBone) {
                isCopied = true;
            }
            if (isCopied) {
                pose.positions[bone] = legs.positions[bone];
                pose.rotations[bone] = legs.rotations[bone];
            }
        }
    }
    return placedBonesOf(skeleton, pose, query.placement);
}

auto mergeBones(const nx::assets::MdlView& model, const nx::assets::AnimationFrames& firstSequence,
                const Skeleton& ontoSkeleton, std::span<const nx::Mat3x4> onto, const nx::Mat3x4& placement)
    -> std::vector<nx::Mat3x4> {
    nx::expects(onto.size() == ontoSkeleton.bones.size(), "mergeBones: the other model's bones posed");
    const Skeleton skeleton = skeletonOf(model);
    const std::vector<float> adjustments = adjustmentsOf(skeleton.controllers, ControllerSettings{});
    const LocalPose pose = localPoseOf(skeleton, firstSequence, 0, 0, 0.0F, adjustments);
    std::vector<nx::Mat3x4> transforms;
    transforms.reserve(skeleton.bones.size());
    for (std::size_t bone = 0; bone < skeleton.bones.size(); ++bone) {
        const auto same = std::ranges::find(ontoSkeleton.bones, skeleton.bones[bone].name, &StudioBone::name);
        if (same != ontoSkeleton.bones.end()) {
            transforms.push_back(onto[static_cast<std::size_t>(same - ontoSkeleton.bones.begin())]);
            continue;
        }
        const std::optional<nx::assets::BoneIndex> parent = skeleton.bones[bone].parent;
        const nx::Mat3x4& placedIn = parent ? transforms[parent->value] : placement;
        transforms.push_back(placedIn * boneTransformOf(pose.rotations[bone], pose.positions[bone]));
    }
    return transforms;
}

auto controllerSettingOf(const BoneController& controller, float value) noexcept -> std::uint8_t {
    if (isRotation(controller.axis)) {
        if (controller.end < controller.start) {
            value = -value;
        }
        if (static_cast<double>(controller.end) > static_cast<double>(controller.start) + kWholeTurnLess) {
            // A whole turn or more: into it.
            if (static_cast<double>(value) > kDegreesPerTurn) {
                value = static_cast<float>(
                    static_cast<double>(value) -
                    (std::trunc(static_cast<double>(value) / kDegreesPerTurn) * kDegreesPerTurn));
            } else if (value < 0.0F) {
                value = static_cast<float>(
                    static_cast<double>(value) +
                    (std::trunc((static_cast<double>(value) / -kDegreesPerTurn) + 1.0) * kDegreesPerTurn));
            }
        } else {
            // Within half a turn of the range's middle.
            const float middle = (controller.start + controller.end) / 2.0F;
            if (value > middle + kHalfTurn) {
                value -= kWholeTurn;
            }
            if (value < middle - kHalfTurn) {
                value += kWholeTurn;
            }
        }
    }
    const float setting =
        static_cast<float>(kControllerTop) * (value - controller.start) / (controller.end - controller.start);
    if (!(setting > 0.0F)) {
        return 0; // below the range, and not a number
    }
    return static_cast<std::uint8_t>(std::min(std::trunc(setting), static_cast<float>(kControllerTop)));
}

auto placedHitboxesOf(std::span<const nx::assets::Hitbox> hitboxes, std::span<const nx::Mat3x4> bones,
                      nx::Vec3 grownBy) -> std::vector<nx::collision::PlacedBox> {
    nx::expects(std::ranges::all_of(
                    hitboxes,
                    [&bones](const nx::assets::Hitbox& hitbox) { return hitbox.bone.value < bones.size(); }),
                "placedHitboxesOf: each hitbox on one of the bones");
    return hitboxes | std::views::transform([&bones, grownBy](const nx::assets::Hitbox& hitbox) {
               return nx::collision::PlacedBox{
                   .placement = bones[hitbox.bone.value], .bounds = hitbox.bounds, .grownBy = grownBy};
           }) |
           std::ranges::to<std::vector>();
}

auto headTopOf(std::span<const nx::assets::Hitbox> hitboxes, std::span<const nx::collision::PlacedBox> placed)
    -> std::optional<nx::Vec3> {
    nx::expects(hitboxes.size() == placed.size(), "headTopOf: a placed box of each hitbox");
    const auto isHead = [](const nx::assets::Hitbox& hitbox) {
        return HitGroup{hitbox.group} == HitGroup::Head;
    };
    const bool hasHead = std::ranges::any_of(hitboxes, isHead);
    std::optional<nx::Aabb> around;
    for (const auto& [hitbox, box] : std::views::zip(hitboxes, placed)) {
        if (hasHead && !isHead(hitbox)) {
            continue;
        }
        // Its eight corners as its bone turns them.
        const nx::Aabb& bounds = box.bounds;
        for (const float x : {bounds.min.x, bounds.max.x}) {
            for (const float y : {bounds.min.y, bounds.max.y}) {
                for (const float z : {bounds.min.z, bounds.max.z}) {
                    const nx::Vec3 corner = nx::transformPoint(box.placement, {.x = x, .y = y, .z = z});
                    around = around ? around->including(corner) : nx::Aabb{.min = corner, .max = corner};
                }
            }
        }
    }
    if (!around) {
        return std::nullopt;
    }
    const nx::Vec3 middle = around->center();
    return nx::Vec3{.x = middle.x, .y = middle.y, .z = around->max.z};
}

} // namespace cs
