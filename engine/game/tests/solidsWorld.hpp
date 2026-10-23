// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Test helper: a world of hulls with a wall and a solid func_wall block in front of it, for the traces
// through what is solid. Include after <gtest/gtest.h>, `import std;`, `import nx.math;`,
// `import nx.ecs;`, `import nx.assets;`, `import nx.collision;` and `import nx.game;`.

namespace nx::test {

/// The world: solid from x = 100 on, for a line (hull 0). Model 1: solid from x = 0 to 64 in its own
/// space, across all y and z.
[[nodiscard]] inline auto wallAndBlock() -> nx::collision::BrushHulls {
    using nx::collision::Contents;
    using nx::collision::HullNode;
    using nx::collision::HullNodeIndex;
    using nx::collision::HullRoots;
    const auto facingX = [](float x) {
        return nx::Plane{.normal = {.x = 1.0F}, .distance = x};
    };
    std::vector<HullNode> nodes{HullNode{.plane = {0}, .children = {Contents::Solid, Contents::Empty}},
                                HullNode{.plane = {1}, .children = {HullNodeIndex{2}, Contents::Empty}},
                                HullNode{.plane = {2}, .children = {Contents::Empty, Contents::Solid}}};
    auto hulls = nx::collision::BrushHulls::create(nx::collision::BrushHullsDesc{
        .planes = {facingX(100.0F), facingX(0.0F), facingX(64.0F)},
        .pointNodes = std::move(nodes),
        .models = {nx::collision::BrushModel{.hulls = HullRoots{{HullNodeIndex{0}, Contents::Empty,
                                                                 Contents::Empty, Contents::Empty}}},
                   nx::collision::BrushModel{.hulls = HullRoots{{HullNodeIndex{1}, Contents::Empty,
                                                                 Contents::Empty, Contents::Empty}},
                                             .bounds = {.min = {.x = 0.0F, .y = -64.0F, .z = -64.0F},
                                                        .max = {.x = 64.0F, .y = 64.0F, .z = 64.0F}}}}});
    EXPECT_TRUE(hulls.has_value());
    return std::move(hulls).value();
}

/// A world of model 1 as a solid func_wall at `origin`.
class SolidsWorld {
public:
    explicit SolidsWorld(nx::Vec3 origin) {
        EXPECT_TRUE(nx::game::registerComponents(m_world).has_value());
        const std::vector entities{nx::assets::BspEntity{
            .keyValues = {{"classname", "func_wall"},
                          {"model", "*1"},
                          {"origin", std::format("{} {} {}", origin.x, origin.y, origin.z)}}}};
        const std::vector models{nx::assets::BspModel{}, nx::assets::BspModel{}};
        const nx::game::EntityClasses classes{
            .brushClasses = {nx::game::BrushClass{.classname = "func_wall",
                                                  .solid = nx::game::Solid::Bsp,
                                                  .behavior = nx::game::BrushBehavior::Wall}}};
        EXPECT_TRUE(nx::game::spawnBrushEntities(m_world, entities, models, classes).has_value());
        nx::ecs::Query<nx::ecs::Entity, const nx::game::BrushModel&>{m_world}.forEach(
            [this](nx::ecs::Entity entity, const nx::game::BrushModel&) { m_block = entity; });
    }

    nx::ecs::World m_world;
    nx::collision::BrushHulls m_hulls = wallAndBlock();
    nx::ecs::Entity m_block{};
};

} // namespace nx::test
