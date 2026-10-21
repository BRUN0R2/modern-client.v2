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

} // namespace

auto skeletonOf(const nx::assets::MdlView& model) noexcept -> Skeleton {
    return Skeleton{.bones = model.bones(), .controllers = model.boneControllers()};
}

auto sequenceFrameAt(const nx::assets::StudioSequence& sequence, nx::Seconds elapsed,
                     float framerate) noexcept -> SequenceFrame {
    if (sequence.frameCount <= 1) {
        return SequenceFrame{0.0F};
    }
    const auto last = static_cast<double>(sequence.frameCount - 1);
    double frame = std::max(static_cast<double>(elapsed.count()), 0.0) * static_cast<double>(framerate) *
                   static_cast<double>(sequence.fps);
    if (sequence.isLooping) {
        frame -= std::trunc(frame / last) * last;
    } else {
        frame = std::min(frame, static_cast<double>(sequence.frameCount) - kShortOfTheLastFrame);
    }
    return SequenceFrame{static_cast<float>(frame)};
}

auto poseBones(const Skeleton& skeleton, const nx::assets::StudioSequence& sequence,
               const nx::assets::AnimationFrames& animation, const PoseQuery& query)
    -> std::vector<nx::Mat3x4> {
    nx::expects(animation.boneCount == skeleton.bones.size() && animation.frameCount == sequence.frameCount &&
                    animation.blendCount == sequence.blendCount,
                "poseBones: the animation of the sequence, for the skeleton's bones");
    const float last = static_cast<float>(animation.frameCount - 1);
    float frame = query.frame.value();
    if (!(frame >= 0.0F && frame <= last)) {
        nx::expects(false, "poseBones: a frame of the sequence");
        frame = frame > last ? last : 0.0F; // not a number too
    }
    // As the original splits it: the frame cut to a whole one, and its fraction.
    const auto whole = static_cast<std::uint32_t>(frame);
    const float fraction = frame - static_cast<float>(whole);
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

    // Each bone after its parent, as the reader checked: placed in it, or the roots in the placement.
    std::vector<nx::Mat3x4> transforms;
    transforms.reserve(skeleton.bones.size());
    for (std::size_t bone = 0; bone < skeleton.bones.size(); ++bone) {
        const std::optional<nx::assets::BoneIndex> parent = skeleton.bones[bone].parent;
        const nx::Mat3x4& placedIn = parent ? transforms[parent->value] : query.placement;
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

} // namespace cs
