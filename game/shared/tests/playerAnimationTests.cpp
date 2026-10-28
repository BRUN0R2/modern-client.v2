// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// How a player's model plays, as the original's CBasePlayer animates it: its torso's sequence by what it
// does and the weapon it holds, its flinches and its deaths by where it was met, its legs' gait sequence
// by how fast it moves and whether it ducks, the way its legs face, the aim's yaw and pitch blends; and
// its bones posed by the nine blends around the aim, its legs below the spine playing the gait. On a model
// of the shape of Counter-Strike's players'.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.assets;
import cs.shared;

#include "playerModel.hpp"

namespace {

using cs::Activity;
using cs::HitGroup;
using cs::PlayerAction;
using nx::test::playerSequenceIndex;

constexpr float kTolerance = 1e-3F;

/// A model of playerSequences, read, with the decoded frames of each of its sequences.
class PlayerModelTest : public ::testing::Test {
protected:
    PlayerModelTest() : m_bytes{nx::test::playerModelOf(nx::test::playerSequences())} {
        auto view = nx::assets::MdlView::parse(m_bytes);
        EXPECT_TRUE(view.has_value()) << (view ? "" : nx::describe(view.error()));
        m_view.emplace(std::move(view).value());
        for (std::uint32_t index = 0; index < m_view->sequences().size(); ++index) {
            auto frames = m_view->decodeAnimation(nx::assets::SequenceIndex{index}, m_bytes);
            EXPECT_TRUE(frames.has_value());
            m_animations.push_back(std::move(frames).value());
        }
    }

    [[nodiscard]] auto model() const -> const nx::assets::MdlView& { return *m_view; }
    [[nodiscard]] auto animations() const -> std::span<const nx::assets::AnimationFrames> {
        return m_animations;
    }

    /// What a player holding the knife plays by, on the ground, at `speed`.
    [[nodiscard]] auto standing(float speed = 0.0F) const -> cs::PlayerAnimationContext {
        return cs::PlayerAnimationContext{.model = &*m_view,
                                          .weaponAnimation = "knife",
                                          .isOnGround = true,
                                          .speed = nx::UnitsPerSecond{speed},
                                          .now = nx::GameTime::atTick(100)};
    }

