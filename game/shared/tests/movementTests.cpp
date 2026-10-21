// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Player movement through small worlds of planes: a floor, an edge, stairs, a wall, slopes and
// water. A standing hull here is the space a player's origin may take, the box already grown in.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.collision;
import cs.shared;

namespace {

using cs::Button;
using cs::MovementSettings;
using cs::MovementState;
using cs::UserCommand;
using cs::WaterLevel;
using nx::Plane;
using nx::Vec3;
using nx::collision::BrushHulls;
using nx::collision::Contents;
using nx::collision::HullChild;
using nx::collision::HullNode;
using nx::collision::HullNodeIndex;

constexpr float kTolerance = 1.0e-3F;
/// Where a trace stops before a plane: a player on a floor at z = 0 stands this high.
constexpr float kOnTheFloor = 0.03125F;
/// A command of the classic rate, 100 a second.
constexpr nx::Milliseconds kTick{10.0F};
/// The speed limit of a player with a knife.
constexpr nx::UnitsPerSecond kKnifeSpeed{250.0F};

[[nodiscard]] constexpr auto facingX(float x) -> Plane {
    return Plane{.normal = {.x = 1.0F}, .distance = x};
}

[[nodiscard]] constexpr auto facingUp(float z) -> Plane {
    return Plane{.normal = {.z = 1.0F}, .distance = z};
}

/// A world of `planes` whose standing, point and ducked hulls are the trees `standing`, `point` and
/// `ducked`, each from its own node 0, and empty where none is given. Each list of nodes gets an
/// unused last node, which keeps a root from being the list's last: movement takes such a hull for
/// an empty one.
[[nodiscard]] auto world(std::vector<Plane> planes, std::vector<HullNode> standing,
                         std::vector<HullNode> point = {}, std::vector<HullNode> ducked = {}) -> BrushHulls {
    const auto append = [](std::vector<HullNode>& nodes, std::vector<HullNode> tree) -> HullChild {
        if (tree.empty()) {
            return Contents::Empty;
        }
        const auto first = static_cast<std::uint32_t>(nodes.size());
        for (HullNode& node : tree) {
            for (HullChild& child : node.children) {
                if (auto* index = std::get_if<HullNodeIndex>(&child)) {
                    index->value += first;
                }
            }
        }
        nodes.insert(nodes.end(), tree.begin(), tree.end());
        return HullNodeIndex{first};
    };
    std::vector<HullNode> clipNodes;
    std::vector<HullNode> pointNodes;
    const HullChild standingRoot = append(clipNodes, std::move(standing));
    const HullChild duckedRoot = append(clipNodes, std::move(ducked));
    const HullChild pointRoot = append(pointNodes, std::move(point));
    const HullNode spare{.plane = 0, .children = {Contents::Empty, Contents::Empty}};
    clipNodes.push_back(spare);
    pointNodes.push_back(spare);
    auto hulls = BrushHulls::create(
        nx::collision::BrushHullsDesc{.planes = std::move(planes),
                                      .pointNodes = std::move(pointNodes),
                                      .clipNodes = std::move(clipNodes),
                                      .models = {{pointRoot, standingRoot, Contents::Empty, duckedRoot}}});
    EXPECT_TRUE(hulls.has_value());
    return std::move(hulls).value();
}

/// Solid below z = 0 for a standing player's origin, and below z = -18 for a ducked one's: a floor
/// that both boxes' feet stand on.
[[nodiscard]] auto floorWorld() -> BrushHulls {
    return world({facingUp(0.0F), facingUp(-18.0F)},
                 {HullNode{.plane = 0, .children = {Contents::Empty, Contents::Solid}}}, {},
                 {HullNode{.plane = 1, .children = {Contents::Empty, Contents::Solid}}});
}

/// Nothing at all.
[[nodiscard]] auto emptyWorld() -> BrushHulls {
    return world({facingUp(0.0F)}, {});
}

/// The floor up to x = 10, and a stair of `height` from there on.
[[nodiscard]] auto stairWorld(float height) -> BrushHulls {
    return world({facingX(10.0F), facingUp(height), facingUp(0.0F)},
                 {HullNode{.plane = 0, .children = {HullNodeIndex{1}, HullNodeIndex{2}}},
                  HullNode{.plane = 1, .children = {Contents::Empty, Contents::Solid}},
                  HullNode{.plane = 2, .children = {Contents::Empty, Contents::Solid}}});
}

/// Water up to `surface`, in a world without a floor.
[[nodiscard]] auto waterWorld(float surface) -> BrushHulls {
    return world({facingUp(surface)}, {},
                 {HullNode{.plane = 0, .children = {Contents::Empty, Contents::Water}}});
}

[[nodiscard]] auto standingOnTheFloor() -> MovementState {
    return MovementState{.origin = {.z = kOnTheFloor}, .maxSpeed = kKnifeSpeed, .isOnGround = true};
}

[[nodiscard]] auto command(cs::MoveIntent move = {}, nx::Flags<Button> buttons = {}) -> UserCommand {
    return UserCommand{.duration = kTick, .move = move, .buttons = buttons};
}

[[nodiscard]] auto forward(float speed) -> cs::MoveIntent {
    return cs::MoveIntent{.forward = nx::UnitsPerSecond{speed}};
}

/// `state` after `count` times `move`.
[[nodiscard]] auto repeat(MovementState state, const UserCommand& move, int count, const BrushHulls& hulls)
    -> MovementState {
    for (int index = 0; index < count; ++index) {
        state = cs::simulateMovement(state, move, MovementSettings{}, hulls);
    }
    return state;
}

[[nodiscard]] auto horizontalSpeed(Vec3 velocity) -> float {
    return std::hypot(velocity.x, velocity.y);
}

/// Velocity z after a jump from the ground with `penalty` left before the command: the jump's speed,
/// lowered by the penalty, then the two halves of gravity of the command.
[[nodiscard]] auto jumpSpeedAfterOneCommand(double penalty) -> double {
    constexpr double kJumpSpeed = 268.32815729997475; // the root of 2 * 800 * 45
    constexpr double kGravityPerCommand = 8.0;        // 800 * 0.01
    const double left = std::max(penalty - 10.0, 0.0);
    const double share = penalty > 0.0 ? (100.0 - left * 0.001 * 19.0) * 0.01 : 1.0;
    return kJumpSpeed * share - kGravityPerCommand;
}

TEST(MovementTest, APlayerStandingStillStays) {
    const MovementState after = cs::simulateMovement(standingOnTheFloor(), command(), {}, floorWorld());

    EXPECT_TRUE(after.isOnGround);
    EXPECT_EQ(after.origin, (Vec3{.z = kOnTheFloor}));
    EXPECT_EQ(after.velocity, Vec3{});
}

TEST(MovementTest, GravityPullsHalfBeforeTheMoveAndHalfAfter) {
    const MovementState after =
        cs::simulateMovement(MovementState{.origin = {.z = 100.0F}}, command(), {}, emptyWorld());

    EXPECT_FALSE(after.isOnGround);
    EXPECT_NEAR(after.velocity.z, -8.0F, kTolerance);
    EXPECT_NEAR(after.origin.z, 100.0F - 0.04F, kTolerance); // at the speed half a command's pull gives
}

TEST(MovementTest, WalkingAcceleratesTowardsTheSpeedLimit) {
    const BrushHulls hulls = floorWorld();
    const MovementState first =
        cs::simulateMovement(standingOnTheFloor(), command(forward(400.0F)), {}, hulls);

    // sv_accelerate 5 for a hundredth of a second, of a wish capped at the knife's 250.
    EXPECT_NEAR(first.velocity.x, 12.5F, kTolerance);
    EXPECT_NEAR(first.origin.x, 0.125F, kTolerance);

    const MovementState running = repeat(standingOnTheFloor(), command(forward(400.0F)), 200, hulls);
    EXPECT_NEAR(running.velocity.x, kKnifeSpeed.value(), kTolerance);
    EXPECT_TRUE(running.isOnGround);
    EXPECT_NEAR(running.origin.z, kOnTheFloor, kTolerance);
}

TEST(MovementTest, FrictionSlowsAndEdgeFrictionMoreNearADrop) {
    MovementState sliding = standingOnTheFloor();
    sliding.velocity = Vec3{.x = 100.0F};
    const MovementState onTheFloor = cs::simulateMovement(sliding, command(), {}, floorWorld());
    // The floor up to x = 10 only: a drop 16 units ahead of the player.
    const BrushHulls edge = world({facingX(10.0F), facingUp(0.0F)},
                                  {HullNode{.plane = 0, .children = {Contents::Empty, HullNodeIndex{1}}},
                                   HullNode{.plane = 1, .children = {Contents::Empty, Contents::Solid}}});
    const MovementState nearTheEdge = cs::simulateMovement(sliding, command(), {}, edge);

    EXPECT_NEAR(onTheFloor.velocity.x, 96.0F, kTolerance);  // sv_friction 4 over a hundredth of a second
    EXPECT_NEAR(nearTheEdge.velocity.x, 92.0F, kTolerance); // twice that: edgefriction 2

    sliding.velocity = Vec3{.x = 50.0F};
    EXPECT_NEAR(cs::simulateMovement(sliding, command(), {}, floorWorld()).velocity.x, 47.0F, kTolerance)
        << "slower than sv_stopspeed, friction takes what it takes at 75";
}

TEST(MovementTest, AJumpRisesAndCostsTheNextJumpAndTheWalk) {
    const MovementState jumped =
        cs::simulateMovement(standingOnTheFloor(), command({}, {Button::Jump}), {}, floorWorld());

    EXPECT_FALSE(jumped.isOnGround);
    EXPECT_TRUE(jumped.isJumpHeld);
    EXPECT_NEAR(jumped.velocity.z, jumpSpeedAfterOneCommand(0.0), kTolerance);
    EXPECT_EQ(jumped.jumpPenalty, nx::Milliseconds{1315.789429F});

    MovementState tired = standingOnTheFloor();
    tired.jumpPenalty = nx::Milliseconds{500.0F};
    EXPECT_NEAR(cs::simulateMovement(tired, command({}, {Button::Jump}), {}, floorWorld()).velocity.z,
                jumpSpeedAfterOneCommand(500.0), kTolerance);

    tired.velocity = Vec3{.x = 100.0F};
    const double share = (100.0 - 490.0 * 0.001 * 19.0) * 0.01;
    EXPECT_NEAR(cs::simulateMovement(tired, command(), {}, floorWorld()).velocity.x, 96.0 * share, kTolerance)
        << "friction, then the penalty";
}

TEST(MovementTest, HoldingJumpDoesNotJumpAgain) {
    MovementState held = standingOnTheFloor();
    held.isJumpHeld = true;
    const MovementState after = cs::simulateMovement(held, command({}, {Button::Jump}), {}, floorWorld());

    EXPECT_TRUE(after.isOnGround);
    EXPECT_EQ(after.velocity, Vec3{});
    EXPECT_TRUE(after.isJumpHeld);
    EXPECT_FALSE(cs::simulateMovement(held, command(), {}, floorWorld()).isJumpHeld) << "released";
}

TEST(MovementTest, AJumpFasterThanTheLimitIsSlowed) {
    MovementState fast = standingOnTheFloor();
    fast.velocity = Vec3{.x = 400.0F};
    const MovementState after = cs::simulateMovement(fast, command({}, {Button::Jump}), {}, floorWorld());

    // Over 1.2 times the knife's 250, slowed to 0.8 of that, without the ground's friction; the speed
    // it measures has the half of gravity the command pulled before the jump.
    const double slowed = 400.0 * (300.0 / std::hypot(400.0, 4.0)) * 0.8;
    EXPECT_NEAR(horizontalSpeed(after.velocity), slowed, kTolerance);
    EXPECT_FALSE(after.isOnGround);
}

TEST(MovementTest, APlayerWalksUpAStairButNotAWall) {
    MovementState walking = standingOnTheFloor();
    walking.origin.x = 5.0F;
    walking.velocity = Vec3{.x = kKnifeSpeed.value()};
    const UserCommand ahead = command(forward(kKnifeSpeed.value()));

    const MovementState stair = repeat(walking, ahead, 10, stairWorld(16.0F));
    EXPECT_NEAR(stair.origin.z, 16.0F + kOnTheFloor, kTolerance);
    EXPECT_GT(stair.origin.x, 10.0F);
    EXPECT_TRUE(stair.isOnGround);

    const MovementState wall = repeat(walking, ahead, 10, stairWorld(20.0F)); // over sv_stepsize 18
    EXPECT_NEAR(wall.origin.z, kOnTheFloor, kTolerance);
    EXPECT_LT(wall.origin.x, 10.0F);
}

TEST(MovementTest, AMoveIntoAWallSlidesAlongIt) {
    // A wall from x = 10 on, over the floor.
    const BrushHulls walled = world({facingX(10.0F), facingUp(0.0F)},
                                    {HullNode{.plane = 0, .children = {Contents::Solid, HullNodeIndex{1}}},
                                     HullNode{.plane = 1, .children = {Contents::Empty, Contents::Solid}}});
    MovementState diagonal = standingOnTheFloor();
    diagonal.origin.x = 9.0F;
    diagonal.velocity = Vec3{.x = 200.0F, .y = 200.0F};
    const MovementState after = cs::simulateMovement(diagonal, command(), {}, walled);

    EXPECT_EQ(after.velocity.x, 0.0F);
    EXPECT_NEAR(after.velocity.y, 192.0F, 0.01F); // what friction left of it
    EXPECT_NEAR(after.origin.x, 10.0F - kOnTheFloor, kTolerance);
    EXPECT_NEAR(after.origin.y, 1.92F, 0.01F);
}

TEST(MovementTest, ASteepSlopeIsNotGround) {
    const auto slope = [](Vec3 normal) {
        return world({Plane{.normal = normal}},
                     {HullNode{.plane = 0, .children = {Contents::Empty, Contents::Solid}}});
    };
    const MovementState above{.origin = {.z = 1.0F}, .maxSpeed = kKnifeSpeed};

    EXPECT_TRUE(cs::simulateMovement(above, command(), {}, slope({.x = 0.6F, .z = 0.8F})).isOnGround);

    const MovementState sliding = repeat(above, command(), 50, slope({.x = 0.8F, .z = 0.6F}));
    EXPECT_FALSE(sliding.isOnGround);
    EXPECT_GT(sliding.velocity.x, 0.0F) << "down the slope";
}

TEST(MovementTest, AirAccelerationStopsAtThirtyAlongTheWish) {
    const MovementState falling{.origin = {.z = 100.0F}, .maxSpeed = kKnifeSpeed};
    const UserCommand strafe = command(cs::MoveIntent{.side = kKnifeSpeed});

    // sv_airaccelerate 10 of the whole wish, 250, for a hundredth of a second; then capped at 30.
    EXPECT_NEAR(repeat(falling, strafe, 1, emptyWorld()).velocity.y, -25.0F, kTolerance);
    EXPECT_NEAR(repeat(falling, strafe, 3, emptyWorld()).velocity.y, -30.0F, kTolerance);

    MovementState fast = falling;
    fast.velocity = Vec3{.x = 300.0F};
    EXPECT_GT(horizontalSpeed(repeat(fast, strafe, 1, emptyWorld()).velocity), 300.0F)
        << "across the velocity, the cap does not hold the speed: air strafing";
}

TEST(MovementTest, VelocityIsHeldWithinTheMaximum) {
    const MovementState fast{.origin = {.z = 100.0F}, .velocity = {.x = 5000.0F}};

    EXPECT_EQ(cs::simulateMovement(fast, command(), {}, emptyWorld()).velocity.x, 2000.0F);
}

TEST(MovementTest, WaterLevelsAreTheFeetTheWaistAndTheEyes) {
    const MovementState swimmer{.maxSpeed = kKnifeSpeed};
    const auto levelIn = [&](float surface) {
        return cs::simulateMovement(swimmer, command(), {}, waterWorld(surface)).waterLevel;
    };

    EXPECT_EQ(levelIn(-50.0F), WaterLevel::Dry);
    EXPECT_EQ(levelIn(-20.0F), WaterLevel::Feet);
    EXPECT_EQ(levelIn(10.0F), WaterLevel::Waist);
    EXPECT_EQ(levelIn(30.0F), WaterLevel::Eyes);
    EXPECT_EQ(cs::simulateMovement(swimmer, command(), {}, waterWorld(30.0F)).waterType, Contents::Water);
}

TEST(MovementTest, JumpSwimsUpAndNothingHeldSinks) {
    const MovementState swimmer{.maxSpeed = kKnifeSpeed};
    const MovementState up =
        cs::simulateMovement(swimmer, command({}, {Button::Jump}), {}, waterWorld(30.0F));
    const MovementState down = cs::simulateMovement(swimmer, command(), {}, waterWorld(30.0F));

    EXPECT_NEAR(up.velocity.z, 96.0F, kTolerance); // 100, less water's friction
    EXPECT_FALSE(up.isOnGround);
    // A drift of 60, at 0.8 of it, accelerated at sv_accelerate 5 for a hundredth of a second.
    EXPECT_NEAR(down.velocity.z, -2.4F, kTolerance);
}

TEST(MovementTest, AWallAtTheSurfaceStartsAJumpOutOfTheWater) {
    // Water up to z = 10; from x = 20 on, a ledge up to z = 12.
    const BrushHulls pool = world({facingX(20.0F), facingUp(12.0F), facingUp(10.0F)}, {},
                                  {HullNode{.plane = 0, .children = {HullNodeIndex{1}, HullNodeIndex{2}}},
                                   HullNode{.plane = 1, .children = {Contents::Empty, Contents::Solid}},
                                   HullNode{.plane = 2, .children = {Contents::Empty, Contents::Water}}});
    const MovementState swimmer{.origin = {.x = 5.0F}, .maxSpeed = kKnifeSpeed};

    const MovementState starting = cs::simulateMovement(swimmer, command(), {}, pool);
    EXPECT_EQ(starting.waterJumpTime, nx::Milliseconds{2000.0F});
    EXPECT_EQ(starting.waterJumpVelocity, (Vec3{.x = 50.0F}));
    EXPECT_GT(starting.velocity.z, 200.0F);

    const MovementState climbing = cs::simulateMovement(starting, command(), {}, pool);
    EXPECT_EQ(climbing.waterJumpTime, nx::Milliseconds{1990.0F});
    EXPECT_EQ(climbing.velocity.x, 50.0F);
}

/// A ducked player on the floor of floorWorld.
[[nodiscard]] auto duckedOnTheFloor() -> MovementState {
    MovementState ducked = standingOnTheFloor();
    ducked.origin.z = kOnTheFloor - 18.0F;
    ducked.viewHeight = cs::kDuckedViewHeight;
    ducked.isDucked = true;
    ducked.isDuckHeld = true;
    return ducked;
}

TEST(MovementTest, ADuckOnTheGroundTakesFourTenthsOfASecond) {
    const BrushHulls hulls = floorWorld();
    const UserCommand duck = command({}, {Button::Duck});

    const MovementState started = repeat(standingOnTheFloor(), duck, 2, hulls);
    EXPECT_TRUE(started.isDucking);
    EXPECT_FALSE(started.isDucked);
    EXPECT_LT(started.viewHeight, cs::kStandingViewHeight) << "the eyes ease down";
    EXPECT_EQ(started.origin.z, kOnTheFloor);

    EXPECT_FALSE(repeat(standingOnTheFloor(), duck, 40, hulls).isDucked);
    const MovementState down = repeat(standingOnTheFloor(), duck, 41, hulls);
    EXPECT_TRUE(down.isDucked);
    EXPECT_FALSE(down.isDucking);
    EXPECT_TRUE(down.isOnGround);
    EXPECT_EQ(down.viewHeight, cs::kDuckedViewHeight);
    EXPECT_NEAR(down.origin.z, kOnTheFloor - 18.0F, kTolerance) << "the feet stay on the floor";
}

TEST(MovementTest, ADuckInTheAirIsAtOnce) {
    const MovementState falling{.origin = {.z = 100.0F}, .maxSpeed = kKnifeSpeed};
    const MovementState after = cs::simulateMovement(falling, command({}, {Button::Duck}), {}, emptyWorld());

    EXPECT_TRUE(after.isDucked);
    EXPECT_EQ(after.viewHeight, cs::kDuckedViewHeight);
    EXPECT_NEAR(after.origin.z, 100.0F - 0.04F, kTolerance) << "the feet rise into the ducked box";
}

TEST(MovementTest, ADuckedPlayerMovesAtAThirdOfTheSpeed) {
    const MovementState walking =
        repeat(duckedOnTheFloor(), command(forward(400.0F), {Button::Duck}), 200, floorWorld());

    EXPECT_TRUE(walking.isDucked);
    EXPECT_NEAR(walking.velocity.x, 250.0F * 0.333F, kTolerance);
}

TEST(MovementTest, StandingUpNeedsRoom) {
    const MovementState standing = cs::simulateMovement(duckedOnTheFloor(), command(), {}, floorWorld());
    EXPECT_FALSE(standing.isDucked);
    EXPECT_EQ(standing.viewHeight, cs::kStandingViewHeight);
    EXPECT_NEAR(standing.origin.z, kOnTheFloor, kTolerance);
    EXPECT_TRUE(standing.isOnGround);

    // No room to stand anywhere, the floor of the ducked box as in floorWorld.
    const BrushHulls tunnel =
        world({facingUp(-18.0F)}, {HullNode{.plane = 0, .children = {Contents::Solid, Contents::Solid}}}, {},
              {HullNode{.plane = 0, .children = {Contents::Empty, Contents::Solid}}});
    const MovementState stuck = cs::simulateMovement(duckedOnTheFloor(), command(), {}, tunnel);
    EXPECT_TRUE(stuck.isDucked);
    EXPECT_NEAR(stuck.origin.z, kOnTheFloor - 18.0F, kTolerance);
}

TEST(MovementTest, LettingGoOfADuckBeforeItIsDownHops) {
    const BrushHulls hulls = floorWorld();
    const MovementState pressed =
        cs::simulateMovement(standingOnTheFloor(), command({}, {Button::Duck}), {}, hulls);
    const MovementState released = cs::simulateMovement(pressed, command(), {}, hulls);

    // The ducktap: the standing box is raised as if it had been ducked, and falls from there.
    EXPECT_FALSE(released.isDucking);
    EXPECT_FALSE(released.isOnGround);
    EXPECT_NEAR(released.origin.z, kOnTheFloor + 18.0F - 0.04F, kTolerance);
}

} // namespace
