// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Doors: opened by a touch, moved and turned by the pusher physics, pushing a body out of their way
// and giving way to one they cannot push.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.assets;
import nx.collision;
import nx.game;

namespace {

using nx::Plane;
using nx::Seconds;
using nx::Vec3;
using nx::collision::BrushHulls;
using nx::collision::Contents;
using nx::collision::HullChild;
using nx::collision::HullNode;
using nx::collision::HullNodeIndex;
using nx::game::Door;
using nx::game::DoorState;
using nx::game::Transform;
using KeyValues = std::vector<std::pair<std::string, std::string>>;

constexpr float kTolerance = 1.0e-3F;
constexpr Seconds kTick{0.01F};

[[nodiscard]] constexpr auto facingX(float x) -> Plane {
    return Plane{.normal = {.x = 1.0F}, .distance = x};
}

/// The door's model: 64 units deep from x = 0, 128 wide and high.
constexpr nx::Aabb kDoorBounds{.min = {.x = 0.0F, .y = -64.0F, .z = -64.0F},
                               .max = {.x = 64.0F, .y = 64.0F, .z = 64.0F}};

/// The world, with a wall from x = 100 on for a standing box when `isWalled`, and the door, model 1:
/// solid from x = 0 to 64 for a standing box, in its own space.
[[nodiscard]] auto doorHulls(bool isWalled, nx::Aabb doorBounds = kDoorBounds) -> BrushHulls {
    std::vector<HullNode> nodes{HullNode{.plane = 0, .children = {HullNodeIndex{1}, Contents::Empty}},
                                HullNode{.plane = 1, .children = {Contents::Empty, Contents::Solid}},
                                HullNode{.plane = 2, .children = {Contents::Solid, Contents::Empty}},
                                HullNode{.plane = 0, .children = {Contents::Empty, Contents::Empty}}};
    const HullChild wall = isWalled ? HullChild{HullNodeIndex{2}} : HullChild{Contents::Empty};
    auto hulls = BrushHulls::create(nx::collision::BrushHullsDesc{
        .planes = {facingX(0.0F), facingX(64.0F), facingX(100.0F)},
        .clipNodes = std::move(nodes),
        .models = {
            nx::collision::BrushModel{.hulls = {Contents::Empty, wall, Contents::Empty, Contents::Empty}},
            nx::collision::BrushModel{
                .hulls = {Contents::Empty, HullNodeIndex{0}, Contents::Empty, Contents::Empty},
                .bounds = doorBounds}}});
    EXPECT_TRUE(hulls.has_value());
    return std::move(hulls).value();
}

/// A world with the door of `keyValues`, model 1 of class `classname`, and what touches it: a
/// standing body at `body` when there is one, else a toucher at `toucher`.
class DoorWorld {
public:
    DoorWorld(KeyValues keyValues, nx::game::BrushBehavior behavior, std::optional<Vec3> body = std::nullopt,
              Vec3 toucher = {}) {
        EXPECT_TRUE(nx::game::registerComponents(m_world).has_value());
        const std::string classname = keyValues.front().second;
        const std::vector entities{nx::assets::BspEntity{.keyValues = std::move(keyValues)}};
        const std::vector models{nx::assets::BspModel{}, nx::assets::BspModel{.bounds = kDoorBounds}};
        const nx::game::EntityClasses classes{
            .brushClasses = {nx::game::BrushClass{
                .classname = classname, .solid = nx::game::Solid::Bsp, .behavior = behavior}}};
        EXPECT_TRUE(nx::game::spawnBrushEntities(m_world, entities, models, classes).has_value());
        nx::ecs::Query<nx::ecs::Entity, const Door&>{m_world}.forEach(
            [this](nx::ecs::Entity entity, const Door&) { m_door = entity; });
        if (body) {
            m_toucher =
                m_world.spawn(Transform{.origin = *body}, nx::game::Body{.mins = {-16.0F, -16.0F, -36.0F},
                                                                         .maxs = {16.0F, 16.0F, 36.0F}});
        } else {
            m_toucher = m_world.spawn(Transform{.origin = toucher});
        }
    }

    void touch(const BrushHulls& hulls) {
        const std::array models{std::uint32_t{1}};
        nx::game::touchBrushEntities(m_world, hulls, models, m_toucher);
    }

    void run(const BrushHulls& hulls, int ticks) {
        for (int tick = 0; tick < ticks; ++tick) {
            nx::game::runPushers(m_world, hulls, kTick, m_time);
            m_time += kTick;
        }
    }