    void play(cs::PlayerAnimation& animation, PlayerAction action,
              const cs::PlayerAnimationContext& context) {
        cs::setPlayerAnimation(animation, action, context, m_random);
    }

private:
    std::vector<std::byte> m_bytes;
    std::optional<nx::assets::MdlView> m_view;
    std::vector<nx::assets::AnimationFrames> m_animations;
    nx::Random m_random{1, "test"};
};

[[nodiscard]] auto sequence(std::string_view label) -> nx::assets::SequenceIndex {
    return nx::assets::SequenceIndex{playerSequenceIndex(label)};
}

TEST_F(PlayerModelTest, AStillPlayerAimsItsWeaponItsLegsIdle) {
    cs::PlayerAnimation animation;
    play(animation, PlayerAction::Idle, standing());
    EXPECT_EQ(animation.torso.sequence, sequence("ref_aim_knife"));
    EXPECT_EQ(animation.gaitSequence, sequence("idle1"));
    EXPECT_EQ(animation.activity, Activity::Walk);
}

TEST_F(PlayerModelTest, AMovingPlayerWalksThenRunsBeyond135UnitsASecond) {
    cs::PlayerAnimation animation;
    play(animation, PlayerAction::Walk, standing(100.0F));
    EXPECT_EQ(animation.gaitSequence, sequence("walk"));
    EXPECT_EQ(animation.torso.sequence, sequence("ref_aim_knife"));
    play(animation, PlayerAction::Walk, standing(200.0F));
    EXPECT_EQ(animation.gaitSequence, sequence("run"));
    EXPECT_EQ(animation.activity, Activity::Run);
    EXPECT_EQ(animation.torso.sequence, sequence("ref_aim_knife")) << "the model has no ref_run_knife";
}

TEST_F(PlayerModelTest, ADuckedPlayerAimsCrouchedItsLegsCrouching) {
    cs::PlayerAnimation animation;
    cs::PlayerAnimationContext ducked = standing();
    ducked.isDucked = true;
    play(animation, PlayerAction::Walk, ducked);
    EXPECT_EQ(animation.torso.sequence, sequence("crouch_aim_knife"));
    EXPECT_EQ(animation.gaitSequence, sequence("crouch_idle"));
    ducked.speed = nx::UnitsPerSecond{50.0F};
    play(animation, PlayerAction::Walk, ducked);
    EXPECT_EQ(animation.gaitSequence, sequence("crouchrun"));
}

TEST_F(PlayerModelTest, AShotPlaysFromItsStartAndTheTorsoKeepsItWhileItRuns) {
    cs::PlayerAnimation animation;
    play(animation, PlayerAction::Idle, standing());
    animation.torso.frame = 100.0F;
    play(animation, PlayerAction::Attack1, standing());
    EXPECT_EQ(animation.torso.sequence, sequence("ref_shoot_knife"));
    EXPECT_EQ(animation.torso.frame, 0.0F);
    EXPECT_EQ(animation.lastFired, standing().now);
    // Running within 4 seconds of it: the shot plays on, the legs run.
    play(animation, PlayerAction::Walk, standing(200.0F));
    EXPECT_EQ(animation.torso.sequence, sequence("ref_shoot_knife"));
    EXPECT_EQ(animation.gaitSequence, sequence("run"));
}

TEST_F(PlayerModelTest, AJumpPlaysTheHopUntilTheGroundIsMet) {
    cs::PlayerAnimation animation;
    play(animation, PlayerAction::Idle, standing());
    play(animation, PlayerAction::Jump, standing());
    EXPECT_EQ(animation.gaitSequence, sequence("jump"));
    EXPECT_EQ(animation.activity, Activity::Hop);
    cs::PlayerAnimationContext flying = standing(250.0F);
    flying.isOnGround = false;
    play(animation, PlayerAction::Walk, flying);
    EXPECT_EQ(animation.gaitSequence, sequence("jump")) << "in the air, it hops on";
    play(animation, PlayerAction::Walk, standing(250.0F));
    EXPECT_EQ(animation.gaitSequence, sequence("run"));
}

TEST_F(PlayerModelTest, ADeathPlaysADeathItsLegsNoGait) {
    cs::PlayerAnimation animation;
    play(animation, PlayerAction::Walk, standing(100.0F));
    play(animation, PlayerAction::Die, standing());
    const std::uint32_t played = animation.torso.sequence.value;
    EXPECT_GE(played, playerSequenceIndex("death1"));
    EXPECT_LE(played, playerSequenceIndex("right")) << "of a hit of no group";
    EXPECT_EQ(animation.gaitSequence, nx::assets::SequenceIndex{});
    EXPECT_FALSE(animation.torso.isLooping);

    cs::PlayerAnimation crouched;
    cs::PlayerAnimationContext ducked = standing();
    ducked.isDucked = true;
    play(crouched, PlayerAction::Die, ducked);
    EXPECT_EQ(crouched.torso.sequence, sequence("crouch_die"));
}

TEST_F(PlayerModelTest, APlayerDiesByWhereTheAttackMetIt) {
    for (const auto& [group, death] :
         {std::pair{HitGroup::Head, "head"}, std::pair{HitGroup::Chest, "chestshot"},
          std::pair{HitGroup::Stomach, "gutshot"}, std::pair{HitGroup::LeftArm, "left"},
          std::pair{HitGroup::RightArm, "right"}}) {
        cs::PlayerAnimation animation;
        cs::PlayerAnimationContext met = standing();
        met.lastHitGroup = std::to_underlying(group);
        play(animation, PlayerAction::Die, met);
        EXPECT_EQ(animation.torso.sequence, sequence(death)) << death;
        met.isDucked = true;
        cs::PlayerAnimation ducked;
        play(ducked, PlayerAction::Die, met);
        EXPECT_EQ(ducked.torso.sequence, sequence("crouch_die")) << "ducked, wherever it was met";
    }
    // In the legs: the simplest death, one of those of ACT_DIESIMPLE.
    cs::PlayerAnimation legs;
    cs::PlayerAnimationContext met = standing();
    met.lastHitGroup = std::to_underlying(HitGroup::LeftLeg);
    play(legs, PlayerAction::Die, met);
    EXPECT_EQ(model().sequences()[legs.torso.sequence.value].activity,
              std::to_underlying(Activity::DieSimple));
}

TEST_F(PlayerModelTest, AFlinchPlaysByWhereTheAttackMetItAndHoldsTheTorsoToItsEnd) {
    for (const auto& [group, flinch] :
         {std::pair{HitGroup::Head, "head_flinch"}, std::pair{HitGroup::Chest, "head_flinch"},
          std::pair{HitGroup::Stomach, "gut_flinch"}, std::pair{HitGroup::RightLeg, "gut_flinch"}}) {
        cs::PlayerAnimation animation;
        play(animation, PlayerAction::Idle, standing());
        cs::PlayerAnimationContext met = standing();
        met.lastHitGroup = std::to_underlying(group);
        for (const PlayerAction action : {PlayerAction::Flinch, PlayerAction::LargeFlinch}) {
            play(animation, action, met);
            EXPECT_EQ(animation.torso.sequence, sequence(flinch)) << flinch;
            EXPECT_EQ(animation.gaitSequence, sequence("idle1")) << "its legs play on";
        }
        EXPECT_EQ(animation.activity, Activity::LargeFlinch);
    }
    cs::PlayerAnimation generic;
    cs::PlayerAnimationContext met = standing();
    play(generic, PlayerAction::Flinch, met);
    EXPECT_TRUE(generic.torso.sequence == sequence("head_flinch") ||
                generic.torso.sequence == sequence("gut_flinch"))
        << "either for a hit of no group";

    // Running, the flinch plays to its end before the torso aims again; the legs run meanwhile.
    cs::PlayerAnimation running;
    play(running, PlayerAction::Walk, standing(250.0F));
    met.lastHitGroup = std::to_underlying(HitGroup::Head);
    play(running, PlayerAction::Flinch, met);
    play(running, PlayerAction::Walk, standing(250.0F));
    EXPECT_EQ(running.torso.sequence, sequence("head_flinch"));
    EXPECT_EQ(running.gaitSequence, sequence("run"));
    running.torso.isFinished = true;
    play(running, PlayerAction::Walk, standing(250.0F));
    EXPECT_EQ(running.torso.sequence, sequence("ref_aim_knife"));
    EXPECT_EQ(running.activity, Activity::Run);
}

TEST_F(PlayerModelTest, InDeepWaterAStillPlayerHoversAndAMovingOneSwims) {
    cs::PlayerAnimation animation;
    cs::PlayerAnimationContext water = standing();
    water.isOnGround = false;
    water.waterLevel = cs::WaterLevel::Waist;
    play(animation, PlayerAction::Walk, water);
    EXPECT_EQ(animation.torso.sequence, sequence("treadwater"));
    EXPECT_EQ(animation.gaitSequence, nx::assets::SequenceIndex{});
    water.speed = nx::UnitsPerSecond{100.0F};
    play(animation, PlayerAction::Walk, water);
    EXPECT_EQ(animation.torso.sequence, sequence("swim"));
}

TEST_F(PlayerModelTest, TheLegsFaceTheWayThePlayerMovesTheAimTurnedTowardsTheBody) {
    cs::Gait gait;
    cs::processGait(gait, model(), sequence("walk"), {.x = 10.0F}, {.yaw = nx::Degrees{90.0F}},
                    nx::Seconds{0.01F}, 1.0F);
    EXPECT_NEAR(gait.yaw, 0.0F, kTolerance);
    EXPECT_NEAR(gait.movement, 10.0F, kTolerance);
    // The body 90 degrees to the left of the legs: the yaw's blend at its end (0), the pitch's middle.
    EXPECT_NEAR(gait.yawBlend, 0.0F, kTolerance);
    EXPECT_EQ(gait.pitchBlend, 127);
    // The gait goes as far as the player: 10 of the walk's 100 units over its 10 frames.
    EXPECT_NEAR(gait.frame, 1.0F, kTolerance);
}

TEST_F(PlayerModelTest, MovingAwayFromWhereItFacesTurnsTheLegsAroundAndBackwards) {
    cs::Gait gait;
    cs::processGait(gait, model(), sequence("walk"), {.x = 10.0F}, {.yaw = nx::Degrees{180.0F}},
                    nx::Seconds{0.01F}, 1.0F);
    EXPECT_NEAR(gait.yaw, -180.0F, kTolerance);
    EXPECT_NEAR(gait.movement, -10.0F, kTolerance);
    EXPECT_NEAR(gait.yawBlend, 128.0F, kTolerance);
    EXPECT_NEAR(gait.frame, 9.0F, kTolerance) << "backwards, wrapped into the sequence";
}

TEST_F(PlayerModelTest, StillLegsTurnAfterTheBodySlowlyNearlyAheadFastFacingAway) {
    cs::Gait nearlyAhead;
    cs::processGait(nearlyAhead, model(), sequence("idle1"), {}, {.yaw = nx::Degrees{3.0F}},
                    nx::Seconds{0.01F}, 1.0F);
    EXPECT_EQ(nearlyAhead.yaw, 0.0F) << "a turn under a whole degree is none";
    EXPECT_EQ(nearlyAhead.yawModifier, 0.05F);
    cs::Gait away;
    cs::processGait(away, model(), sequence("idle1"), {}, {.yaw = nx::Degrees{100.0F}}, nx::Seconds{0.01F},
                    1.0F);
    EXPECT_NEAR(away.yaw, 3.5F, kTolerance) << "100 degrees times the frame times 3.5";
}

TEST_F(PlayerModelTest, ThePitchBlendRunsFrom45DegreesUpTo45Down) {
    const auto pitchBlendOf = [this](float viewPitch) {
        cs::Gait gait;
        cs::processGait(gait, model(), sequence("idle1"), {},
                        cs::bodyAnglesOf({.pitch = nx::Degrees{viewPitch}}), nx::Seconds{}, 1.0F);
        return gait.pitchBlend;
    };
    EXPECT_EQ(pitchBlendOf(0.0F), 127);
    EXPECT_EQ(pitchBlendOf(45.0F), 255) << "down";
    EXPECT_EQ(pitchBlendOf(-89.0F), 0) << "up";
    EXPECT_EQ(pitchBlendOf(-30.0F), 42);
    EXPECT_FLOAT_EQ(cs::bodyAnglesOf({.pitch = nx::Degrees{30.0F}}).pitch.value(), -10.0F);
}

class PlayerPoseTest : public PlayerModelTest {
protected:
    /// The bones of the sequence labelled `label` posed at its first frame by `yawBlend` and
    /// `pitchBlend`, the legs playing the idle.
    [[nodiscard]] auto posed(std::string_view label, float yawBlend, float pitchBlend) const
        -> std::vector<nx::Mat3x4> {
        return cs::posePlayerBones(model(), animations(), sequence(label),
                                   cs::PlayerPoseQuery{.gaitSequence = sequence("idle1"),
                                                       .yawBlend = yawBlend,
                                                       .pitchBlend = pitchBlend,
                                                       .placement = nx::Mat3x4{}});
    }

