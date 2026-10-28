// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The client's temporary entities (docs/05, section 9): the blood a hit sprays, as R_BloodSprite makes
// it from the repository's data/cstrike/blood.toml, a spray playing its frames once and drops that fall
// at half the world's gravity and rest where they meet the world, here the box map's floor (its hulls
// have no walls).

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.assets;
import nx.collision;
import nx.render;
import cs.shared;
import cs.client;

#include "boxMap.hpp"

namespace {

/// A spray of the tests: in the middle of the box map, its size a 35-damage hit's.
constexpr cs::BloodSpray kSpray{.position = {.x = 32.0F, .y = 32.0F, .z = 32.0F}, .color = 247, .size = 7};
/// The frames of the tests' blood sprites.
constexpr cs::BloodSpriteFrames kFrames{.spray = 10, .drop = 12};
/// A frame of the tests, a hundredth of a second.
constexpr nx::Seconds kFrame{0.01F};
/// The world's gravity, sv_gravity's.
constexpr nx::UnitsPerSecondSquared kGravity{800.0F};
/// Frames enough for every drop to have met the floor, yet none gone: 0.95 seconds. The last to land,
/// thrown 95 units a second up from 32 above it and falling at 400 units a second squared, lands at 0.71.
constexpr std::uint32_t kRestingFrames = 95;
/// How far above the floor a trace that meets it stops: a 32nd of a unit.
constexpr float kRestingHeight = 0.03125F;

[[nodiscard]] auto repositoryLook() -> cs::BloodSprayLook {
    auto rules = cs::readBloodRules(
        nx::platform::readTextFile(std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "blood.toml")
            .value_or(std::string{}),
        "blood.toml");
    EXPECT_TRUE(rules.has_value());
    return std::move(rules).value_or(cs::BloodRules{}).sprayLook;
}

/// A palette whose entry n is red n.
[[nodiscard]] auto redPalette() -> nx::assets::Palette {
    std::vector<std::byte> bytes;
    for (std::size_t entry = 0; entry < nx::assets::kPaletteColors; ++entry) {
        bytes.insert(bytes.end(), {static_cast<std::byte>(entry), std::byte{0}, std::byte{0}});
    }
    auto palette = nx::assets::Palette::read(bytes);
    EXPECT_TRUE(palette.has_value());
    return std::move(palette).value_or(nx::assets::Palette{});
}

class TempEntitiesTest : public ::testing::Test {
protected:
    TempEntitiesTest() : bytes{nx::test::boxMapWith(R"({ "classname" "worldspawn" })")} {
        auto map = nx::assets::BspView::parse(bytes);
        EXPECT_TRUE(map.has_value());
        auto made = nx::collision::BrushHulls::create(map.value());
        EXPECT_TRUE(made.has_value());
        hulls.emplace(std::move(made).value());
    }

    [[nodiscard]] auto drops() const -> std::vector<cs::TempEntity> {
        return effects.shown() | std::views::filter([](const cs::TempEntity& each) {
                   return each.sprite == cs::TempSprite::BloodDrop;
               }) |
               std::ranges::to<std::vector>();
    }

