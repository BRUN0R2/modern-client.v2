// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Player movement through small worlds of planes: a floor, an edge, stairs, a wall, slopes and
// water. A standing hull here is the space a player's origin may take, the box already grown in.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;
import nx.game;
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
using nx::collision::HullRoots;
using nx::collision::ModelIndex;

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

/// The trees of one brush model's standing, point and ducked hulls, each from its own node 0, and
/// empty where none is given; and the box of its faces.
struct ModelTrees {
    std::vector<HullNode> standing{};
    std::vector<HullNode> point{};
    std::vector<HullNode> ducked{};
    nx::Aabb bounds{};
};

/// Brush models of `planes`, the world first. Each list of nodes gets an unused last node, which
/// keeps a root from being the list's last: movement takes such a hull for an empty one.
[[nodiscard]] auto modelsOf(std::vector<Plane> planes, std::vector<ModelTrees> trees) -> BrushHulls {
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
    nx::collision::BrushHullsDesc desc{.planes = std::move(planes)};
    for (ModelTrees& model : trees) {
        const HullChild standingRoot = append(desc.clipNodes, std::move(model.standing));
        const HullChild duckedRoot = append(desc.clipNodes, std::move(model.ducked));
        const HullChild pointRoot = append(desc.pointNodes, std::move(model.point));
        desc.models.push_back(nx::collision::BrushModel{
            .hulls = HullRoots{{pointRoot, standingRoot, HullChild{Contents::Empty}, duckedRoot}},
            .bounds = model.bounds});
    }
    const HullNode spare{.plane = nx::collision::PlaneIndex{0},
                         .children = {Contents::Empty, Contents::Empty}};
    desc.clipNodes.push_back(spare);
    desc.pointNodes.push_back(spare);
    auto hulls = BrushHulls::create(std::move(desc));
    EXPECT_TRUE(hulls.has_value());
    return std::move(hulls).value();
}

/// A world of `planes` alone, its standing, point and ducked hulls the trees `standing`, `point`
/// and `ducked`.
[[nodiscard]] auto world(std::vector<Plane> planes, std::vector<HullNode> standing,
                         std::vector<HullNode> point = {}, std::vector<HullNode> ducked = {}) -> BrushHulls {
    return modelsOf(std::move(planes), {ModelTrees{.standing = std::move(standing),
                                                   .point = std::move(point),
                                                   .ducked = std::move(ducked)}});
}

/// Solid below z = 0 for a standing player's origin, and below z = -18 for a ducked one's: a floor
/// that both boxes' feet stand on.
[[nodiscard]] auto floorWorld() -> BrushHulls {
    return world({facingUp(0.0F), facingUp(-18.0F)},
                 {HullNode{.plane = {0}, .children = {Contents::Empty, Contents::Solid}}}, {},
                 {HullNode{.plane = {1}, .children = {Contents::Empty, Contents::Solid}}});
}

/// Nothing at all.
[[nodiscard]] auto emptyWorld() -> BrushHulls {
    return world({facingUp(0.0F)}, {});
}

/// The floor up to x = 10, and a stair of `height` from there on.
[[nodiscard]] auto stairWorld(float height) -> BrushHulls {
    return world({facingX(10.0F), facingUp(height), facingUp(0.0F)},
                 {HullNode{.plane = {0}, .children = {HullNodeIndex{1}, HullNodeIndex{2}}},
                  HullNode{.plane = {1}, .children = {Contents::Empty, Contents::Solid}},
                  HullNode{.plane = {2}, .children = {Contents::Empty, Contents::Solid}}});
}

/// Water up to `surface`, in a world without a floor.
[[nodiscard]] auto waterWorld(float surface) -> BrushHulls {
    return world({facingUp(surface)}, {},
                 {HullNode{.plane = {0}, .children = {Contents::Empty, Contents::Water}}});
}

