// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The enhanced look's blood on the client (cs::BloodEffects), as data/cstrike/blood.toml's enhancedLook
// makes it: a burst by the spray's size and its part of the body, a head's gushing after it in pulses,
// droplets falling to the box map's floor (its hulls, floor at z = 0, 64 units each way) and leaving
// splats flat on it that lie and fade out, mists growing and fading, all within their room.

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

/// A spray of the tests: in the middle of the box map, its size a 35-damage hit's, the hit going along +X.
constexpr cs::BloodSpray kSpray{
    .position = {.x = 32.0F, .y = 32.0F, .z = 32.0F}, .direction = {.x = 1.0F}, .color = 247, .size = 10};
constexpr cs::BloodShade kShade{.color = {.red = 120, .green = 8, .blue = 6}, .light = 0.75F};
/// A frame of the tests, a hundredth of a second.
constexpr nx::Seconds kFrame{0.01F};
/// The world's gravity, sv_gravity's.
constexpr nx::UnitsPerSecondSquared kGravity{800.0F};
/// How far above the floor a trace that meets it stops: a 32nd of a unit.
constexpr float kResting = 0.03125F;

[[nodiscard]] auto repositoryLook() -> cs::BloodEnhancedLook {
    auto rules = cs::readBloodRules(
        nx::platform::readTextFile(std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "blood.toml")
            .value_or(std::string{}),
        "blood.toml");
    EXPECT_TRUE(rules.has_value()) << (rules ? "" : nx::describe(rules.error()));
    return std::move(rules).value_or(cs::BloodRules{}).enhancedLook;
}

/// `spray` met in the head.
[[nodiscard]] constexpr auto inTheHead(cs::BloodSpray spray) noexcept -> cs::BloodSpray {
    spray.hitGroup = cs::HitGroup::Head;
    return spray;
}

class BloodEffectsTest : public ::testing::Test {
protected:
    BloodEffectsTest() : bytes{nx::test::boxMapWith(R"({ "classname" "worldspawn" })")} {
        auto map = nx::assets::BspView::parse(bytes);
        EXPECT_TRUE(map.has_value());
        auto made = nx::collision::BrushHulls::create(map.value());
        EXPECT_TRUE(made.has_value());
        hulls.emplace(std::move(made).value());
    }

    /// Moves the blood a frame at a time from `from` for `frames` frames: the time it gets to.
    auto play(nx::Seconds from, std::uint32_t frames, const cs::BloodEnhancedLook& moved) -> nx::Seconds {
        nx::Seconds now = from;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            now += kFrame;
            blood.update(now, kFrame, *hulls, kGravity, moved);
        }
        return now;
    }

    std::vector<std::byte> bytes;
    std::optional<nx::collision::BrushHulls> hulls;
    cs::BloodEnhancedLook look = repositoryLook();
    cs::BloodEffects blood{1};
};

/// `look` with its body's droplets falling straight down, none back nor turned, without a mist, each
/// that lands leaving a splat.
[[nodiscard]] auto fallingStraight(cs::BloodEnhancedLook look) -> cs::BloodEnhancedLook {
    look.body.spread = 0.0F;
    look.body.backShare = 0.0F;
    look.body.rise = nx::UnitsPerSecond{};
    look.body.mists = 0;
    look.splatShare = 1.0F;
    return look;
}

TEST_F(BloodEffectsTest, AHitElsewhereThanTheHeadBurstsByItsSizeTheWayItWentWithoutAGush) {
    blood.spray(kSpray, look, kShade, {});
    ASSERT_EQ(blood.droplets().size(), 15U) << "1.5 for each of its size";
    EXPECT_EQ(blood.mists().size(), 2U);
    EXPECT_TRUE(blood.gushes().empty());
    const auto forward = std::ranges::count_if(
        blood.droplets(), [](const cs::BloodDroplet& droplet) { return droplet.velocity.x > 0.0F; });
    EXPECT_GT(forward, 15 / 2) << "most the way the hit went, a fifth back";
    for (const cs::BloodDroplet& droplet : blood.droplets()) {
        EXPECT_EQ(droplet.position, kSpray.position);
        EXPECT_GE(droplet.radius.value(), 0.2F);
        EXPECT_LE(droplet.radius.value(), 0.6F);
        EXPECT_EQ(droplet.shade.color, kShade.color);
        EXPECT_EQ(droplet.shade.light, kShade.light);
        EXPECT_EQ(droplet.goesAt, nx::Seconds{2.0F}) << "unless it lands first";
    }
}

