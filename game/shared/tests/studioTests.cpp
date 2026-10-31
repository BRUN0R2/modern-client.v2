// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Studio bones posed at a frame: rest values, animation between frames, a parent's chain, bone
// controllers and the mouth, two blends, the removed motion and the placement.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.collision;
import cs.shared;

namespace {

using nx::Mat3x4;
using nx::Vec3;
using nx::assets::AnimationFrames;
using nx::assets::BoneAxis;
using nx::assets::BoneController;
using nx::assets::BoneControllerIndex;
using nx::assets::BoneIndex;
using nx::assets::SequenceIndex;
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

TEST(SequenceFrameTest, TheClientEstimatesAFrameFromTheOneTheServerSent) {
    const StudioSequence idle{.label = "idle", .fps = 30.0F, .isLooping = true, .frameCount = 11};
    // 128 of 256: half of its ten frames after its first, then 0.1 seconds on.
    EXPECT_FLOAT_EQ(cs::estimatedFrameOf(idle, 128.0F, nx::Seconds{0.1F}, 1.0F).value(), 8.0F);
    EXPECT_FLOAT_EQ(cs::estimatedFrameOf(idle, 128.0F, nx::Seconds{0.2F}, 1.0F).value(), 1.0F) << "wrapped";
    EXPECT_FLOAT_EQ(cs::estimatedFrameOf(idle, 128.0F, nx::Seconds{-0.2F}, 1.0F).value(), 5.0F)
        << "nothing passed before it was sent";
    const StudioSequence draw{.label = "draw", .fps = 30.0F, .frameCount = 11};
    EXPECT_NEAR(cs::estimatedFrameOf(draw, 255.0F, nx::Seconds{0.5F}, 1.0F).value(), 9.999F, kTolerance);
}

TEST(SequenceFrameTest, TheServerPosesAFrameAsItsShareOfTheSequence) {
    const StudioSequence idle{.label = "idle", .fps = 30.0F, .isLooping = true, .frameCount = 11};
    EXPECT_FLOAT_EQ(cs::serverFrameOf(idle, 128.0F).value(), 5.0F);
    EXPECT_FLOAT_EQ(cs::serverFrameOf(idle, 255.0F).value(), 10.0F * 255.0F / 256.0F) << "short of its last";
    const StudioSequence still{.label = "still", .fps = 30.0F, .frameCount = 1};
    EXPECT_FLOAT_EQ(cs::serverFrameOf(still, 200.0F).value(), 0.0F);
}

TEST(ActivityTest, ASequenceOfTheActivityIsDrawnAsLikelyAsItsWeight) {
    const std::array sequences{
        StudioSequence{.label = "walk", .activity = 3, .activityWeight = 100},
        StudioSequence{.label = "idle1", .activity = 1, .activityWeight = 50},
        StudioSequence{.label = "idle3", .activity = 1, .activityWeight = 0},
        StudioSequence{.label = "idle7", .activity = 1, .activityWeight = 10},
    };
    std::array<int, 4> drawn{};
    for (std::uint64_t seed = 0; seed < 6000; ++seed) {
        nx::Random random{seed, "activity"};
        const std::optional<SequenceIndex> sequence =
            cs::lookupActivity(sequences, cs::Activity::Idle, random);
        ASSERT_TRUE(sequence.has_value());
        ++drawn.at(sequence->value);
    }
    EXPECT_EQ(drawn[0], 0) << "of another activity";
    EXPECT_EQ(drawn[2], 0) << "a weight of nothing is never drawn among weights";
    EXPECT_NEAR(drawn[1], 5000, 150) << "50 of 60";
    EXPECT_NEAR(drawn[3], 1000, 150) << "10 of 60";
}

TEST(ActivityTest, SequencesThatWeighNothingAreDrawnAlike) {
    const std::array sequences{
        StudioSequence{.label = "idle1", .activity = 1},
        StudioSequence{.label = "idle2", .activity = 1},
    };
    std::array<int, 2> drawn{};
    for (std::uint64_t seed = 0; seed < 2000; ++seed) {
        nx::Random random{seed, "activity"};
        ++drawn.at(cs::lookupActivity(sequences, cs::Activity::Idle, random).value_or(SequenceIndex{}).value);
    }
    EXPECT_NEAR(drawn[0], 1000, 120);
    EXPECT_NEAR(drawn[1], 1000, 120);
    nx::Random random{0, "activity"};
    EXPECT_FALSE(
        cs::lookupActivity(std::span<const StudioSequence>{}, cs::Activity::Idle, random).has_value())
        << "none of the activity";
}

TEST(AnimationTest, ASequenceStartsAtItsOwnSpeed) {
    cs::Animation animation{.frame = 40.0F, .framerate = 2.0F, .isFinished = true};
    const StudioSequence idle{.label = "idle", .fps = 30.0F, .isLooping = true, .frameCount = 11};
    const nx::GameTime now = nx::GameTime::atTick(7);
    cs::resetSequence(animation, idle, now);
    EXPECT_FLOAT_EQ(animation.frameSpeed, 30.0F * 256.0F / 10.0F);
    EXPECT_FLOAT_EQ(animation.framerate, 1.0F);
    EXPECT_TRUE(animation.isLooping);
    EXPECT_FALSE(animation.isFinished);
    EXPECT_EQ(animation.advancedAt, now);
    EXPECT_FLOAT_EQ(animation.frame, 40.0F) << "where it is is left";
    const StudioSequence still{.label = "still", .fps = 30.0F, .frameCount = 1};
    cs::resetSequence(animation, still, now);
    EXPECT_FLOAT_EQ(animation.frameSpeed, 256.0F) << "a sequence of one frame";
}

TEST(AnimationTest, AFrameAdvancesByTheTimeSinceAndWrapsOrStops) {
    const nx::TickRate rate = nx::kDefaultTickRate;
    const StudioSequence idle{.label = "idle", .fps = 30.0F, .isLooping = true, .frameCount = 11};
    cs::Animation animation{};
    cs::resetSequence(animation, idle, nx::GameTime{});
    cs::advanceFrame(animation, nx::GameTime::atTick(10), rate);
    EXPECT_NEAR(animation.frame, 76.8F, 0.001F) << "0.1 seconds at 768 a second";
    EXPECT_EQ(animation.advancedAt, nx::GameTime::atTick(10));
    cs::advanceFrame(animation, nx::GameTime::atTick(10, 0.05F), rate);
    EXPECT_NEAR(animation.frame, 76.8F, 0.001F) << "a thousandth of a second or less does not advance it";
    EXPECT_EQ(animation.advancedAt, nx::GameTime::atTick(10, 0.05F));
    cs::advanceFrame(animation, nx::GameTime::atTick(40, 0.05F), rate);
    EXPECT_NEAR(animation.frame, 76.8F + 230.4F - 256.0F, 0.01F) << "wrapped";
    EXPECT_TRUE(animation.isFinished);

    const StudioSequence draw{.label = "draw", .fps = 30.0F, .frameCount = 11};
    cs::Animation drawn{};
    cs::resetSequence(drawn, draw, nx::GameTime{});
    cs::advanceFrame(drawn, nx::GameTime::atTick(100), rate);
    EXPECT_FLOAT_EQ(drawn.frame, 255.0F) << "past its end, at 255";
    EXPECT_TRUE(drawn.isFinished);
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

TEST(StudioHitboxTest, AHitboxIsPlacedByItsBone) {
    const std::vector bones{nx::affine(nx::Mat3{}, {.x = 1.0F}), nx::affine(nx::Mat3{}, {.y = 2.0F})};
    const std::vector hitboxes{
        nx::assets::Hitbox{
            .bone = BoneIndex{1}, .group = 1, .bounds = {.min = {.x = -1.0F}, .max = {.x = 1.0F}}},
        nx::assets::Hitbox{.bone = BoneIndex{0}, .group = 2, .bounds = {.max = {.z = 3.0F}}}};

    const std::vector<nx::collision::PlacedBox> placed = cs::placedHitboxesOf(hitboxes, bones);
    ASSERT_EQ(placed.size(), 2U);
    EXPECT_EQ(placed[0].placement, bones[1]);
    EXPECT_EQ(placed[0].bounds, hitboxes[0].bounds);
    EXPECT_EQ(placed[1].placement, bones[0]);
    EXPECT_EQ(placed[1].bounds, hitboxes[1].bounds);
}

TEST(StudioHitboxTest, OverTheHeadIsTheMiddleOfItsHitboxesAcrossAtTheirHighestCornerAsTheyArePosed) {
    // The head's bone 40 up, rolled a quarter turn: its box, flat along Y, stands up along Z.
    const std::vector bones{nx::affine(nx::Mat3{}, {}),
                            nx::affine(nx::rotationMatrix(nx::EulerAngles{.roll = nx::Degrees{90.0F}}),
                                       {.x = 2.0F, .y = 3.0F, .z = 40.0F})};
    std::vector hitboxes{nx::assets::Hitbox{.bone = BoneIndex{1},
                                            .group = 1,
                                            .bounds = {.min = {.x = -2.0F, .y = -5.0F, .z = -1.0F},
                                                       .max = {.x = 2.0F, .y = 5.0F, .z = 1.0F}}},
                         // An arm raised past it.
                         nx::assets::Hitbox{.bone = BoneIndex{0},
                                            .group = 4,
                                            .bounds = {.min = {.x = -1.0F, .y = -1.0F},
                                                       .max = {.x = 1.0F, .y = 1.0F, .z = 60.0F}}}};

    const std::optional<Vec3> head = cs::headTopOf(hitboxes, cs::placedHitboxesOf(hitboxes, bones));
    ASSERT_TRUE(head.has_value());
    expectNear(*head, {.x = 2.0F, .y = 3.0F, .z = 45.0F});
    // A model with none of a head's: over all of its hitboxes.
    hitboxes[0].group = 2;
    const std::optional<Vec3> body = cs::headTopOf(hitboxes, cs::placedHitboxesOf(hitboxes, bones));
    ASSERT_TRUE(body.has_value());
    expectNear(*body, {.x = 1.5F, .y = 1.5F, .z = 60.0F});
    EXPECT_FALSE(cs::headTopOf({}, {}).has_value());
}

} // namespace
