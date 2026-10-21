// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Triggers a body walks into, firing what they name: at once or after a delay, again after their
// wait or once; doors used by name, firing their own targets when they arrive.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.assets;
import nx.collision;
import nx.game;

namespace {

using nx::Vec3;
using nx::collision::Contents;
using nx::game::BrushBehavior;
using nx::game::DelayedUse;
using nx::game::Door;
using nx::game::DoorState;
using nx::game::Named;
using nx::game::Transform;
using nx::game::Trigger;
using KeyValues = std::vector<std::pair<std::string, std::string>>;

/// The doors' models, 64 units deep from x = 0, 128 wide and high, empty to every hull.
constexpr nx::Aabb kDoorBounds{.min = {.x = 0.0F, .y = -64.0F, .z = -64.0F},
                               .max = {.x = 64.0F, .y = 64.0F, .z = 64.0F}};
/// The trigger's model, model 3: solid to every hull, from x = 200 to 300.
constexpr nx::Aabb kTriggerBounds{.min = {.x = 200.0F, .y = -50.0F, .z = -50.0F},
                                  .max = {.x = 300.0F, .y = 50.0F, .z = 50.0F}};
/// Where a body is in the trigger, and where it is not.
constexpr Vec3 kInside{.x = 250.0F};
constexpr Vec3 kOutside{.x = 500.0F};

/// The world, empty; models 1 and 2, doors; model 3, the trigger.
[[nodiscard]] auto triggerHulls() -> nx::collision::BrushHulls {
    const nx::collision::HullRoots empty{
        {Contents::Empty, Contents::Empty, Contents::Empty, Contents::Empty}};
    const nx::collision::HullRoots solid{
        {Contents::Solid, Contents::Solid, Contents::Solid, Contents::Solid}};
    auto hulls = nx::collision::BrushHulls::create(nx::collision::BrushHullsDesc{
        .models = {nx::collision::BrushModel{.hulls = empty},
                   nx::collision::BrushModel{.hulls = empty, .bounds = kDoorBounds},
                   nx::collision::BrushModel{.hulls = empty, .bounds = kDoorBounds},
                   nx::collision::BrushModel{.hulls = solid, .bounds = kTriggerBounds}}});
    EXPECT_TRUE(hulls.has_value());
    return std::move(hulls).value();
}

/// A world of the brush entities of `entities`, of models 1 to 3, and a standing body.
class TriggerWorld {
public:
    explicit TriggerWorld(std::vector<KeyValues> entities, Vec3 body = kOutside) {
        EXPECT_TRUE(nx::game::registerComponents(m_world).has_value());
        std::vector<nx::assets::BspEntity> made;
        for (KeyValues& keyValues : entities) {
            made.push_back(nx::assets::BspEntity{.keyValues = std::move(keyValues)});
        }
        const std::vector models{nx::assets::BspModel{}, nx::assets::BspModel{.bounds = kDoorBounds},
                                 nx::assets::BspModel{.bounds = kDoorBounds},
                                 nx::assets::BspModel{.bounds = kTriggerBounds}};
        const nx::game::EntityClasses classes{
            .brushClasses = {nx::game::BrushClass{.classname = "func_door",
                                                  .solid = nx::game::Solid::Bsp,
                                                  .behavior = BrushBehavior::SlidingDoor},
                             nx::game::BrushClass{.classname = "trigger_multiple",
                                                  .solid = nx::game::Solid::Not,
                                                  .behavior = BrushBehavior::Trigger},
                             nx::game::BrushClass{.classname = "trigger_once",
                                                  .solid = nx::game::Solid::Not,
                                                  .behavior = BrushBehavior::TriggerOnce},
                             nx::game::BrushClass{.classname = "func_wall",
                                                  .solid = nx::game::Solid::Bsp,
                                                  .behavior = BrushBehavior::Wall}}};
        EXPECT_TRUE(nx::game::spawnBrushEntities(m_world, made, models, classes).has_value());
        m_body = m_world.spawn(
            Transform{.origin = body},
            nx::game::Body{.box = {.min = {-16.0F, -16.0F, -36.0F}, .max = {16.0F, 16.0F, 36.0F}}});
    }

