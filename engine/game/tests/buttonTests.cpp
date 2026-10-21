// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Buttons pressed in by use or touch, firing their targets once in and coming back out; and what a
// player's use reaches.

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
using nx::game::Button;
using nx::game::ButtonState;
using nx::game::Door;
using nx::game::DoorState;
using nx::game::Named;
using nx::game::Transform;
using KeyValues = std::vector<std::pair<std::string, std::string>>;

/// The button's model, model 1: 8 units deep from x = 0, 32 wide and high. Pressed toward +X, it goes
/// 4 units in: its loaded depth, 10, less 2 and its lip of 4.
constexpr nx::Aabb kButtonBounds{.min = {.x = 0.0F, .y = -16.0F, .z = -16.0F},
                                 .max = {.x = 8.0F, .y = 16.0F, .z = 16.0F}};
/// The door's model, model 2, far from the button.
constexpr nx::Aabb kDoorBounds{.min = {.x = 500.0F, .y = -64.0F, .z = -64.0F},
                               .max = {.x = 564.0F, .y = 64.0F, .z = 64.0F}};
/// Ticks a button takes to go its 4 units at 40 a second.
constexpr int kButtonTravelTicks = 10;

/// The world, empty, and models 1 and 2, empty to every hull: nothing here stops anything.
[[nodiscard]] auto buttonHulls() -> nx::collision::BrushHulls {
    const nx::collision::HullRoots empty{
        {Contents::Empty, Contents::Empty, Contents::Empty, Contents::Empty}};
    auto hulls = nx::collision::BrushHulls::create(nx::collision::BrushHullsDesc{
        .models = {nx::collision::BrushModel{.hulls = empty},
                   nx::collision::BrushModel{.hulls = empty, .bounds = kButtonBounds},
                   nx::collision::BrushModel{.hulls = empty, .bounds = kDoorBounds}}});
    EXPECT_TRUE(hulls.has_value());
    return std::move(hulls).value();
}

/// A world of a button, model 1, with the keys of `extra` too, of a door named "gate", model 2, flagged
/// "use only", and of a player at `player`.
class ButtonWorld {
public:
    explicit ButtonWorld(const KeyValues& extra, Vec3 player = {.x = -40.0F}) {
        EXPECT_TRUE(nx::game::registerComponents(m_world).has_value());
        KeyValues buttonKeys{{"classname", "func_button"}, {"model", "*1"}, {"angles", "0 0 0"}};
        buttonKeys.insert(buttonKeys.end(), extra.begin(), extra.end());
        const std::vector entities{nx::assets::BspEntity{.keyValues = std::move(buttonKeys)},
                                   nx::assets::BspEntity{.keyValues = {{"classname", "func_door"},
                                                                       {"model", "*2"},
                                                                       {"angles", "0 0 0"},
                                                                       {"targetname", "gate"},
                                                                       {"spawnflags", "256"}}}};
        const std::vector models{nx::assets::BspModel{}, nx::assets::BspModel{.bounds = kButtonBounds},
                                 nx::assets::BspModel{.bounds = kDoorBounds}};
        const nx::game::EntityClasses classes{
            .brushClasses = {nx::game::BrushClass{.classname = "func_button",
                                                  .solid = nx::game::Solid::Bsp,
                                                  .behavior = BrushBehavior::Button},
                             nx::game::BrushClass{.classname = "func_door",
                                                  .solid = nx::game::Solid::Bsp,
                                                  .behavior = BrushBehavior::SlidingDoor}}};
        EXPECT_TRUE(nx::game::spawnBrushEntities(m_world, entities, models, classes).has_value());
        nx::ecs::Query<nx::ecs::Entity, const Button&>{m_world}.forEach(
            [this](nx::ecs::Entity entity, const Button&) { m_button = entity; });
        nx::ecs::Query<nx::ecs::Entity, const Door&>{m_world}.forEach(
            [this](nx::ecs::Entity entity, const Door&) { m_door = entity; });
        m_player = m_world.spawn(
            Transform{.origin = player},
            nx::game::Body{.box = {.min = {-16.0F, -16.0F, -36.0F}, .max = {16.0F, 16.0F, 36.0F}}});
    }

    void use() { nx::game::use(m_world, m_hulls, m_button, m_player); }

    void touch() {
        const std::array models{nx::collision::ModelIndex{1}};
        nx::game::touchBrushEntities(m_world, m_hulls, models, m_player);
    }

    /// The player, its eye at its origin, uses what is in front of it, facing `forward`.
    auto playerUses(Vec3 forward) -> std::optional<nx::ecs::Entity> {
        const Vec3 eye = m_world.get<Transform>(m_player)->origin;
        return nx::game::playerUse(m_world, m_hulls, m_player, {.eye = eye, .forward = forward});
    }