/// The player after one command, as cs::simulateMovement leaves it.
[[nodiscard]] auto simulate(const MovementState& state, const UserCommand& command,
                            const MovementSettings& settings, const BrushHulls& hulls,
                            std::span<const cs::BrushEntity> entities = {}) -> MovementState {
    return cs::simulateMovement(state, command, settings, hulls, entities).state;
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
[[nodiscard]] auto repeat(MovementState state, const UserCommand& move, int count, const BrushHulls& hulls,
                          std::span<const cs::BrushEntity> entities = {}) -> MovementState {
    for (int index = 0; index < count; ++index) {
        state = simulate(state, move, MovementSettings{}, hulls, entities);
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
    const MovementState after = simulate(standingOnTheFloor(), command(), {}, floorWorld());

    EXPECT_TRUE(after.isOnGround);
    EXPECT_EQ(after.origin, (Vec3{.z = kOnTheFloor}));
    EXPECT_EQ(after.velocity, Vec3{});
}

TEST(MovementTest, AWeaponsKickDropsBackEachMove) {
    // PM_DropPunchAngle: the kick's length less 10 and half of itself a second.
    MovementState kicked = standingOnTheFloor();
    kicked.punch = nx::EulerAngles{.pitch = nx::Degrees{-3.0F}, .yaw = nx::Degrees{4.0F}};
    const MovementState after = simulate(kicked, command(), {}, floorWorld());
    const double length = 5.0 - ((10.0 + (5.0 * 0.5)) * 0.01);
    EXPECT_NEAR(after.punch.pitch.value(), -3.0 * length / 5.0, kTolerance);
    EXPECT_NEAR(after.punch.yaw.value(), 4.0 * length / 5.0, kTolerance);

    // Never past none.
    MovementState small = standingOnTheFloor();
    small.punch = nx::EulerAngles{.pitch = nx::Degrees{-0.05F}};
    EXPECT_EQ(simulate(small, command(), {}, floorWorld()).punch, nx::EulerAngles{});
}

TEST(MovementTest, GravityPullsHalfBeforeTheMoveAndHalfAfter) {
    const MovementState after = simulate(MovementState{.origin = {.z = 100.0F}}, command(), {}, emptyWorld());

    EXPECT_FALSE(after.isOnGround);
    EXPECT_NEAR(after.velocity.z, -8.0F, kTolerance);
    EXPECT_NEAR(after.origin.z, 100.0F - 0.04F, kTolerance); // at the speed half a command's pull gives
}

TEST(MovementTest, WalkingAcceleratesTowardsTheSpeedLimit) {
    const BrushHulls hulls = floorWorld();
    const MovementState first = simulate(standingOnTheFloor(), command(forward(400.0F)), {}, hulls);

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
    const MovementState onTheFloor = simulate(sliding, command(), {}, floorWorld());
    // The floor up to x = 10 only: a drop 16 units ahead of the player.
    const BrushHulls edge = world({facingX(10.0F), facingUp(0.0F)},
                                  {HullNode{.plane = {0}, .children = {Contents::Empty, HullNodeIndex{1}}},
                                   HullNode{.plane = {1}, .children = {Contents::Empty, Contents::Solid}}});
    const MovementState nearTheEdge = simulate(sliding, command(), {}, edge);

    EXPECT_NEAR(onTheFloor.velocity.x, 96.0F, kTolerance);  // sv_friction 4 over a hundredth of a second
    EXPECT_NEAR(nearTheEdge.velocity.x, 92.0F, kTolerance); // twice that: edgefriction 2

    sliding.velocity = Vec3{.x = 50.0F};
    EXPECT_NEAR(simulate(sliding, command(), {}, floorWorld()).velocity.x, 47.0F, kTolerance)
        << "slower than sv_stopspeed, friction takes what it takes at 75";
}

TEST(MovementTest, AJumpRisesAndCostsTheNextJumpAndTheWalk) {
    const MovementState jumped =
        simulate(standingOnTheFloor(), command({}, {Button::Jump}), {}, floorWorld());

    EXPECT_FALSE(jumped.isOnGround);
    EXPECT_TRUE(jumped.isJumpHeld);
    EXPECT_NEAR(jumped.velocity.z, jumpSpeedAfterOneCommand(0.0), kTolerance);
    EXPECT_EQ(jumped.jumpPenalty, nx::Milliseconds{1315.789429F});

    MovementState tired = standingOnTheFloor();
    tired.jumpPenalty = nx::Milliseconds{500.0F};
    EXPECT_NEAR(simulate(tired, command({}, {Button::Jump}), {}, floorWorld()).velocity.z,
                jumpSpeedAfterOneCommand(500.0), kTolerance);

    tired.velocity = Vec3{.x = 100.0F};
    const double share = (100.0 - 490.0 * 0.001 * 19.0) * 0.01;
    EXPECT_NEAR(simulate(tired, command(), {}, floorWorld()).velocity.x, 96.0 * share, kTolerance)
        << "friction, then the penalty";
}

TEST(MovementTest, HoldingJumpDoesNotJumpAgain) {
    MovementState held = standingOnTheFloor();
    held.isJumpHeld = true;
    const MovementState after = simulate(held, command({}, {Button::Jump}), {}, floorWorld());

    EXPECT_TRUE(after.isOnGround);
    EXPECT_EQ(after.velocity, Vec3{});
    EXPECT_TRUE(after.isJumpHeld);
    EXPECT_FALSE(simulate(held, command(), {}, floorWorld()).isJumpHeld) << "released";
}

TEST(MovementTest, AJumpFasterThanTheLimitIsSlowed) {
    MovementState fast = standingOnTheFloor();
    fast.velocity = Vec3{.x = 400.0F};
    const MovementState after = simulate(fast, command({}, {Button::Jump}), {}, floorWorld());

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

TEST(MovementTest, NoclipFliesThroughWallsAtTheCommandsSpeeds) {
    // A wall from x = 10 on, over the floor.
    const BrushHulls walled = world({facingX(10.0F), facingUp(0.0F)},
                                    {HullNode{.plane = {0}, .children = {Contents::Solid, HullNodeIndex{1}}},
                                     HullNode{.plane = {1}, .children = {Contents::Empty, Contents::Solid}}});
    const MovementState flying{.moveType = cs::MoveType::Noclip,
                               .origin = {.x = 5.0F, .z = 50.0F},
                               .velocity = {.z = -100.0F},
                               .maxSpeed = kKnifeSpeed};
    // 400 ahead and 300 up ask for 500, which the knife's 250 halves.
    const UserCommand ahead =
        command(cs::MoveIntent{.forward = nx::UnitsPerSecond{400.0F}, .up = nx::UnitsPerSecond{300.0F}});

    const MovementState after = repeat(flying, ahead, 10, walled);
    EXPECT_NEAR(after.origin.x, 5.0F + 20.0F, kTolerance) << "through the wall at x = 10";
    EXPECT_NEAR(after.origin.z, 50.0F + 15.0F, kTolerance) << "without gravity";
    EXPECT_EQ(after.velocity, Vec3{});
    EXPECT_EQ(after.moveType, cs::MoveType::Noclip);
}

TEST(MovementTest, NoclipRisesOffTheFloorOnlyFasterThanItSticks) {
    // Parity: on the ground, each command puts the player back on the floor 2 units below, noclip
    // too: a rise of 1.5 a command stays there, one of 2.5 leaves it.
    MovementState onTheFloor = standingOnTheFloor();
    onTheFloor.moveType = cs::MoveType::Noclip;
    const auto rise = [](float speed) {
        return command(cs::MoveIntent{.up = nx::UnitsPerSecond{speed}});
    };

    EXPECT_NEAR(repeat(onTheFloor, rise(150.0F), 10, floorWorld()).origin.z, kOnTheFloor + 1.5F, kTolerance);
    EXPECT_NEAR(repeat(onTheFloor, rise(250.0F), 10, floorWorld()).origin.z, kOnTheFloor + 25.0F, kTolerance);
}

TEST(MovementTest, ADeadPlayerMovesNoMoreItsEyesUnderItsOrigin) {
    // Killed by nothing, it observes its own body (PM_SpectatorMove): no key moves it, nor gravity.
    MovementState dead = standingOnTheFloor();
    dead.isDead = true;
    dead.punch = nx::EulerAngles{.roll = nx::Degrees{5.0F}};
    const MovementState after =
        simulate(dead, command(forward(250.0F), {Button::Jump, Button::Duck}), {}, floorWorld());
    EXPECT_EQ(after.origin, dead.origin);
    EXPECT_EQ(after.velocity, Vec3{});
    EXPECT_TRUE(after.isOnGround);
    EXPECT_FALSE(after.isDucking);
    EXPECT_FALSE(after.isDucked);
    EXPECT_EQ(after.viewHeight, cs::kDeadViewHeight);
    EXPECT_LT(after.punch.roll, dead.punch.roll) << "its kick drops back still";
    EXPECT_FALSE(cs::bodyOf(after).isPushed) << "pushers pass through it";

    const MovementState falling{.origin = {.z = 100.0F}, .velocity = {.z = -300.0F}, .isDead = true};
    const MovementState fell = simulate(falling, command(), {}, emptyWorld());
    EXPECT_EQ(fell.origin, falling.origin);
    EXPECT_EQ(fell.velocity, falling.velocity);
    EXPECT_FALSE(fell.isOnGround);
}

TEST(MovementTest, AMoveIntoAWallSlidesAlongIt) {
    // A wall from x = 10 on, over the floor.
    const BrushHulls walled = world({facingX(10.0F), facingUp(0.0F)},
                                    {HullNode{.plane = {0}, .children = {Contents::Solid, HullNodeIndex{1}}},
                                     HullNode{.plane = {1}, .children = {Contents::Empty, Contents::Solid}}});
    MovementState diagonal = standingOnTheFloor();
    diagonal.origin.x = 9.0F;
    diagonal.velocity = Vec3{.x = 200.0F, .y = 200.0F};
    const MovementState after = simulate(diagonal, command(), {}, walled);

    EXPECT_EQ(after.velocity.x, 0.0F);
    EXPECT_NEAR(after.velocity.y, 192.0F, 0.01F); // what friction left of it
    EXPECT_NEAR(after.origin.x, 10.0F - kOnTheFloor, kTolerance);
    EXPECT_NEAR(after.origin.y, 1.92F, 0.01F);
}

TEST(MovementTest, ASteepSlopeIsNotGround) {
    const auto slope = [](Vec3 normal) {
        return world({Plane{.normal = normal}},
                     {HullNode{.plane = {0}, .children = {Contents::Empty, Contents::Solid}}});
    };
    const MovementState above{.origin = {.z = 1.0F}, .maxSpeed = kKnifeSpeed};

    EXPECT_TRUE(simulate(above, command(), {}, slope({.x = 0.6F, .z = 0.8F})).isOnGround);

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

    EXPECT_EQ(simulate(fast, command(), {}, emptyWorld()).velocity.x, 2000.0F);
}

TEST(MovementTest, WaterLevelsAreTheFeetTheWaistAndTheEyes) {
    const MovementState swimmer{.maxSpeed = kKnifeSpeed};
    const auto levelIn = [&](float surface) {
        return simulate(swimmer, command(), {}, waterWorld(surface)).waterLevel;
    };

    EXPECT_EQ(levelIn(-50.0F), WaterLevel::Dry);
    EXPECT_EQ(levelIn(-20.0F), WaterLevel::Feet);
    EXPECT_EQ(levelIn(10.0F), WaterLevel::Waist);
    EXPECT_EQ(levelIn(30.0F), WaterLevel::Eyes);
    EXPECT_EQ(simulate(swimmer, command(), {}, waterWorld(30.0F)).waterType, Contents::Water);
}

TEST(MovementTest, JumpSwimsUpAndNothingHeldSinks) {
    const MovementState swimmer{.maxSpeed = kKnifeSpeed};
    const MovementState up = simulate(swimmer, command({}, {Button::Jump}), {}, waterWorld(30.0F));
    const MovementState down = simulate(swimmer, command(), {}, waterWorld(30.0F));

    EXPECT_NEAR(up.velocity.z, 96.0F, kTolerance); // 100, less water's friction
    EXPECT_FALSE(up.isOnGround);
    // A drift of 60, at 0.8 of it, accelerated at sv_accelerate 5 for a hundredth of a second.
    EXPECT_NEAR(down.velocity.z, -2.4F, kTolerance);
}

TEST(MovementTest, AWallAtTheSurfaceStartsAJumpOutOfTheWater) {
    // Water up to z = 10; from x = 20 on, a ledge up to z = 12.
    const BrushHulls pool = world({facingX(20.0F), facingUp(12.0F), facingUp(10.0F)}, {},
                                  {HullNode{.plane = {0}, .children = {HullNodeIndex{1}, HullNodeIndex{2}}},
                                   HullNode{.plane = {1}, .children = {Contents::Empty, Contents::Solid}},
                                   HullNode{.plane = {2}, .children = {Contents::Empty, Contents::Water}}});
    const MovementState swimmer{.origin = {.x = 5.0F}, .maxSpeed = kKnifeSpeed};

    const MovementState starting = simulate(swimmer, command(), {}, pool);
    EXPECT_EQ(starting.waterJumpTime, nx::Milliseconds{2000.0F});
    EXPECT_EQ(starting.waterJumpVelocity, (Vec3{.x = 50.0F}));
    EXPECT_GT(starting.velocity.z, 200.0F);

    const MovementState climbing = simulate(starting, command(), {}, pool);
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
    const MovementState after = simulate(falling, command({}, {Button::Duck}), {}, emptyWorld());

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
    const MovementState standing = simulate(duckedOnTheFloor(), command(), {}, floorWorld());
    EXPECT_FALSE(standing.isDucked);
    EXPECT_EQ(standing.viewHeight, cs::kStandingViewHeight);
    EXPECT_NEAR(standing.origin.z, kOnTheFloor, kTolerance);
    EXPECT_TRUE(standing.isOnGround);

    // No room to stand anywhere, the floor of the ducked box as in floorWorld.
    const BrushHulls tunnel =
        world({facingUp(-18.0F)}, {HullNode{.plane = {0}, .children = {Contents::Solid, Contents::Solid}}},
              {}, {HullNode{.plane = {0}, .children = {Contents::Empty, Contents::Solid}}});
    const MovementState stuck = simulate(duckedOnTheFloor(), command(), {}, tunnel);
    EXPECT_TRUE(stuck.isDucked);
    EXPECT_NEAR(stuck.origin.z, kOnTheFloor - 18.0F, kTolerance);
}

TEST(MovementTest, LettingGoOfADuckBeforeItIsDownHops) {
    const BrushHulls hulls = floorWorld();
    const MovementState pressed = simulate(standingOnTheFloor(), command({}, {Button::Duck}), {}, hulls);
    const MovementState released = simulate(pressed, command(), {}, hulls);

    // The ducktap: the standing box is raised as if it had been ducked, and falls from there.
    EXPECT_FALSE(released.isDucking);
    EXPECT_FALSE(released.isOnGround);
    EXPECT_NEAR(released.origin.z, kOnTheFloor + 18.0F - 0.04F, kTolerance);
}

TEST(MovementTest, ASolidEntityStopsThePlayerWhereItIsPlaced) {
    // The world's floor; model 1, a wall from x = 10 on in its own space, placed 20 units further.
    const BrushHulls hulls = modelsOf(
        {facingUp(0.0F), facingX(10.0F)},
        {ModelTrees{.standing = {HullNode{.plane = {0}, .children = {Contents::Empty, Contents::Solid}}}},
         ModelTrees{.standing = {HullNode{.plane = {1}, .children = {Contents::Solid, Contents::Empty}}}}});
    const std::array wall{cs::BrushEntity{.model = {1}, .origin = {.x = 20.0F}}};
    MovementState walking = standingOnTheFloor();
    walking.origin.x = 5.0F;
    walking.velocity = Vec3{.x = kKnifeSpeed.value()};
    const UserCommand ahead = command(forward(kKnifeSpeed.value()));

    EXPECT_NEAR(repeat(walking, ahead, 20, hulls, wall).origin.x, 30.0F - kOnTheFloor, kTolerance);
    EXPECT_GT(repeat(walking, ahead, 20, hulls).origin.x, 50.0F) << "without the entity";
}

TEST(MovementTest, ASolidBoxStopsThePlayerGrownByItsBox) {
    // A hostage's box standing on the floor, whose surface is 36 below a standing player's origin: grown
    // by the player's box, it stops the player 26 units before its middle.
    const BrushHulls floor = floorWorld();
    const std::array hostage{cs::SolidBox{
        .origin = {.x = 60.0F, .z = -36.0F},
        .box = {.min = {.x = -10.0F, .y = -10.0F, .z = 0.0F}, .max = {.x = 10.0F, .y = 10.0F, .z = 62.0F}}}};
    MovementState walking = standingOnTheFloor();
    walking.velocity = Vec3{.x = kKnifeSpeed.value()};
    const UserCommand ahead = command(forward(kKnifeSpeed.value()));
    MovementState blocked = walking;
    MovementState free = walking;
    for (int tick = 0; tick < 40; ++tick) {
        blocked = cs::simulateMovement(blocked, ahead, MovementSettings{}, floor, {}, hostage).state;
        free = cs::simulateMovement(free, ahead, MovementSettings{}, floor, {}).state;
    }
    EXPECT_NEAR(blocked.origin.x, 60.0F - 26.0F - kOnTheFloor, kTolerance);
    EXPECT_GT(free.origin.x, 90.0F) << "without the box";
}

TEST(MovementTest, AStuckPlayerIsTriedAtANudgeEachSixtyMilliseconds) {
    // Two boxes, grown by a standing player's, solid for its origin from x = -1 on and below y = 1: the
    // first nudge that frees a player at the origin is its 29th, (-2, 2, 0), one the server moves it
    // by, tried 28 tries after the first, each more than 50 ms after the last.
    constexpr float kFar = 1000.0F;
    const std::array boxes{cs::SolidBox{.box = {.min = {.x = 15.0F, .y = -kFar, .z = -kFar},
                                                .max = {.x = kFar, .y = kFar, .z = kFar}}},
                           cs::SolidBox{.box = {.min = {.x = -kFar, .y = -kFar, .z = -kFar},
                                                .max = {.x = kFar, .y = -15.0F, .z = kFar}}}};
    const BrushHulls nothing = emptyWorld();
    constexpr std::uint32_t kFreeingNudge = 28;
    constexpr int kCommandsPerTry = 6;
    MovementState stuck{.maxSpeed = kKnifeSpeed};
    for (std::uint32_t tick = 0; tick < kFreeingNudge * kCommandsPerTry; ++tick) {
        stuck = cs::simulateMovement(stuck, command(), MovementSettings{}, nothing, {}, boxes).state;
    }
    EXPECT_EQ(stuck.origin, Vec3{}) << "it does nothing else";
    EXPECT_EQ(stuck.velocity, Vec3{});
    EXPECT_EQ(stuck.stuckNudge, kFreeingNudge);

    const MovementState freed =
        cs::simulateMovement(stuck, command(), MovementSettings{}, nothing, {}, boxes).state;
    EXPECT_FLOAT_EQ(freed.origin.x, -2.0F);
    EXPECT_FLOAT_EQ(freed.origin.y, 2.0F);
    EXPECT_EQ(freed.stuckNudge, 0U);
}

TEST(MovementTest, APlayerStuckInAnothersBoxIsForcedOutAsItFlails) {
    // Another standing player's box 26 units back along x, solid for this one's origin below x = 6, deeper
    // than any nudge: still, it stays in it; jumping, it is forced out at the first place free of the grid
    // about it, 8 units each way across: x 8, y -8.
    const std::array boxes{cs::SolidBox{.origin = {.x = -26.0F}, .box = cs::kStandingBox}};
    const BrushHulls nothing = emptyWorld();
    const MovementState stuck{.maxSpeed = kKnifeSpeed};
    const MovementState still =
        cs::simulateMovement(stuck, command(), MovementSettings{}, nothing, {}, boxes).state;
    EXPECT_EQ(still.origin, Vec3{});
    UserCommand flailing = command();
    flailing.buttons.set(Button::Jump);
    const MovementState freed =
        cs::simulateMovement(stuck, flailing, MovementSettings{}, nothing, {}, boxes).state;
    EXPECT_FLOAT_EQ(freed.origin.x, 8.0F);
    EXPECT_FLOAT_EQ(freed.origin.y, -8.0F);
}

TEST(MovementTest, AStuckPlayerTouchesWhatItIsInAtEachTry) {
    // Model 1, a wall from x = 10 on; model 2, a floor: solid below z = 0, both in their own space.
    const BrushHulls hulls = modelsOf(
        {facingUp(0.0F), facingX(10.0F)},
        {ModelTrees{},
         ModelTrees{.standing = {HullNode{.plane = {1}, .children = {Contents::Solid, Contents::Empty}}}},
         ModelTrees{.standing = {HullNode{.plane = {0}, .children = {Contents::Empty, Contents::Solid}}}}});
    const std::array entities{cs::BrushEntity{.model = {1}}, cs::BrushEntity{.model = {2}}};
    MovementState stuck = standingOnTheFloor();
    stuck.origin.x = 20.0F;
    std::vector<int> touching;
    for (int tick = 1; tick <= 13; ++tick) {
        const cs::MovementResult result = cs::simulateMovement(stuck, command(), {}, hulls, entities);
        stuck = result.state;
        if (!result.touchedModels.empty()) {
            EXPECT_EQ(result.touchedModels, std::vector{ModelIndex{1}});
            touching.push_back(tick);
        }
    }
    EXPECT_EQ(touching, (std::vector{1, 7, 13})) << "a try each sixth command of 10 ms";
    EXPECT_EQ(stuck.origin.x, 20.0F) << "deeper in than any nudge";
}

TEST(MovementTest, TheServerLeavesAStuckPlayerASmallNudgeWouldFree) {
    // A box whose top, grown by a standing player's box, is at z = 36: a player a tenth of a unit
    // below it is freed by the nudge of an eighth of a unit up, which the server leaves to the client.
    const std::array box{cs::SolidBox{.box = {.min = {.x = -10.0F, .y = -10.0F, .z = -100.0F},
                                              .max = {.x = 10.0F, .y = 10.0F, .z = 0.0F}}}};
    const BrushHulls nothing = emptyWorld();
    MovementState stuck{.origin = {.z = 35.9F}, .maxSpeed = kKnifeSpeed};
    for (int tick = 0; tick < 100; ++tick) {
        stuck = cs::simulateMovement(stuck, command(), MovementSettings{}, nothing, {}, box).state;
    }
    EXPECT_EQ(stuck.origin, (Vec3{.z = 35.9F})) << "never nudged";
    EXPECT_LT(stuck.stuckNudge, 3U) << "the nudges start again once one frees it";
}

TEST(MovementTest, APlayerStandingOnABoxStandsOnABox) {
    // A hostage's box under a standing player: it stands on it, which has no model (no texture).
    const BrushHulls nothing = emptyWorld();
    const std::array box{cs::SolidBox{.box = {.min = {.x = -10.0F, .y = -10.0F, .z = -100.0F},
                                              .max = {.x = 10.0F, .y = 10.0F, .z = 0.0F}}}};
    const MovementState above{.origin = {.z = 36.0F + kOnTheFloor}, .maxSpeed = kKnifeSpeed};
    const cs::MovementResult result =
        cs::simulateMovement(above, command(), MovementSettings{}, nothing, {}, box);
    EXPECT_TRUE(result.state.isOnGround);
    EXPECT_EQ(result.state.ground, cs::MovementSolid{cs::BoxSolid{}});
    EXPECT_FALSE(cs::bodyOf(result.state).groundModel.has_value()) << "no pusher carries it";
}

TEST(MovementTest, WhatThePlayerRunsIntoOrStandsOnIsTouched) {
    // Model 1, a wall from x = 10 on; model 2, a floor: solid below z = 0, both in their own space.
    const BrushHulls hulls = modelsOf(
        {facingUp(0.0F), facingX(10.0F)},
        {ModelTrees{},
         ModelTrees{.standing = {HullNode{.plane = {1}, .children = {Contents::Solid, Contents::Empty}}}},
         ModelTrees{.standing = {HullNode{.plane = {0}, .children = {Contents::Empty, Contents::Solid}}}}});
    const std::array entities{cs::BrushEntity{.model = {1}}, cs::BrushEntity{.model = {2}}};
    MovementState running = standingOnTheFloor();
    running.origin.x = 9.0F;
    running.velocity = Vec3{.x = 200.0F};

    const cs::MovementResult result = cs::simulateMovement(running, command(), {}, hulls, entities);
    EXPECT_TRUE(result.state.isOnGround);
    EXPECT_EQ(result.state.ground, cs::MovementSolid{ModelIndex{2}});
    EXPECT_EQ(result.touchedModels, (std::vector{ModelIndex{2}, ModelIndex{1}}))
        << "the floor, then the wall, each once";

    const cs::MovementResult onTheWorld =
        cs::simulateMovement(standingOnTheFloor(), command(), {}, floorWorld());
    EXPECT_TRUE(onTheWorld.state.isOnGround);
    EXPECT_EQ(onTheWorld.state.ground, cs::MovementSolid{cs::WorldSolid{}});
    EXPECT_TRUE(onTheWorld.touchedModels.empty());
}

TEST(MovementTest, AVolumeEntityHoldsWhatItSays) {
    // Model 1: below z = 30 in its point hull.
    const BrushHulls hulls = modelsOf(
        {facingUp(30.0F)},
        {ModelTrees{},
         ModelTrees{.point = {HullNode{.plane = {0}, .children = {Contents::Empty, Contents::Solid}}}}});
    const std::array slime{
        cs::BrushEntity{.model = {1}, .role = cs::BrushRole::Volume, .contents = Contents::Slime}};
    const MovementState swimmer{.maxSpeed = kKnifeSpeed};
    const MovementState after = simulate(swimmer, command({}, {Button::Jump}), {}, hulls, slime);

    EXPECT_EQ(after.waterLevel, WaterLevel::Eyes);
    EXPECT_EQ(after.waterType, Contents::Slime);
    EXPECT_NEAR(after.velocity.z, 76.8F, kTolerance); // 80 in slime, less its friction
    EXPECT_EQ(simulate(swimmer, command(), {}, hulls).waterLevel, WaterLevel::Dry) << "without the entity";
}

/// A floor (its point hull 36 below the standing one's, where the feet are) and model 1, a ladder:
/// a standing box touches it from x = 9 on, its brush is from x = 25 on, 200 high.
[[nodiscard]] auto ladderWorld() -> BrushHulls {
    return modelsOf(
        {facingUp(0.0F), facingUp(-36.0F), facingX(9.0F), facingX(25.0F)},
        {ModelTrees{.standing = {HullNode{.plane = {0}, .children = {Contents::Empty, Contents::Solid}}},
                    .point = {HullNode{.plane = {1}, .children = {Contents::Empty, Contents::Solid}}}},
         ModelTrees{
             .standing = {HullNode{.plane = {2}, .children = {Contents::Solid, Contents::Empty}}},
             .point = {HullNode{.plane = {3}, .children = {Contents::Solid, Contents::Empty}}},
             .bounds = {.min = {.x = 25.0F, .y = -32.0F}, .max = {.x = 33.0F, .y = 32.0F, .z = 200.0F}}}});
}

constexpr std::array kLadder{cs::BrushEntity{.model = {1}, .role = cs::BrushRole::Ladder}};

TEST(MovementTest, OnALadderForwardClimbsAndBackOnTheFloorStepsOff) {
    const BrushHulls hulls = ladderWorld();
    MovementState atTheLadder = standingOnTheFloor();
    atTheLadder.origin.x = 10.0F;

    // Facing the ladder: what goes into its face goes up it, at 200 at most, without gravity.
    const MovementState climbing = simulate(atTheLadder, command({}, {Button::Forward}), {}, hulls, kLadder);
    EXPECT_EQ(climbing.velocity, (Vec3{.z = 200.0F}));
    EXPECT_NEAR(climbing.origin.z, kOnTheFloor + 2.0F, kTolerance);

    // Backing away down on the floor: pushed off the ladder, sliding along the floor.
    const MovementState leaving = simulate(atTheLadder, command({}, {Button::Back}), {}, hulls, kLadder);
    EXPECT_NEAR(leaving.velocity.x, -200.0F, kTolerance);
    EXPECT_NEAR(leaving.origin.x, 8.0F, kTolerance);

    EXPECT_EQ(simulate(atTheLadder, command(), {}, hulls, kLadder).velocity, Vec3{}) << "held still";
}

TEST(MovementTest, JumpLeavesALadder) {
    MovementState onTheLadder{.origin = {.x = 10.0F, .z = 50.0F}, .maxSpeed = kKnifeSpeed};
    const MovementState after =
        simulate(onTheLadder, command({}, {Button::Jump}), {}, ladderWorld(), kLadder);

    EXPECT_EQ(after.velocity.x, -270.0F); // away from its face
    EXPECT_NEAR(after.velocity.z, -8.0F, kTolerance);
}

TEST(MovementTest, APlayersBodyIsItsBoxAndWhatItStandsOn) {
    const nx::game::Body standing = cs::bodyOf(MovementState{.isOnGround = true, .ground = ModelIndex{3}});
    EXPECT_EQ(standing.hull, nx::collision::Hull::Standing);
    EXPECT_EQ(standing.box.min, (Vec3{.x = -16.0F, .y = -16.0F, .z = -36.0F}));
    EXPECT_EQ(standing.box.max, (Vec3{.x = 16.0F, .y = 16.0F, .z = 36.0F}));
    EXPECT_EQ(standing.groundModel, std::optional{ModelIndex{3}});

    const nx::game::Body ducked = cs::bodyOf(MovementState{.isDucked = true, .ground = ModelIndex{3}});
    EXPECT_EQ(ducked.hull, nx::collision::Hull::Ducked);
    EXPECT_EQ(ducked.box.max.z, 18.0F);
    EXPECT_FALSE(ducked.groundModel.has_value()) << "in the air, it stands on nothing";
}

TEST(MovementTest, TheGamesBrushEntitiesAreMetAsTheEngineListsThem) {
    nx::ecs::World world;
    ASSERT_TRUE(nx::game::registerComponents(world).has_value());
    const auto spawn = [&](std::uint32_t model, nx::game::Solid solid, std::optional<Contents> contents) {
        world.spawn(nx::game::BrushModel{.index = {model}},
                    nx::game::Transform{.origin = {.x = static_cast<float>(model)}},
                    nx::game::BrushSolidity{.solid = solid, .contents = contents});
    };
    spawn(1, nx::game::Solid::Bsp, Contents::Water);  // solid, whatever its skin
    spawn(2, nx::game::Solid::Not, std::nullopt);     // nothing to meet
    spawn(3, nx::game::Solid::Not, Contents::Ladder); // a ladder
    spawn(4, nx::game::Solid::Not, Contents::Slime);  // a volume

    const std::vector<cs::BrushEntity> met = cs::brushEntitiesOf(world);
    ASSERT_EQ(met.size(), 3U);
    EXPECT_EQ(met[0].model, ModelIndex{1});
    EXPECT_EQ(met[0].role, cs::BrushRole::Solid);
    EXPECT_EQ(met[0].origin, (Vec3{.x = 1.0F}));
    EXPECT_EQ(met[1].model, ModelIndex{3});
    EXPECT_EQ(met[1].role, cs::BrushRole::Ladder);
    EXPECT_EQ(met[2].model, ModelIndex{4});
    EXPECT_EQ(met[2].role, cs::BrushRole::Volume);
    EXPECT_EQ(met[2].contents, Contents::Slime);
}

/// The footsteps among `sounds`.
[[nodiscard]] auto footstepsOf(std::span<const cs::MovementSound> sounds) -> std::vector<cs::Footstep> {
    std::vector<cs::Footstep> steps;
    for (const cs::MovementSound& sound : sounds) {
        if (const auto* step = std::get_if<cs::Footstep>(&sound)) {
            steps.push_back(*step);
        }
    }
    return steps;
}

/// A player running on the floor at `speed`, its next step due.
[[nodiscard]] auto runningOnTheFloor(float speed) -> MovementState {
    MovementState state = standingOnTheFloor();
    state.velocity = Vec3{.x = speed};
    return state;
}

TEST(FootstepTest, ARunnersFeetSoundInTurnEachThreeTenthsOfASecond) {
    const BrushHulls floor = floorWorld();
    MovementState state = runningOnTheFloor(250.0F);
    const UserCommand run = command(forward(250.0F));
    std::vector<std::pair<int, cs::Footstep>> steps;
    for (int tick = 0; tick < 70; ++tick) {
        const cs::MovementResult moved = cs::simulateMovement(state, run, MovementSettings{}, floor, {});
        for (const cs::Footstep& step : footstepsOf(moved.sounds)) {
            steps.emplace_back(tick, step);
        }
        state = moved.state;
    }
    ASSERT_EQ(steps.size(), 3U);
    EXPECT_EQ(steps[0].first, 0);
    EXPECT_EQ(steps[1].first, 30);
    EXPECT_EQ(steps[2].first, 60);
    EXPECT_TRUE(steps[0].second.isLeftFoot) << "the left foot first, as iStepLeft turns from 0";
    EXPECT_FALSE(steps[1].second.isLeftFoot);
    EXPECT_TRUE(steps[2].second.isLeftFoot);
    const cs::Footstep& first = steps[0].second;
    EXPECT_EQ(first.place, cs::StepPlace::Ground);
    EXPECT_EQ(first.share, 1.0F);
    EXPECT_FALSE(first.volume.has_value());
    ASSERT_TRUE(first.ground.has_value());
    EXPECT_EQ(first.ground->ground, cs::MovementSolid{cs::WorldSolid{}}) << "on the world";
}

TEST(FootstepTest, AWalkerOrADuckedPlayerIsQuieter) {
    const BrushHulls floor = floorWorld();
    const cs::MovementResult walking = cs::simulateMovement(
        runningOnTheFloor(140.0F), command(forward(140.0F)), MovementSettings{}, floor, {});
    EXPECT_TRUE(footstepsOf(walking.sounds).empty()) << "no faster than 150: silent";
    EXPECT_EQ(walking.state.stepTime, nx::Milliseconds{400.0F});

    MovementState ducked = duckedOnTheFloor();
    ducked.velocity = Vec3{.x = 200.0F};
    const cs::MovementResult crouching =
        cs::simulateMovement(ducked, command(forward(200.0F), {Button::Duck}), MovementSettings{}, floor, {});
    const std::vector<cs::Footstep> steps = footstepsOf(crouching.sounds);
    ASSERT_EQ(steps.size(), 1U);
    EXPECT_FLOAT_EQ(steps.front().share, 0.35F);
    EXPECT_EQ(crouching.state.stepTime, nx::Milliseconds{400.0F}) << "300, and 100 more ducked";
}

TEST(FootstepTest, AHardLandingSoundsLoudAndRollsTheView) {
    const BrushHulls floor = floorWorld();
    MovementState falling{.origin = {.z = 5.0F}, .velocity = {.z = -600.0F}, .maxSpeed = kKnifeSpeed};
    const cs::MovementResult landed = cs::simulateMovement(falling, command(), MovementSettings{}, floor, {});
    ASSERT_TRUE(landed.state.isOnGround);
    const std::vector<cs::Footstep> steps = footstepsOf(landed.sounds);
    ASSERT_EQ(steps.size(), 1U);
    EXPECT_EQ(steps.front().volume, 1.0F) << "faster than the safest fall, 580";
    EXPECT_NEAR(landed.state.punch.roll.value(), 600.0F * 0.013F, kTolerance);
    EXPECT_EQ(landed.state.fallSpeed, nx::UnitsPerSecond{}) << "on the ground, the fall is over";
    EXPECT_EQ(landed.state.stepTime, nx::Milliseconds{300.0F});

    falling.velocity.z = -300.0F;
    const cs::MovementResult firm = cs::simulateMovement(falling, command(), MovementSettings{}, floor, {});
    ASSERT_EQ(footstepsOf(firm.sounds).size(), 1U);
    EXPECT_EQ(footstepsOf(firm.sounds).front().volume, 0.85F) << "faster than half the safest fall, 290";
    falling.velocity.z = -270.0F;
    const cs::MovementResult soft = cs::simulateMovement(falling, command(), MovementSettings{}, floor, {});
    EXPECT_TRUE(footstepsOf(soft.sounds).empty()) << "no faster than 290 and slower than 350: silent";
    EXPECT_NEAR(soft.state.punch.roll.value(), 0.0F, kTolerance) << "silent: no roll either";
    falling.velocity.z = -200.0F;
    EXPECT_EQ(
        cs::simulateMovement(falling, command(), MovementSettings{}, floor, {}).state.punch.roll.value(),
        0.0F)
        << "slower than 250: no roll";
}

TEST(FootstepTest, GoingIntoWaterSplashes) {
    MovementState falling{.origin = {.z = 36.0F}, .velocity = {.z = -200.0F}, .maxSpeed = kKnifeSpeed};
    const cs::MovementResult moved =
        cs::simulateMovement(falling, command(), MovementSettings{}, waterWorld(0.0F), {});
    EXPECT_EQ(moved.state.waterLevel, WaterLevel::Feet);
    ASSERT_EQ(moved.sounds.size(), 1U);
    EXPECT_TRUE(std::holds_alternative<cs::Splash>(moved.sounds.front()));
}

TEST(MovementTest, AMoveMeetsTheBoxesWithinThePhysentReachButItsOwn) {
    nx::ecs::World world;
    const nx::ecs::Entity mover = world.spawn();
    const nx::ecs::Entity near = world.spawn();
    const nx::ecs::Entity reached = world.spawn();
    const nx::ecs::Entity far = world.spawn();
    constexpr nx::Aabb kBox{.min = {.x = -16.0F, .y = -16.0F, .z = -36.0F},
                            .max = {.x = 16.0F, .y = 16.0F, .z = 36.0F}};
    const float reach = cs::kPhysentReach.value();
    const std::array boxes{
        cs::SolidBox{.origin = {.x = 100.0F}, .box = kBox, .entity = near},
        cs::SolidBox{.origin = {}, .box = kBox, .entity = mover},
        cs::SolidBox{.origin = {.y = reach + 15.0F}, .box = kBox, .entity = reached}, // its side within reach
        cs::SolidBox{.origin = {.x = -(reach + 17.0F)}, .box = kBox, .entity = far}}; // its side beyond it
    std::vector<cs::SolidBox> met{cs::SolidBox{.entity = far}};
    cs::boxesNear(boxes, {}, mover, met);
    ASSERT_EQ(met.size(), 2U) << "what was there replaced";
    EXPECT_EQ(met[0].entity, near);
    EXPECT_EQ(met[1].entity, reached);
}

} // namespace