    [[nodiscard]] auto door() -> const Door& { return *m_world.get<Door>(m_door); }
    [[nodiscard]] auto doorAt() -> const Transform& { return *m_world.get<Transform>(m_door); }
    [[nodiscard]] auto toucherOrigin() -> Vec3 { return m_world.get<Transform>(m_toucher)->origin; }

private:
    nx::ecs::World m_world;
    nx::ecs::Entity m_door{};
    nx::ecs::Entity m_toucher{};
    Seconds m_time{};
};

/// A sliding door opening toward +X at 100 a second, open a second, and `extra`.
[[nodiscard]] auto slidingDoor(KeyValues extra = {}) -> KeyValues {
    KeyValues keyValues{
        {"classname", "func_door"}, {"model", "*1"}, {"angles", "0 0 0"}, {"speed", "100"}, {"wait", "1"}};
    keyValues.insert(keyValues.end(), extra.begin(), extra.end());
    return keyValues;
}

TEST(DoorTest, ATouchOpensADoorThatClosesAfterItsWait) {
    const BrushHulls hulls = doorHulls(false);
    DoorWorld game{slidingDoor(), nx::game::BrushBehavior::SlidingDoor};
    EXPECT_EQ(std::get<nx::game::DoorSlide>(game.door().motion).open, (Vec3{.x = 64.0F}))
        << "its depth along the way it opens";

    game.run(hulls, 10);
    EXPECT_EQ(game.doorAt().origin, Vec3{}) << "untouched, it rests";

    game.touch(hulls);
    game.run(hulls, 32);
    EXPECT_EQ(game.door().state, DoorState::Opening);
    EXPECT_NEAR(game.doorAt().origin.x, 32.0F, kTolerance) << "at 100 a second";

    game.run(hulls, 40);
    EXPECT_EQ(game.door().state, DoorState::Open);
    EXPECT_EQ(game.doorAt().origin, (Vec3{.x = 64.0F}));

    game.run(hulls, 100);
    EXPECT_EQ(game.door().state, DoorState::Closing) << "a second open";
    game.run(hulls, 70);
    EXPECT_EQ(game.door().state, DoorState::Closed);
    EXPECT_EQ(game.doorAt().origin, Vec3{});
}

TEST(DoorTest, ADoorOthersOpenIgnoresATouch) {
    const BrushHulls hulls = doorHulls(false);
    DoorWorld game{slidingDoor({{"targetname", "vault"}}), nx::game::BrushBehavior::SlidingDoor};

    game.touch(hulls);
    game.run(hulls, 50);
    EXPECT_EQ(game.door().state, DoorState::Closed);
    EXPECT_EQ(game.doorAt().origin, Vec3{});
}

TEST(DoorTest, AnOpeningDoorPushesABodyOutOfItsWay) {
    const BrushHulls hulls = doorHulls(false);
    DoorWorld game{slidingDoor(), nx::game::BrushBehavior::SlidingDoor, Vec3{.x = 70.0F}};

    game.touch(hulls);
    game.run(hulls, 80);
    EXPECT_EQ(game.door().state, DoorState::Open);
    EXPECT_NEAR(game.toucherOrigin().x, 128.0F, 0.01F) << "pushed ahead of its face";
}

TEST(DoorTest, ADoorThatCannotPushABodyGoesBack) {
    const BrushHulls hulls = doorHulls(true);
    DoorWorld game{slidingDoor(), nx::game::BrushBehavior::SlidingDoor, Vec3{.x = 70.0F}};

    game.touch(hulls);
    game.run(hulls, 200);
    EXPECT_EQ(game.door().state, DoorState::Closed) << "blocked against the wall, it closed again";
    EXPECT_EQ(game.doorAt().origin, Vec3{});
    EXPECT_LT(game.toucherOrigin().x, 100.0F);
}

/// A door turning a quarter about its yaw axis at 90 degrees a second, open a second.
[[nodiscard]] auto rotatingDoor() -> KeyValues {
    return KeyValues{{"classname", "func_door_rotating"},
                     {"model", "*1"},
                     {"distance", "90"},
                     {"speed", "90"},
                     {"wait", "1"}};
}

TEST(DoorTest, ARotatingDoorTurnsAwayFromWhatOpensIt) {
    // A leaf of 64 along +X from its hinge at the origin, 8 thick.
    const BrushHulls hulls = doorHulls(false, nx::Aabb{.min = {.x = 0.0F, .y = -4.0F, .z = -64.0F},
                                                       .max = {.x = 64.0F, .y = 4.0F, .z = 64.0F}});
    // It turns away from a toucher on either side of its leaf.
    for (const auto& [side, expectedYaw] : {std::pair{-50.0F, 90.0F}, std::pair{50.0F, -90.0F}}) {
        DoorWorld game{rotatingDoor(), nx::game::BrushBehavior::RotatingDoor, std::nullopt,
                       Vec3{.x = 32.0F, .y = side}};
        game.touch(hulls);
        game.run(hulls, 50);
        EXPECT_EQ(game.door().state, DoorState::Opening);
        EXPECT_NEAR(game.doorAt().angles.yaw.value(), expectedYaw / 2.0F, 0.01F) << side;

        game.run(hulls, 60);
        EXPECT_EQ(game.door().state, DoorState::Open);
        EXPECT_EQ(game.doorAt().angles.yaw.value(), expectedYaw) << side;

        game.run(hulls, 210);
        EXPECT_EQ(game.door().state, DoorState::Closed) << "a second open, then back";
        EXPECT_EQ(game.doorAt().angles, nx::EulerAngles{});
    }
}

} // namespace