TEST_F(BloodEffectsTest, AHitInTheHeadBurstsMoreThenGushesInPulsesEachSlowerThanTheLast) {
    blood.spray(inTheHead(kSpray), look, kShade, {});
    EXPECT_EQ(blood.droplets().size(), 30U) << "3 for each of its size";
    EXPECT_EQ(blood.mists().size(), 4U);
    ASSERT_EQ(blood.gushes().size(), 1U);
    // Without the time to move, the droplets stay where they were thrown, and only the pulses come.
    blood.update(nx::Seconds{0.15F}, {}, *hulls, kGravity, look);
    EXPECT_EQ(blood.droplets().size(), 30U + 8U) << "its first pulse";
    EXPECT_FLOAT_EQ(blood.gushes().front().speedShare, 0.8F * 0.8F) << "the next slower";
    blood.update(nx::Seconds{0.8F}, {}, *hulls, kGravity, look);
    EXPECT_EQ(blood.droplets().size(), 30U + (5U * 8U)) << "five pulses in all, the last at 0.75";
    EXPECT_TRUE(blood.gushes().empty()) << "done";
    // A pulse's droplets the way the hit went, none back, as fast as 0.8 of the burst's at most: turned
    // by up to 0.6 of their speed on each axis, and up to 60 faster upward.
    const float fastest = (260.0F * 0.8F * std::hypot(1.6F, 0.6F, 0.6F)) + 60.0F;
    for (const cs::BloodDroplet& droplet : blood.droplets() | std::views::drop(30)) {
        EXPECT_GT(droplet.velocity.x, 0.0F);
        EXPECT_LE(nx::length(droplet.velocity), fastest);
    }
}

TEST_F(BloodEffectsTest, ADropletFallsUntilTheFloorStopsItAndLeavesASplatFlatOnIt) {
    const cs::BloodEnhancedLook falling = fallingStraight(look);
    cs::BloodSpray down = kSpray;
    down.direction = {.z = -1.0F};
    blood.spray(down, falling, kShade, {});
    const std::size_t thrown = blood.droplets().size();
    const std::vector<cs::BloodDroplet> before{blood.droplets().begin(), blood.droplets().end()};
    std::ignore = play({}, 1, falling);
    ASSERT_EQ(blood.droplets().size(), thrown) << "a frame in, none on the floor yet";
    EXPECT_LT(blood.droplets().front().position.z, kSpray.position.z);
    EXPECT_LT(blood.droplets().front().velocity.z, before.front().velocity.z) << "falling faster";
    EXPECT_LT(nx::length(blood.droplets().front().velocity), nx::length(before.front().velocity) + 8.0F)
        << "slowed by the air as it falls";

    std::ignore = play(kFrame, 100, falling);
    EXPECT_TRUE(blood.droplets().empty()) << "all landed";
    ASSERT_EQ(blood.splats().size(), thrown) << "each leaving a splat";
    for (const cs::BloodSplat& splat : blood.splats()) {
        EXPECT_NEAR(splat.position.z, kResting, 1e-3F) << "on the floor";
        EXPECT_EQ(splat.normal, (nx::Vec3{.z = 1.0F})) << "flat on it";
        EXPECT_GE(splat.radius.value(), 0.2F * 2.0F);
        EXPECT_LE(splat.radius.value(), 0.6F * 3.5F);
        EXPECT_EQ(splat.shade.color, kShade.color);
    }
}

