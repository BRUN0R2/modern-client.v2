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
using nx::Vec3;
using nx::collision::BrushHulls;
using nx::collision::Contents;
using nx::collision::HullChild;
using nx::collision::HullNode;
using nx::collision::HullNodeIndex;
using nx::collision::HullRoots;
using nx::game::Door;
using nx::game::DoorState;
using nx::game::Transform;
using KeyValues = std::vector<std::pair<std::string, std::string>>;

constexpr float kTolerance = 1.0e-3F;

[[nodiscard]] constexpr auto facingX(float x) -> Plane {
    return Plane{.normal = {.x = 1.0F}, .distance = x};
}

/// The door's model: 64 units deep from x = 0, 128 wide and high.
constexpr nx::Aabb kDoorBounds{.min = {.x = 0.0F, .y = -64.0F, .z = -64.0F},
                               .max = {.x = 64.0F, .y = 64.0F, .z = 64.0F}};

/// The world, with a wall from x = 100 on for a standing box when `isWalled`, and the door, model 1:
/// solid from x = 0 to 64 for a standing box, in its own space.
[[nodiscard]] auto doorHulls(bool isWalled, nx::Aabb doorBounds = kDoorBounds) -> BrushHulls {
    std::vector<HullNode> nodes{HullNode{.plane = {0}, .children = {HullNodeIndex{1}, Contents::Empty}},
                                HullNode{.plane = {1}, .children = {Contents::Empty, Contents::Solid}},
                                HullNode{.plane = {2}, .children = {Contents::Solid, Contents::Empty}},
                                HullNode{.plane = {0}, .children = {Contents::Empty, Contents::Empty}}};
    const HullChild wall = isWalled ? HullChild{HullNodeIndex{2}} : HullChild{Contents::Empty};
    auto hulls = BrushHulls::create(nx::collision::BrushHullsDesc{
        .planes = {facingX(0.0F), facingX(64.0F), facingX(100.0F)},
        .clipNodes = std::move(nodes),
        .models = {nx::collision::BrushModel{
                       .hulls = HullRoots{{Contents::Empty, wall, Contents::Empty, Contents::Empty}}},
                   nx::collision::BrushModel{.hulls = HullRoots{{Contents::Empty, HullNodeIndex{0},
                                                                 Contents::Empty, Contents::Empty}},
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
            .brushClasses = {nx::game::BrushClass{.classname = classname,
                                                  .solid = nx::game::Solid::Bsp,
                                                  .behavior = behavior,
                                                  .restartsEachRound = true}}};
        EXPECT_TRUE(nx::game::spawnBrushEntities(m_world, entities, models, classes).has_value());
        nx::ecs::Query<nx::ecs::Entity, const Door&>{m_world}.forEach(
            [this](nx::ecs::Entity entity, const Door&) { m_door = entity; });
        if (body) {
            m_toucher = m_world.spawn(
                Transform{.origin = *body},
                nx::game::Body{.box = {.min = {-16.0F, -16.0F, -36.0F}, .max = {16.0F, 16.0F, 36.0F}}});
        } else {
            m_toucher = m_world.spawn(Transform{.origin = toucher});
        }
    }

    void touch(const BrushHulls& hulls) {
        const std::array models{nx::collision::ModelIndex{1}};
        nx::game::touchBrushEntities(m_world, hulls, models, m_toucher);
    }

    void restart(const BrushHulls& hulls) { nx::game::restartBrushEntities(m_world, hulls); }

    void run(const BrushHulls& hulls, int ticks) {
        for (int tick = 0; tick < ticks; ++tick) {
            nx::game::runPushers(m_world, hulls);
            nx::game::advanceTick(m_world);
        }
    }

    /// The game's clock, and the door's own, put at `time`.
    void startAt(nx::GameTime time) {
        m_world.resource<nx::game::TickTime>()->now = time;
        m_world.get<nx::game::Pusher>(m_door)->localTime = time;
    }

    [[nodiscard]] auto door() -> const Door& { return *m_world.get<Door>(m_door); }
    [[nodiscard]] auto doorAt() -> const Transform& { return *m_world.get<Transform>(m_door); }
    [[nodiscard]] auto toucherOrigin() -> Vec3 { return m_world.get<Transform>(m_toucher)->origin; }

    /// The body flies in noclip: pushers pass through it.
    void noclip() { m_world.get<nx::game::Body>(m_toucher)->isPushed = false; }

    /// The body is solid as its box, as a player's is to the server's traces.
    void makeSolid() {
        m_world.add(m_toucher, nx::game::BoxSolidity{.box = m_world.get<nx::game::Body>(m_toucher)->box});
    }

private:
    nx::ecs::World m_world;
    nx::ecs::Entity m_door{};
    nx::ecs::Entity m_toucher{};
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

TEST(DoorTest, ARoundStartClosesADoorFromWhereItIsATouchOpeningItAgainMeanwhile) {
    const BrushHulls hulls = doorHulls(false);
    DoorWorld game{slidingDoor(), nx::game::BrushBehavior::SlidingDoor};
    game.touch(hulls);
    game.run(hulls, 72);
    ASSERT_EQ(game.door().state, DoorState::Open);

    game.restart(hulls);
    EXPECT_EQ(game.door().state, DoorState::Closing) << "it closes again, not at once";
    EXPECT_TRUE(game.door().isTouchable) << "a touch opens it again, as CBaseDoor::Restart sets it";
    game.run(hulls, 32);
    EXPECT_NEAR(game.doorAt().origin.x, 32.0F, kTolerance);
    game.touch(hulls);
    EXPECT_EQ(game.door().state, DoorState::Opening);

    game.run(hulls, 40);
    game.restart(hulls);
    game.run(hulls, 70);
    EXPECT_EQ(game.door().state, DoorState::Closed);
    EXPECT_EQ(game.doorAt().origin, Vec3{});
    game.restart(hulls);
    EXPECT_EQ(game.door().state, DoorState::Closed) << "closed already, it arrives at once";
}

TEST(DoorTest, ADoorMovesAMonthIntoAMapAsAtItsStart) {
    // A month of ticks at 100 a second: float seconds that far are a quarter of a second apart.
    constexpr std::int64_t kMonth = std::int64_t{100} * 60 * 60 * 24 * 31;
    const BrushHulls hulls = doorHulls(false);
    DoorWorld fresh{slidingDoor(), nx::game::BrushBehavior::SlidingDoor};
    DoorWorld old{slidingDoor(), nx::game::BrushBehavior::SlidingDoor};
    old.startAt(nx::GameTime::atTick(kMonth));

    fresh.touch(hulls);
    old.touch(hulls);
    for (const int ticks : {32, 40, 100, 70}) {
        fresh.run(hulls, ticks);
        old.run(hulls, ticks);
        EXPECT_EQ(old.door().state, fresh.door().state) << "after " << ticks << " more ticks";
        EXPECT_EQ(old.doorAt().origin, fresh.doorAt().origin) << "after " << ticks << " more ticks";
    }
    EXPECT_EQ(old.door().state, DoorState::Closed);
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

TEST(DoorTest, AnOpeningDoorPushesABodySolidAsABoxThroughAllButItself) {
    const BrushHulls hulls = doorHulls(false);
    DoorWorld game{slidingDoor(), nx::game::BrushBehavior::SlidingDoor, Vec3{.x = 70.0F}};
    game.makeSolid();

    game.touch(hulls);
    game.run(hulls, 80);
    EXPECT_EQ(game.door().state, DoorState::Open) << "its own box does not hold it";
    EXPECT_NEAR(game.toucherOrigin().x, 128.0F, 0.01F);
}

TEST(DoorTest, AnOpeningDoorPassesThroughABodyInNoclip) {
    const BrushHulls hulls = doorHulls(true);
    DoorWorld game{slidingDoor(), nx::game::BrushBehavior::SlidingDoor, Vec3{.x = 70.0F}};
    game.noclip();

    game.touch(hulls);
    game.run(hulls, 80);
    EXPECT_EQ(game.door().state, DoorState::Open) << "not blocked, even against the wall";
    EXPECT_EQ(game.toucherOrigin(), (Vec3{.x = 70.0F})) << "not pushed";
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