    /// Where the spine's own blends put it: its place less the gait's root's.
    [[nodiscard]] auto spineOf(std::string_view label, float yawBlend, float pitchBlend) const -> float {
        return posed(label, yawBlend, pitchBlend)
                   .at(std::to_underlying(nx::test::PlayerBone::Spine))
                   .rows[0]
                   .w -
               nx::test::kGaitRoot;
    }
};

TEST_F(PlayerPoseTest, TheNineBlendsAreMixedAroundTheAimsYawAndPitch) {
    EXPECT_NEAR(spineOf("ref_aim_knife", 0.0F, 0.0F), 0.0F, kTolerance);
    EXPECT_NEAR(spineOf("ref_aim_knife", 255.0F, 0.0F), 20.0F, kTolerance);
    EXPECT_NEAR(spineOf("ref_aim_knife", 255.0F, 255.0F), 80.0F, kTolerance);
    EXPECT_NEAR(spineOf("ref_aim_knife", 0.0F, 255.0F), 60.0F, kTolerance);
    // Halfway up the right half: between the middle (10) and the right (20).
    EXPECT_NEAR(spineOf("ref_aim_knife", 191.0F, 0.0F), 10.0F + (10.0F * 128.0F / 255.0F), kTolerance);
    // The middle of the yaw, halfway down: between the middle row's (40) and the lowest's (70).
    EXPECT_NEAR(spineOf("ref_aim_knife", 127.0F, 191.0F),
                30.0F + (10.0F * 254.0F / 255.0F) + (30.0F * 128.0F / 255.0F), kTolerance);
}

TEST_F(PlayerPoseTest, TheLegsBelowTheSpinePlayTheGait) {
    const std::vector<nx::Mat3x4> bones = posed("ref_aim_knife", 0.0F, 0.0F);
    EXPECT_NEAR(bones.at(std::to_underlying(nx::test::PlayerBone::Root)).rows[0].w, nx::test::kGaitRoot,
                kTolerance)
        << "the gait's root, not the aim's";
    EXPECT_NEAR(bones.at(std::to_underlying(nx::test::PlayerBone::Thigh)).rows[0].w,
                nx::test::kGaitRoot + nx::test::kGaitThigh, kTolerance);
}

TEST_F(PlayerPoseTest, ASwimmingSequencePlaysNoGait) {
    const std::vector<nx::Mat3x4> bones = cs::posePlayerBones(
        model(), animations(), sequence("swim"),
        cs::PlayerPoseQuery{.gaitSequence = sequence("idle1"), .placement = nx::Mat3x4{}});
    EXPECT_NEAR(bones.at(std::to_underlying(nx::test::PlayerBone::Root)).rows[0].w, 0.0F, kTolerance);
}

TEST_F(PlayerPoseTest, AMergedModelTakesTheBonesOfTheSameNames) {
    const std::vector<nx::Mat3x4> player = posed("ref_aim_knife", 255.0F, 255.0F);
    const std::vector<nx::Mat3x4> merged =
        cs::mergeBones(model(), animations().front(), cs::skeletonOf(model()), player, nx::Mat3x4{});
    ASSERT_EQ(merged.size(), player.size());
    for (std::size_t bone = 0; bone < player.size(); ++bone) {
        EXPECT_EQ(merged[bone], player[bone]) << bone;
    }
}

} // namespace