    std::vector<std::byte> bytes;
    std::optional<nx::collision::BrushHulls> hulls;
    cs::BloodSprayLook look = repositoryLook();
    nx::assets::Palette palette = redPalette();
    cs::TempEntities effects{1};
};

TEST_F(TempEntitiesTest, ASprayShowsItsSpriteAboveTheHitAndItsDropsFlyingOff) {
    effects.sprayBlood(kSpray, look, palette, kFrames, {});
    ASSERT_FALSE(effects.shown().empty());
    const cs::TempEntity& spray = effects.shown().front();
    EXPECT_EQ(spray.sprite, cs::TempSprite::BloodSpray);
    EXPECT_EQ(spray.origin.x, kSpray.position.x);
    EXPECT_GE(spray.origin.z, kSpray.position.z + 2.0F) << "lifted 2 to 4";
    EXPECT_LE(spray.origin.z, kSpray.position.z + 4.0F);
    EXPECT_GE(spray.scale, 7.0F / 35.0F - 1e-5F);
    EXPECT_LE(spray.scale, 7.0F / 25.0F + 1e-5F);
    EXPECT_GE(spray.color.red, 248U) << "the server's entry plus 1 to 3";
    EXPECT_LE(spray.color.red, 250U);
    EXPECT_EQ(spray.amount, 250U);
    EXPECT_EQ(spray.goesAt, nx::Seconds{0.25F}) << "its frames once in a quarter of a second";
    EXPECT_FLOAT_EQ(spray.framerate.value(), 36.0F);

    const std::vector<cs::TempEntity> flying = drops();
    EXPECT_GE(flying.size(), 7U + 2U) << "its size and two dice of eight";
    EXPECT_LE(flying.size(), 7U + 16U);
    for (const cs::TempEntity& drop : flying) {
        EXPECT_EQ(drop.origin, kSpray.position);
        EXPECT_EQ(drop.color.red, spray.color.red - 1U) << "the entry before the spray's";
        EXPECT_GE(drop.velocity.x, -96.0F);
        EXPECT_LE(drop.velocity.x, 95.0F);
        EXPECT_GE(drop.velocity.z, -32.0F);
        EXPECT_LE(drop.velocity.z, 95.0F);
        EXPECT_TRUE((drop.frame >= 1.0F && drop.frame <= 8.0F) || drop.frame == 11.0F) << drop.frame;
        EXPECT_GE(drop.goesAt, nx::Seconds{1.0F});
        EXPECT_LE(drop.goesAt, nx::Seconds{3.0F});
        EXPECT_EQ(drop.gravity, nx::Scale{0.5F});
        EXPECT_TRUE(drop.isStoppedByWorld);
    }
}

TEST_F(TempEntitiesTest, TheSprayPlaysItsFramesOnceAndGoes) {
    effects.sprayBlood(kSpray, look, palette, {.spray = 10}, {});
    ASSERT_EQ(effects.shown().size(), 1U) << "no drops of a sprite the level has none of";
    effects.update(nx::Seconds{0.125F}, nx::Seconds{0.125F}, *hulls, kGravity);
    EXPECT_FLOAT_EQ(effects.shown().front().frame, 4.5F);
    effects.update(nx::Seconds{0.24F}, nx::Seconds{0.115F}, *hulls, kGravity);
    EXPECT_FLOAT_EQ(effects.shown().front().frame, 8.64F);
    effects.update(nx::Seconds{0.25F}, nx::Seconds{0.01F}, *hulls, kGravity);
    EXPECT_TRUE(effects.shown().empty());
}

TEST_F(TempEntitiesTest, DropsFallAtHalfTheGravityAndRestWhereTheyMeetTheWorld) {
    effects.sprayBlood(kSpray, look, palette, {.drop = 12}, {});
    const std::vector<cs::TempEntity> before = drops();
    ASSERT_FALSE(before.empty());
    effects.update(kFrame, kFrame, *hulls, kGravity);
    const std::vector<cs::TempEntity> after = drops();
    ASSERT_EQ(after.size(), before.size());
    for (std::size_t drop = 0; drop < before.size(); ++drop) {
        EXPECT_NEAR(after[drop].velocity.z, before[drop].velocity.z - 4.0F, 1e-4F) << "800 times a half";
        EXPECT_NEAR(after[drop].origin.x, before[drop].origin.x + (before[drop].velocity.x * kFrame.count()),
                    1e-4F);
    }
    for (std::uint32_t frame = 2; frame <= kRestingFrames; ++frame) {
        effects.update(kFrame * static_cast<float>(frame), kFrame, *hulls, kGravity);
    }
    const std::vector<cs::TempEntity> resting = drops();
    ASSERT_EQ(resting.size(), before.size()) << "none gone before a second";
    for (const cs::TempEntity& drop : resting) {
        EXPECT_EQ(drop.velocity, nx::Vec3{}) << "met the floor";
        EXPECT_NEAR(drop.origin.z, kRestingHeight, 1e-4F);
    }
}

TEST(TempSpritesTest, EachSpriteIsTheFileItsLookNames) {
    const cs::BloodSprayLook look = repositoryLook();
    EXPECT_EQ(cs::pathOf(cs::TempSprite::BloodSpray, look), "sprites/bloodspray.spr");
    EXPECT_EQ(cs::pathOf(cs::TempSprite::BloodDrop, look), "sprites/blood.spr");
}

} // namespace