    void run(int ticks) {
        for (int tick = 0; tick < ticks; ++tick) {
            nx::game::runThinks(m_world, m_hulls);
            nx::game::runPushers(m_world, m_hulls);
            nx::game::advanceTick(m_world);
        }
    }

    [[nodiscard]] auto button() -> const Button& { return *m_world.get<Button>(m_button); }
    [[nodiscard]] auto buttonAt() -> Vec3 { return m_world.get<Transform>(m_button)->origin; }
    [[nodiscard]] auto buttonEntity() const -> nx::ecs::Entity { return m_button; }
    [[nodiscard]] auto doorEntity() const -> nx::ecs::Entity { return m_door; }
    [[nodiscard]] auto door() -> const Door& { return *m_world.get<Door>(m_door); }

private:
    nx::collision::BrushHulls m_hulls = buttonHulls();
    nx::ecs::World m_world;
    nx::ecs::Entity m_button{};
    nx::ecs::Entity m_door{};
    nx::ecs::Entity m_player{};
};

TEST(ButtonTest, AUsedButtonGoesInFiresItsTargetsAndComesBackOut) {
    ButtonWorld game{{{"target", "gate"}}};
    EXPECT_EQ(game.button().in, (Vec3{.x = 4.0F})) << "4 units in";

    game.use();
    EXPECT_EQ(game.button().state, ButtonState::GoingIn);
    game.run(kButtonTravelTicks + 1);
    EXPECT_EQ(game.button().state, ButtonState::In);
    EXPECT_EQ(game.buttonAt(), (Vec3{.x = 4.0F}));
    EXPECT_EQ(game.door().state, DoorState::Opening) << "its target fired once in";

    game.use();
    EXPECT_EQ(game.button().state, ButtonState::In) << "in, and no toggle: a use does nothing";
    game.run(100);
    EXPECT_EQ(game.button().state, ButtonState::GoingOut) << "a second in";
    game.run(kButtonTravelTicks + 1);
    EXPECT_EQ(game.button().state, ButtonState::Out);
    EXPECT_EQ(game.buttonAt(), Vec3{});
}

TEST(ButtonTest, AToggleComesBackOutOnlyWhenUsedAgain) {
    ButtonWorld game{{{"spawnflags", "32"}}};

    game.use();
    game.run(kButtonTravelTicks + 1);
    game.run(500);
    EXPECT_EQ(game.button().state, ButtonState::In);
    game.use();
    EXPECT_EQ(game.button().state, ButtonState::GoingOut);
}

TEST(ButtonTest, AButtonThatDoesNotMoveFiresAtOnce) {
    ButtonWorld game{{{"spawnflags", "1"}, {"target", "gate"}}};

    game.use();
    EXPECT_EQ(game.button().state, ButtonState::In);
    EXPECT_EQ(game.door().state, DoorState::Opening);
}

TEST(ButtonTest, AButtonPressedByTouchIgnoresUse) {
    ButtonWorld game{{{"spawnflags", "256"}}};

    game.use();
    EXPECT_EQ(game.button().state, ButtonState::Out);
    game.touch();
    EXPECT_EQ(game.button().state, ButtonState::GoingIn);
    game.touch();
    EXPECT_EQ(game.button().state, ButtonState::GoingIn) << "untouchable until its move is done";
}

TEST(ButtonTest, AButtonStaysInForGoodWithAWaitOfMinusOne) {
    ButtonWorld game{{{"wait", "-1"}}};

    game.use();
    game.run(500);
    EXPECT_EQ(game.button().state, ButtonState::In);
    game.use();
    EXPECT_EQ(game.button().state, ButtonState::In);
}

TEST(ButtonTest, APlayerUsesTheButtonInFrontOfIt) {
    ButtonWorld facing{KeyValues{}};
    EXPECT_EQ(facing.playerUses({.x = -1.0F}), std::nullopt) << "behind it";
    EXPECT_EQ(facing.playerUses({.y = 1.0F}), std::nullopt) << "to its side";
    EXPECT_EQ(facing.playerUses({.x = 1.0F}), facing.buttonEntity());
    EXPECT_EQ(facing.button().state, ButtonState::GoingIn);

    ButtonWorld far{KeyValues{}, Vec3{.x = -100.0F}};
    EXPECT_EQ(far.playerUses({.x = 1.0F}), std::nullopt) << "more than 64 units away";

    ButtonWorld shot{{{"health", "20"}}};
    EXPECT_EQ(shot.playerUses({.x = 1.0F}), std::nullopt) << "one that takes damage is shot, not used";
}

TEST(ButtonTest, APlayerUsesADoorUsedOnly) {
    // The door, flagged "use only", in front of a player 40 units short of it.
    ButtonWorld game{KeyValues{}, Vec3{.x = 460.0F}};
    EXPECT_EQ(game.playerUses({.x = 1.0F}), game.doorEntity());
    EXPECT_EQ(game.door().state, DoorState::Opening);
}

} // namespace