    /// The body, moved to `at`, touches the triggers it is in.
    void touchAt(Vec3 at) {
        m_world.get<Transform>(m_body)->origin = at;
        nx::game::touchTriggers(m_world, m_hulls, m_body);
    }

    /// `ticks` ticks of what acts and of the pushers.
    void run(int ticks) {
        for (int tick = 0; tick < ticks; ++tick) {
            nx::game::runThinks(m_world, m_hulls);
            nx::game::runPushers(m_world, m_hulls);
            nx::game::advanceTick(m_world);
        }
    }

    /// The state of the door named `name`; none when there is none.
    [[nodiscard]] auto doorState(std::string_view name) -> std::optional<DoorState> {
        std::optional<DoorState> state;
        nx::ecs::Query<const Named&, const Door&>{m_world}.forEach([&](const Named& named, const Door& door) {
            if (named.name == name) {
                state = door.state;
            }
        });
        return state;
    }

    /// Whether the entity named `name` shows its alternate textures; none when there is none.
    [[nodiscard]] auto showsAlternateTextures(std::string_view name) -> std::optional<bool> {
        std::optional<bool> isShown;
        nx::ecs::Query<const Named&, const nx::game::AlternateTextures&>{m_world}.forEach(
            [&](const Named& named, const nx::game::AlternateTextures& textures) {
                if (named.name == name) {
                    isShown = textures.isShown;
                }
            });
        return isShown;
    }