TEST_F(BloodEffectsTest, ASplatLiesForItsLifeThenFadesOutOverItsLastFade) {
    const cs::BloodEnhancedLook falling = fallingStraight(look);
    cs::BloodSpray down = kSpray;
    down.direction = {.z = -1.0F};
    blood.spray(down, falling, kShade, {});
    const nx::Seconds landed = play({}, 100, falling);
    ASSERT_FALSE(blood.splats().empty());
    const cs::BloodSplat first = blood.splats().front();
    EXPECT_NEAR((first.goesAt - first.fadesAt).count(), falling.splatFade.count(), 1e-4F);
    const auto opacityAt = [this, &falling](nx::Seconds now) {
        const std::span<const nx::render::BloodInstance> shown = blood.shown(now, falling);
        return shown.front().opacity;
    };
    EXPECT_EQ(blood.shown(landed, falling).front().shape, nx::render::BloodShape::Splat);
    EXPECT_EQ(blood.shown(landed, falling).front().normal, first.normal);
    EXPECT_FLOAT_EQ(opacityAt(first.fadesAt - kFrame), 1.0F) << "lying";
    EXPECT_NEAR(opacityAt(first.fadesAt + (falling.splatFade / 2.0F)), 0.5F, 1e-4F) << "fading";
    blood.update(first.goesAt, {}, *hulls, kGravity, falling);
    EXPECT_LT(blood.splats().size(), std::size_t{15}) << "the first gone";
}

TEST_F(BloodEffectsTest, AMistGrowsAndFadesOutAsItGoes) {
    blood.spray(kSpray, look, kShade, {});
    ASSERT_EQ(blood.mists().size(), 2U);
    const cs::BloodMist mist = blood.mists().front();
    const auto shownMist = [this](nx::Seconds now) {
        const std::span<const nx::render::BloodInstance> shown = blood.shown(now, look);
        const auto first =
            std::ranges::find(shown, nx::render::BloodShape::Mist, &nx::render::BloodInstance::shape);
        EXPECT_NE(first, shown.end());
        return *first;
    };
    const nx::render::BloodInstance made = shownMist({});
    EXPECT_FLOAT_EQ(made.radius.value(), mist.radius.value());
    EXPECT_FLOAT_EQ(made.opacity, 0.5F) << "as opaque as it is made";
    const nx::Seconds halfway = look.body.mistLife / 2.0F;
    const nx::render::BloodInstance grown = shownMist(halfway);
    EXPECT_NEAR(grown.radius.value(), mist.radius.value() + (14.0F * halfway.count()), 1e-4F);
    EXPECT_NEAR(grown.opacity, 0.25F, 1e-4F) << "half faded";
    blood.update(look.body.mistLife, {}, *hulls, kGravity, look);
    EXPECT_TRUE(blood.mists().empty()) << "gone";
}

TEST_F(BloodEffectsTest, NoneIsMadePastItsRoomAndANewSplatTakesTheOldestsPlace) {
    cs::BloodEnhancedLook small = fallingStraight(look);
    small.mostDroplets = 6;
    small.mostSplats = 4;
    cs::BloodSpray down = kSpray;
    down.direction = {.z = -1.0F};
    blood.spray(down, small, kShade, {});
    EXPECT_EQ(blood.droplets().size(), 6U) << "15 asked for";
    std::ignore = play({}, 100, small);
    ASSERT_EQ(blood.splats().size(), 4U) << "the room for 4";
    blood.spray(down, small, kShade, nx::Seconds{1.0F});
    std::ignore = play(nx::Seconds{1.0F}, 100, small);
    ASSERT_EQ(blood.splats().size(), 4U);
    for (const cs::BloodSplat& splat : blood.splats()) {
        EXPECT_GT(splat.goesAt, small.splatLife + nx::Seconds{1.0F}) << "the newest kept";
    }
}

TEST_F(BloodEffectsTest, WhatIsShownIsTheSplatsUnderTheDropletsUnderTheMists) {
    blood.spray(kSpray, look, kShade, {});
    const std::span<const nx::render::BloodInstance> shown = blood.shown({}, look);
    ASSERT_EQ(shown.size(), 15U + 2U);
    for (const nx::render::BloodInstance& droplet : shown.first(15)) {
        EXPECT_EQ(droplet.shape, nx::render::BloodShape::Droplet);
        EXPECT_EQ(droplet.color, kShade.color);
        EXPECT_EQ(droplet.light, kShade.light);
        EXPECT_FLOAT_EQ(droplet.opacity, 1.0F);
    }
    for (const nx::render::BloodInstance& mist : shown.last(2)) {
        EXPECT_EQ(mist.shape, nx::render::BloodShape::Mist);
    }
}

} // namespace
