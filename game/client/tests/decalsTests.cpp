// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The decals a level shows, read from a decals.wad (the fuzz corpus' textures.wad: {BLUE and CRATE01),
// what a decal is stamped on, and the mark a bullet leaves on what it meets, as the game names it.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.ecs;
import nx.render;
import nx.game;
import cs.client;

#include "temporaryDirectory.hpp"

namespace {

using cs::BulletMark;
using cs::LevelDecals;

/// A brush entity of class `classname` with `keys`.
[[nodiscard]] auto entityOf(std::string_view classname,
                            std::initializer_list<std::pair<std::string, std::string>> keys = {})
    -> nx::assets::BspEntity {
    nx::assets::BspEntity entity{.keyValues = {{"classname", std::string{classname}}, {"model", "*1"}}};
    entity.keyValues.insert(entity.keyValues.end(), keys.begin(), keys.end());
    return entity;
}

TEST(BulletMarkTest, AnEntityDrawnNormallyTakesAGunshotOneDrawnOtherwiseBulletproofASolidOneNone) {
    EXPECT_EQ(cs::bulletMarkOf(entityOf("func_wall")), BulletMark::Gunshot);
    EXPECT_EQ(cs::bulletMarkOf(entityOf("func_door", {{"rendermode", "0"}})), BulletMark::Gunshot);
    for (const std::string_view mode : {"1", "2", "3", "5", "9"}) {
        EXPECT_EQ(cs::bulletMarkOf(entityOf("func_wall", {{"rendermode", std::string{mode}}})),
                  BulletMark::Bulletproof)
            << "rendermode " << mode << ", even one the renderer does not know";
    }
    EXPECT_EQ(cs::bulletMarkOf(entityOf("func_wall", {{"rendermode", "4"}})), BulletMark::None);
}

TEST(BulletMarkTest, ABreakableIsMarkedByItsMaterialGlassByDefault) {
    EXPECT_EQ(cs::bulletMarkOf(entityOf("func_breakable")), BulletMark::BrokenGlass);
    EXPECT_EQ(cs::bulletMarkOf(entityOf("func_breakable", {{"material", "0"}, {"rendermode", "4"}})),
              BulletMark::BrokenGlass)
        << "whatever its mode";
    EXPECT_EQ(cs::bulletMarkOf(entityOf("func_breakable", {{"material", "7"}})), BulletMark::Bulletproof);
    // Another material leaves the mark of how it is drawn, one the game does not number too (wood).
    EXPECT_EQ(cs::bulletMarkOf(entityOf("func_breakable", {{"material", "1"}})), BulletMark::Gunshot);
    EXPECT_EQ(cs::bulletMarkOf(entityOf("func_breakable", {{"material", "42"}, {"rendermode", "2"}})),
              BulletMark::Bulletproof);
}

TEST(BulletMarkTest, AMarkLeavesOneOfItsDecalsAtRandom) {
    nx::Random random{1, "bulletMarks"};
    EXPECT_EQ(cs::bulletDecalOf(BulletMark::None, random), std::nullopt);
    EXPECT_EQ(cs::bulletDecalOf(BulletMark::Bulletproof, random), std::optional{cs::kBulletproofDecal});
    std::set<std::string_view> gunshots;
    std::set<std::string_view> broken;
    for (int draw = 0; draw < 200; ++draw) {
        gunshots.insert(cs::bulletDecalOf(BulletMark::Gunshot, random).value_or(""));
        broken.insert(cs::bulletDecalOf(BulletMark::BrokenGlass, random).value_or(""));
    }
    EXPECT_EQ(gunshots, (std::set<std::string_view>{cs::kGunshotDecals.begin(), cs::kGunshotDecals.end()}));
    EXPECT_EQ(broken,
              (std::set<std::string_view>{cs::kBrokenGlassDecals.begin(), cs::kBrokenGlassDecals.end()}));
    EXPECT_EQ(cs::bulletDecals().size(), cs::kGunshotDecals.size() + cs::kBrokenGlassDecals.size() + 1);
}

/// A VFS of `folder`, with the corpus' textures.wad as its decals.wad when `hasDecals`.
[[nodiscard]] auto vfsWithDecals(const nx::test::TemporaryDirectory& folder, bool hasDecals)
    -> nx::assets::Vfs {
    if (hasDecals) {
        std::error_code error;
        std::filesystem::copy_file(std::filesystem::path{NX_FUZZ_CORPUS_DIRECTORY} / "wad" / "textures.wad",
                                   folder.path() / "decals.wad", error);
        EXPECT_FALSE(error) << error.message();
    }
    const std::array mounts{folder.path()};
    auto vfs = nx::assets::Vfs::create(mounts);
    EXPECT_TRUE(vfs.has_value()) << nx::describe(vfs.error());
    return std::move(vfs).value();
}

TEST(LevelDecalsTest, ReadsEachDecalNamedOnceAndFindsItWhateverItsCase) {
    const nx::test::TemporaryDirectory folder;
    const nx::assets::Vfs vfs = vfsWithDecals(folder, true);
    const std::array names{std::string_view{"crate01"}, std::string_view{"{shot1"}, std::string_view{"{BLUE"},
                           std::string_view{"CRATE01"}};
    const LevelDecals decals = LevelDecals::read(vfs, names);
    ASSERT_EQ(decals.images().size(), 2U);
    EXPECT_EQ(decals.indexOf("Crate01"), std::optional{nx::render::DecalImageIndex{0}});
    EXPECT_EQ(decals.indexOf("{blue"), std::optional{nx::render::DecalImageIndex{1}});
    EXPECT_TRUE(nx::equalsIgnoringCase(decals.images()[1].name, "{blue"));
    EXPECT_EQ(decals.indexOf("{shot1"), std::nullopt);
    ASSERT_EQ(decals.missing().size(), 1U);
    EXPECT_TRUE(decals.missing().front().starts_with("{shot1 (")) << decals.missing().front();
}

TEST(DecalTargetTest, ADecalOnABrushEntityIsOnItsModelWhereTheGameHasIt) {
    nx::ecs::World world;
    ASSERT_TRUE(nx::game::registerComponents(world).has_value());
    const std::vector entities{nx::assets::BspEntity{
        .keyValues = {{"classname", "func_wall"}, {"model", "*1"}, {"origin", "1 2 3"}}}};
    const std::vector models{nx::assets::BspModel{}, nx::assets::BspModel{}};
    const nx::game::EntityClasses classes{
        .brushClasses = {nx::game::BrushClass{.classname = "func_wall", .solid = nx::game::Solid::Bsp}}};
    ASSERT_TRUE(nx::game::spawnBrushEntities(world, entities, models, classes).has_value());
    std::optional<nx::ecs::Entity> wall;
    nx::ecs::Query<nx::ecs::Entity, const nx::game::BrushModel&>{world}.forEach(
        [&wall](nx::ecs::Entity entity, const nx::game::BrushModel&) { wall = entity; });
    ASSERT_TRUE(wall.has_value());
    const nx::render::DecalTarget on = cs::decalTargetOf(world, wall);
    EXPECT_EQ(on.model, nx::assets::ModelIndex{1});
    EXPECT_EQ(on.origin, (nx::Vec3{.x = 1.0F, .y = 2.0F, .z = 3.0F}));
    EXPECT_EQ(cs::decalTargetOf(world, std::nullopt).model, nx::assets::kWorldModel) << "the world";
}

TEST(LevelDecalsTest, WithoutDecalsWadEveryDecalIsLeftOut) {
    const nx::test::TemporaryDirectory folder;
    const nx::assets::Vfs vfs = vfsWithDecals(folder, false);
    const std::array names{std::string_view{"{shot1"}, std::string_view{"{shot2"}};
    const LevelDecals decals = LevelDecals::read(vfs, names);
    EXPECT_TRUE(decals.images().empty());
    EXPECT_EQ(decals.missing().size(), 2U);
    EXPECT_EQ(decals.indexOf("{shot1"), std::nullopt);
}

} // namespace
