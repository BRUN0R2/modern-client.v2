// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Studio bones posed at a frame: rest values, animation between frames, a parent's chain, bone
// controllers and the mouth, two blends, the removed motion and the placement.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.assets;
import cs.shared;

namespace {

using nx::Mat3x4;
using nx::Vec3;
using nx::assets::AnimationFrames;
using nx::assets::BoneAxis;
using nx::assets::BoneController;
using nx::assets::BoneControllerIndex;
using nx::assets::BoneIndex;
using nx::assets::StudioBone;
using nx::assets::StudioSequence;

constexpr float kTolerance = 1e-5F;
constexpr float kQuarterTurn = std::numbers::pi_v<float> / 2.0F;

/// A bone at `position`, its every axis moving one unit a step of its animation.
[[nodiscard]] auto boneAt(Vec3 position, std::optional<BoneIndex> parent = std::nullopt) -> StudioBone {
    return StudioBone{.name = "bone",
                      .parent = parent,
                      .values = {position.x, position.y, position.z, 0.0F, 0.0F, 0.0F},
                      .scales = {1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F}};
}

/// The frames of `blends` blends of `frames` frames of `bones` bones, all 0 but what `set` writes.
[[nodiscard]] auto animationOf(std::uint32_t blends, std::uint32_t frames, std::uint32_t bones,
                               const std::function<void(AnimationFrames&)>& set = {}) -> AnimationFrames {
    AnimationFrames animation{
        .blendCount = blends,
        .frameCount = frames,
        .boneCount = bones,
        .values = std::vector<std::int16_t>(std::size_t{blends} * frames * bones * nx::assets::kBoneAxes)};
    if (set) {
        set(animation);
    }
    return animation;
}

/// Sets the value of `axis` of `bone` at `frame` of `blend`.
void setValue(AnimationFrames& animation, std::uint32_t blend, std::uint32_t frame, std::uint32_t bone,
              BoneAxis axis, std::int16_t value) {
    const std::size_t index =
        ((((std::size_t{blend} * animation.frameCount) + frame) * animation.boneCount + bone) *
         nx::assets::kBoneAxes) +
        std::to_underlying(axis);
    animation.values[index] = value;
}

[[nodiscard]] auto sequenceOf(const AnimationFrames& animation) -> StudioSequence {
    return StudioSequence{
        .label = "test", .frameCount = animation.frameCount, .blendCount = animation.blendCount};
}

[[nodiscard]] auto originOf(const Mat3x4& transform) -> Vec3 {
    return nx::translationPart(transform);
}

void expectNear(Vec3 actual, Vec3 expected, std::string_view what = {}) {
    EXPECT_NEAR(actual.x, expected.x, kTolerance) << what;
    EXPECT_NEAR(actual.y, expected.y, kTolerance) << what;
    EXPECT_NEAR(actual.z, expected.z, kTolerance) << what;
}

TEST(SequenceFrameTest, ALoopingSequenceWrapsAtItsLastFrame) {
    const StudioSequence idle{.label = "idle", .fps = 30.0F, .isLooping = true, .frameCount = 11};
    EXPECT_FLOAT_EQ(cs::sequenceFrameAt(idle, nx::Seconds{0.1F}, 1.0F).value(), 3.0F);
    EXPECT_FLOAT_EQ(cs::sequenceFrameAt(idle, nx::Seconds{0.5F}, 1.0F).value(), 5.0F)
        << "15 frames: past 10, 5";
    EXPECT_FLOAT_EQ(cs::sequenceFrameAt(idle, nx::Seconds{-1.0F}, 1.0F).value(), 0.0F) << "not started";
}

TEST(SequenceFrameTest, ASequenceThatDoesNotLoopStopsShortOfItsLastFrame) {
    const StudioSequence draw{.label = "draw", .fps = 30.0F, .frameCount = 11};
    EXPECT_FLOAT_EQ(cs::sequenceFrameAt(draw, nx::Seconds{0.2F}, 1.0F).value(), 6.0F);
    EXPECT_NEAR(cs::sequenceFrameAt(draw, nx::Seconds{1.0F}, 1.0F).value(), 9.999F, kTolerance);
    const StudioSequence still{.label = "still", .fps = 30.0F, .isLooping = true, .frameCount = 1};
    EXPECT_FLOAT_EQ(cs::sequenceFrameAt(still, nx::Seconds{2.0F}, 1.0F).value(), 0.0F) << "a single frame";
}

TEST(SequenceFrameTest, TheEntitysFramerateScalesItsSpeed) {
    const StudioSequence idle{.label = "idle", .fps = 30.0F, .isLooping = true, .frameCount = 11};
    EXPECT_FLOAT_EQ(cs::sequenceFrameAt(idle, nx::Seconds{0.1F}, 2.0F).value(), 6.0F);
}

TEST(ControllerSettingTest, AValueIsMappedOntoItsControllersRangeAsTheGameSetsIt) {
    const BoneController head{.axis = BoneAxis::RotationZ, .start = -90.0F, .end = 90.0F};
    EXPECT_EQ(cs::controllerSettingOf(head, 0.0F), 127) << "the middle, 127.5 cut";
    EXPECT_EQ(cs::controllerSettingOf(head, 90.0F), 255);
    EXPECT_EQ(cs::controllerSettingOf(head, 200.0F), 0) << "200 is -160, within half a turn of 0, below -90";
    const BoneController backward{.axis = BoneAxis::RotationZ, .start = 90.0F, .end = -90.0F};
    EXPECT_EQ(cs::controllerSettingOf(backward, 45.0F), 191) << "reversed: -45 on 90 to -90";
    const BoneController turning{.axis = BoneAxis::RotationY, .start = 0.0F, .end = 360.0F};
    EXPECT_EQ(cs::controllerSettingOf(turning, -90.0F), 191) << "-90 is 270 of a whole turn";
    const BoneController sliding{.axis = BoneAxis::X, .start = 0.0F, .end = 10.0F};
    EXPECT_EQ(cs::controllerSettingOf(sliding, 20.0F), 255) << "held to the range";
}

TEST(StudioPoseTest, ABoneAtRestIsWhereItsValuesPutIt) {
    std::vector bones{boneAt({.x = 1.0F, .y = 2.0F, .z = 3.0F})};
    bones[0].values[std::to_underlying(BoneAxis::RotationZ)] = kQuarterTurn;
    const AnimationFrames animation = animationOf(1, 1, 1);

    const std::vector<Mat3x4> pose = cs::poseBones({.bones = bones}, sequenceOf(animation), animation, {});
    ASSERT_EQ(pose.size(), 1U);
    expectNear(originOf(pose[0]), {.x = 1.0F, .y = 2.0F, .z = 3.0F});
    expectNear(nx::transformDirection(pose[0], {.x = 1.0F}), {.y = 1.0F}, "a quarter turn about Z");
}

TEST(StudioPoseTest, AFractionOfAFrameInterpolatesTowardsTheNext) {
    const std::vector bones{boneAt({})};
    const AnimationFrames animation =
        animationOf(1, 2, 1, [](AnimationFrames& frames) { setValue(frames, 0, 1, 0, BoneAxis::X, 10); });

    const std::vector<Mat3x4> pose = cs::poseBones({.bones = bones}, sequenceOf(animation), animation,
                                                   {.frame = cs::SequenceFrame{0.25F}});
    expectNear(originOf(pose[0]), {.x = 2.5F});
    const std::vector<Mat3x4> last =
        cs::poseBones({.bones = bones}, sequenceOf(animation), animation, {.frame = cs::SequenceFrame{1.0F}});
    expectNear(originOf(last[0]), {.x = 10.0F}, "the last frame has no next");
}

TEST(StudioPoseTest, ABoneIsPlacedInItsParent) {
    std::vector bones{boneAt({.x = 10.0F}), boneAt({.y = 5.0F}, BoneIndex{0})};
    bones[0].values[std::to_underlying(BoneAxis::RotationZ)] = kQuarterTurn;
    const AnimationFrames animation = animationOf(1, 1, 2);

    const std::vector<Mat3x4> pose = cs::poseBones({.bones = bones}, sequenceOf(animation), animation,
                                                   {.placement = nx::affine(nx::Mat3{}, {.z = 100.0F})});
    expectNear(originOf(pose[0]), {.x = 10.0F, .z = 100.0F});
    expectNear(originOf(pose[1]), {.x = 5.0F, .z = 100.0F}, "its parent's quarter turn turns its +Y to -X");
}

TEST(StudioPoseTest, ControllersAddTheirSettingsAcrossTheirRanges) {
    const std::vector bones{[] {
        StudioBone bone = boneAt({});
        bone.controllers[std::to_underlying(BoneAxis::X)] = BoneControllerIndex{0};
        bone.controllers[std::to_underlying(BoneAxis::RotationZ)] = BoneControllerIndex{1};
        return bone;
    }()};
    const std::vector controllers{
        BoneController{
            .bone = BoneIndex{0}, .axis = BoneAxis::X, .start = -10.0F, .end = 10.0F, .channel = 0},
        BoneController{
            .bone = BoneIndex{0}, .axis = BoneAxis::RotationZ, .start = 0.0F, .end = 90.0F, .channel = 4}};
    const AnimationFrames animation = animationOf(1, 1, 1);

    const std::vector<Mat3x4> pose =
        cs::poseBones({.bones = bones, .controllers = controllers}, sequenceOf(animation), animation,
                      {.controllers = {.channels = {255, 0, 0, 0}, .mouth = 64}});
    expectNear(originOf(pose[0]), {.x = 10.0F}, "255: the end of the range");
    expectNear(nx::transformDirection(pose[0], {.x = 1.0F}), {.y = 1.0F}, "the mouth fully open: 90 degrees");

    const std::vector<Mat3x4> half =
        cs::poseBones({.bones = bones, .controllers = controllers}, sequenceOf(animation), animation,
                      {.controllers = {.channels = {0, 0, 0, 0}, .mouth = 32}});
    expectNear(originOf(half[0]), {.x = -10.0F});
    const float angle = std::atan2(nx::transformDirection(half[0], {.x = 1.0F}).y,
                                   nx::transformDirection(half[0], {.x = 1.0F}).x);
    EXPECT_NEAR(angle, kQuarterTurn / 2.0F, kTolerance) << "half open";
}

TEST(StudioPoseTest, ACyclicControllerTurnsPastItsStart) {
    const std::vector bones{[] {
        StudioBone bone = boneAt({});
        bone.controllers[std::to_underlying(BoneAxis::RotationZ)] = BoneControllerIndex{0};
        return bone;
    }()};
    const std::vector controllers{BoneController{
        .bone = BoneIndex{0}, .axis = BoneAxis::RotationZ, .isCyclic = true, .start = 90.0F, .end = 450.0F}};
    const AnimationFrames animation = animationOf(1, 1, 1);

    const std::vector<Mat3x4> pose =
        cs::poseBones({.bones = bones, .controllers = controllers}, sequenceOf(animation), animation,
                      {.controllers = {.channels = {128, 0, 0, 0}}});
    expectNear(nx::transformDirection(pose[0], {.x = 1.0F}), {.y = -1.0F}, "90 + 128 * 360 / 256 degrees");
}

TEST(StudioPoseTest, TheFirstBlendingMixesTheFirstTwoBlends) {
    const std::vector bones{boneAt({})};
    const AnimationFrames animation =
        animationOf(2, 1, 1, [](AnimationFrames& frames) { setValue(frames, 1, 0, 0, BoneAxis::Y, 51); });

    for (const auto& [blending, y] : {std::pair{std::uint8_t{0}, 0.0F}, std::pair{std::uint8_t{255}, 51.0F},
                                      std::pair{std::uint8_t{51}, 10.2F}}) {
        const std::vector<Mat3x4> pose =
            cs::poseBones({.bones = bones}, sequenceOf(animation), animation, {.blending = {blending, 0}});
        expectNear(originOf(pose[0]), {.y = y});
    }
}

TEST(StudioPoseTest, TheClientRemovesTheMotionOfTheMotionBoneAndTheServerKeepsIt) {
    const std::vector bones{boneAt({.x = 7.0F, .y = 8.0F})};
    const AnimationFrames animation = animationOf(1, 1, 1);
    StudioSequence sequence = sequenceOf(animation);
    sequence.motionType = 0x1; // STUDIO_X

    const std::vector<Mat3x4> removed = cs::poseBones({.bones = bones}, sequence, animation, {});
    expectNear(originOf(removed[0]), {.y = 8.0F});
    const std::vector<Mat3x4> kept =
        cs::poseBones({.bones = bones}, sequence, animation, {.motion = cs::SequenceMotion::Kept});
    expectNear(originOf(kept[0]), {.x = 7.0F, .y = 8.0F});
}

} // namespace