    /// How many entities have a `Component`.
    template <class Component>
    [[nodiscard]] auto countOf() -> std::size_t {
        std::size_t count = 0;
        nx::ecs::Query<const Component&>{m_world}.forEach([&count](const Component&) { ++count; });
        return count;
    }

private:
    nx::collision::BrushHulls m_hulls = triggerHulls();
    nx::ecs::World m_world;
    nx::ecs::Entity m_body{};
};

/// A sliding door, model `model`, named `name`, open 1 second, and `extra`.
[[nodiscard]] auto namedDoor(std::string_view model, std::string_view name, KeyValues extra = {})
    -> KeyValues {
    KeyValues keyValues{{"classname", "func_door"},
                        {"model", std::string{model}},
                        {"angles", "0 0 0"},
                        {"speed", "100"},
                        {"wait", "1"},
                        {"targetname", std::string{name}}};
    keyValues.insert(keyValues.end(), extra.begin(), extra.end());
    return keyValues;
}

/// A trigger of `classname`, model 3, and `extra`.
[[nodiscard]] auto trigger(std::string_view classname, KeyValues extra) -> KeyValues {
    KeyValues keyValues{{"classname", std::string{classname}}, {"model", "*3"}};
    keyValues.insert(keyValues.end(), extra.begin(), extra.end());
    return keyValues;
}

TEST(TriggerTest, AWalkerInATriggerOpensTheDoorItNames) {
    TriggerWorld game{{namedDoor("*1", "gate"), trigger("trigger_multiple", {{"target", "gate"}})}};

    game.touchAt(kOutside);
    EXPECT_EQ(game.doorState("gate"), DoorState::Closed) << "out of the trigger";
    game.touchAt(Vec3{.x = 150.0F});
    EXPECT_EQ(game.doorState("gate"), DoorState::Closed) << "short of it";

    game.touchAt(kInside);
    EXPECT_EQ(game.doorState("gate"), DoorState::Opening);
    game.run(70);
    EXPECT_EQ(game.doorState("gate"), DoorState::Open);
}

TEST(TriggerTest, ADelayedTargetFiresWhenItsTimeComes) {
    TriggerWorld game{
        {namedDoor("*1", "gate"), trigger("trigger_multiple", {{"target", "gate"}, {"delay", "0.5"}})}};

    game.touchAt(kInside);
    EXPECT_EQ(game.countOf<DelayedUse>(), 1U);
    // Due at tick 50: it fires as the tick before it runs, as SV_RunThink runs what is due by the
    // frame's end.
    game.run(49);
    EXPECT_EQ(game.doorState("gate"), DoorState::Closed);
    game.run(1);
    EXPECT_EQ(game.doorState("gate"), DoorState::Opening);
    EXPECT_EQ(game.countOf<DelayedUse>(), 0U) << "gone once fired";
}

TEST(TriggerTest, ATriggerWaitsBeforeFiringAgain) {
    // Each firing leaves a delayed use behind, which counts them.
    TriggerWorld game{{namedDoor("*1", "gate"),
                       trigger("trigger_multiple", {{"target", "gate"}, {"delay", "10"}, {"wait", "1"}})}};

    game.touchAt(kInside);
    game.run(50);
    game.touchAt(kInside);
    EXPECT_EQ(game.countOf<DelayedUse>(), 1U) << "half a second later: still waiting";
    game.run(50);
    game.touchAt(kInside);
    EXPECT_EQ(game.countOf<DelayedUse>(), 2U) << "a second later: again";
}

TEST(TriggerTest, ATriggerOnceFiresOnceAndGoes) {
    TriggerWorld game{
        {namedDoor("*1", "gate"), trigger("trigger_once", {{"target", "gate"}, {"delay", "10"}})}};

    game.touchAt(kInside);
    game.touchAt(kInside);
    EXPECT_EQ(game.countOf<DelayedUse>(), 1U);
    EXPECT_EQ(game.countOf<Trigger>(), 1U);
    // A float tenth of a second is a hair more than 10 ticks: it goes in the 11th.
    game.run(10);
    EXPECT_EQ(game.countOf<Trigger>(), 1U);
    game.run(1);
    EXPECT_EQ(game.countOf<Trigger>(), 0U) << "gone a tenth of a second after";
    game.touchAt(kInside);
    EXPECT_EQ(game.countOf<DelayedUse>(), 1U);
}

TEST(TriggerTest, ATriggerFlaggedNoClientsIgnoresPlayers) {
    TriggerWorld game{
        {namedDoor("*1", "gate"), trigger("trigger_multiple", {{"target", "gate"}, {"spawnflags", "2"}})}};

    game.touchAt(kInside);
    EXPECT_EQ(game.doorState("gate"), DoorState::Closed);
}

TEST(TriggerTest, AKillTargetIsRemovedBeforeTheTargetFires) {
    TriggerWorld game{{namedDoor("*1", "gate"), namedDoor("*2", "wall"),
                       trigger("trigger_multiple", {{"target", "gate"}, {"killtarget", "wall"}})}};

    game.touchAt(kInside);
    EXPECT_EQ(game.doorState("wall"), std::nullopt);
    EXPECT_EQ(game.doorState("gate"), DoorState::Opening);
}

TEST(TriggerTest, AFuncWallFiredShowsItsOtherTextures) {
    TriggerWorld game{{{{"classname", "func_wall"}, {"model", "*2"}, {"targetname", "lights"}},
                       {{"classname", "func_wall"}, {"model", "*1"}, {"targetname", "lit"}, {"frame", "1"}},
                       trigger("trigger_multiple", {{"target", "lights"}})}};
    EXPECT_EQ(game.showsAlternateTextures("lights"), false);
    EXPECT_EQ(game.showsAlternateTextures("lit"), true) << "the map's frame";

    game.touchAt(kInside);
    EXPECT_EQ(game.showsAlternateTextures("lights"), true);
    game.run(25); // past the trigger's wait of 0.2 s
    game.touchAt(kInside);
    EXPECT_EQ(game.showsAlternateTextures("lights"), false) << "and back, fired again";
}

TEST(TriggerTest, ADoorFiresItsTargetsWhenItArrives) {
    // The first door, opened by the trigger, opens the second when it is open.
    TriggerWorld game{{namedDoor("*1", "first", {{"target", "second"}}), namedDoor("*2", "second"),
                       trigger("trigger_once", {{"target", "first"}})}};

    game.touchAt(kInside);
    game.run(60);
    EXPECT_EQ(game.doorState("first"), DoorState::Opening);
    EXPECT_EQ(game.doorState("second"), DoorState::Closed);
    game.run(10);
    EXPECT_EQ(game.doorState("first"), DoorState::Open);
    EXPECT_EQ(game.doorState("second"), DoorState::Opening);
}

} // namespace
