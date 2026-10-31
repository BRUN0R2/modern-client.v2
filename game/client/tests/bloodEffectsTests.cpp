// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The enhanced look's blood on the client (cs::BloodEffects), as data/cstrike/blood.toml's enhancedLook
// makes it: a burst by the spray's size and its part of the body, droplets and mists flying off it, some
// back, a head's gushing after it in pulses, the air slowing them alike at any frame rate, droplets
// fading out in the air or falling to the box map's floor (its hulls, floor at z = 0, 64 units each way)
// and leaving splats flat on it that lie and fade out, mists growing and fading, all within their room.

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
/// How far above the floor a trace that meets it stops: a 32nd of a unit, a little more or less along a
/// longer move, a tenth at most.
constexpr float kMostResting = 0.1F;

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

/// `look` with its body's droplets falling straight down, none back nor turned, without a mist, falling at
/// the world's gravity through little air for long enough to land, each that lands leaving a splat.
[[nodiscard]] auto fallingStraight(cs::BloodEnhancedLook look) -> cs::BloodEnhancedLook {
    look.body.spread = 0.0F;
    look.body.backShare = 0.0F;
    look.body.rise = nx::UnitsPerSecond{};
    look.body.mists = 0;
    look.gravity = nx::Scale{1.0F};
    look.drag = 0.8F;
    look.dropletLife = nx::Seconds{2.0F};
    look.dropletFade = {};
    look.splatShare = 1.0F;
    return look;
}

/// `spray` going straight down.
[[nodiscard]] constexpr auto goingDown(cs::BloodSpray spray) noexcept -> cs::BloodSpray {
    spray.direction = {.z = -1.0F};
    return spray;
}

/// The droplets of `size` a burst of `burst` throws.
[[nodiscard]] auto dropletsOf(const cs::BloodBurst& burst, std::uint32_t size) -> std::size_t {
    return static_cast<std::size_t>(std::lround(burst.dropletsPerSize * static_cast<float>(size)));
}

TEST_F(BloodEffectsTest, AHitElsewhereThanTheHeadBurstsByItsSizeBothWaysWithoutAGush) {
    blood.spray(kSpray, look, kShade, {});
    const std::size_t thrown = dropletsOf(look.body, kSpray.size);
    ASSERT_EQ(blood.droplets().size(), thrown);
    EXPECT_EQ(blood.mists().size(), look.body.mists);
    EXPECT_TRUE(blood.gushes().empty());
    // Each the way the hit went or back the way it came, turned off it by less than its speed.
    const auto forward = std::ranges::count_if(
        blood.droplets(), [](const cs::BloodDroplet& droplet) { return droplet.velocity.x > 0.0F; });
    EXPECT_GT(forward, 0) << "the way the hit went";
    EXPECT_LT(static_cast<std::size_t>(forward), thrown) << "and back the way it came";
    for (const cs::BloodDroplet& droplet : blood.droplets()) {
        EXPECT_EQ(droplet.position, kSpray.position);
        EXPECT_GE(droplet.radius, look.body.leastRadius);
        EXPECT_LE(droplet.radius, look.body.mostRadius);
        EXPECT_GE(std::abs(droplet.velocity.x), (1.0F - look.body.spread) * look.body.leastSpeed.value());
        EXPECT_EQ(droplet.shade.color, kShade.color);
        EXPECT_EQ(droplet.shade.light, kShade.light);
        EXPECT_EQ(droplet.goesAt, look.dropletLife) << "unless it lands first";
    }
}

TEST_F(BloodEffectsTest, AHitInTheHeadBurstsMoreThenGushesInPulsesEachSlowerThanTheLast) {
    blood.spray(inTheHead(kSpray), look, kShade, {});
    const std::size_t burst = dropletsOf(look.head, kSpray.size);
    EXPECT_GT(burst, dropletsOf(look.body, kSpray.size)) << "more than elsewhere";
    EXPECT_EQ(blood.droplets().size(), burst);
    EXPECT_EQ(blood.mists().size(), look.head.mists);
    ASSERT_EQ(blood.gushes().size(), 1U);
    // Without the time to move, the droplets stay where they were thrown, and only the pulses come.
    blood.update(look.head.gushInterval, {}, *hulls, kGravity, look);
    EXPECT_EQ(blood.droplets().size(), burst + look.head.gushDroplets) << "its first pulse";
    EXPECT_EQ(blood.mists().size(), look.head.mists + look.head.gushMists) << "and its mist";
    EXPECT_FLOAT_EQ(blood.gushes().front().speedShare, look.head.gushSlowing * look.head.gushSlowing)
        << "the next slower";
    const nx::Seconds gushed =
        (look.head.gushInterval * static_cast<float>(look.head.gushPulses)) + (look.head.gushInterval / 2.0F);
    blood.update(gushed, {}, *hulls, kGravity, look);
    EXPECT_EQ(blood.droplets().size(), burst + (look.head.gushPulses * look.head.gushDroplets))
        << "all pulses";
    // The burst's mists splashed and went before the last pulse; the last pulse's are there.
    const nx::Seconds lastPulse = gushed - look.head.gushInterval;
    EXPECT_EQ(std::ranges::count_if(
                  blood.mists(), [lastPulse](const cs::BloodMist& mist) { return mist.madeAt > lastPulse; }),
              look.head.gushMists);
    EXPECT_TRUE(blood.gushes().empty()) << "done";
    // A pulse's droplets the way the hit went, none back, as fast as gushSlowing of the burst's at most:
    // turned by up to `spread` of their speed on each axis, and up to `rise` faster upward.
    const float spread = look.head.spread;
    const float fastest =
        (look.head.mostSpeed.value() * look.head.gushSlowing * std::hypot(1.0F + spread, spread, spread)) +
        look.head.rise.value();
    for (const cs::BloodDroplet& droplet : blood.droplets() | std::views::drop(burst)) {
        EXPECT_GT(droplet.velocity.x, 0.0F);
        EXPECT_LE(nx::length(droplet.velocity), fastest);
    }
}

TEST_F(BloodEffectsTest, TheAirSlowsADropletAsMuchOverAFrameOfAnyLength) {
    cs::BloodEnhancedLook weightless = look;
    weightless.gravity = {};
    cs::BloodEffects other{1};
    blood.spray(kSpray, weightless, kShade, {});
    other.spray(kSpray, weightless, kShade, {});
    // A fiftieth of a second in ten frames, and in one: e^-drag a second either way; none flies the 32
    // units to a wall in it.
    constexpr nx::Seconds kShort{0.002F};
    nx::Seconds now{};
    for (int frame = 0; frame < 10; ++frame) {
        now += kShort;
        blood.update(now, kShort, *hulls, kGravity, weightless);
    }
    other.update(nx::Seconds{0.02F}, nx::Seconds{0.02F}, *hulls, kGravity, weightless);
    ASSERT_EQ(blood.droplets().size(), other.droplets().size()) << "none reached a wall";
    const float kept = std::exp(-weightless.drag * 0.02F);
    for (std::size_t index = 0; index < blood.droplets().size(); ++index) {
        const nx::Vec3 many = blood.droplets()[index].velocity;
        const nx::Vec3 one = other.droplets()[index].velocity;
        EXPECT_NEAR(many.x, one.x, 1e-2F);
        EXPECT_NEAR(many.y, one.y, 1e-2F);
        EXPECT_NEAR(many.z, one.z, 1e-2F);
    }
    cs::BloodEffects thrown{1};
    thrown.spray(kSpray, weightless, kShade, {});
    EXPECT_NEAR(other.droplets().front().velocity.x, thrown.droplets().front().velocity.x * kept, 1e-2F);
}

TEST_F(BloodEffectsTest, ADropletFadesOutInTheAirOverTheLastOfItsLife) {
    blood.spray(kSpray, look, kShade, {});
    ASSERT_FALSE(blood.droplets().empty());
    const nx::Seconds fades = look.dropletLife - look.dropletFade;
    const auto opacityAt = [this](nx::Seconds now) {
        const std::span<const nx::render::BloodInstance> shown = blood.shown(now, look);
        const auto droplet =
            std::ranges::find(shown, nx::render::BloodShape::Droplet, &nx::render::BloodInstance::shape);
        EXPECT_NE(droplet, shown.end());
        return droplet->opacity;
    };
    EXPECT_FLOAT_EQ(opacityAt(fades - kFrame), 1.0F) << "flying";
    EXPECT_NEAR(opacityAt(fades + (look.dropletFade / 2.0F)), 0.5F, 1e-4F) << "fading";
    EXPECT_NEAR(opacityAt(look.dropletLife - (look.dropletFade / 10.0F)), 0.1F, 1e-4F) << "nearly gone";
}

TEST_F(BloodEffectsTest, ADropletFallsUntilTheFloorStopsItAndLeavesASplatFlatOnIt) {
    const cs::BloodEnhancedLook falling = fallingStraight(look);
    blood.spray(goingDown(kSpray), falling, kShade, {});
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
        EXPECT_GE(splat.position.z, 0.0F) << "on the floor";
        EXPECT_LE(splat.position.z, kMostResting);
        EXPECT_EQ(splat.normal, (nx::Vec3{.z = 1.0F})) << "flat on it";
        EXPECT_GE(splat.radius.value(), look.body.leastRadius.value() * look.leastSplatGrowth.value());
        EXPECT_LE(splat.radius.value(), look.body.mostRadius.value() * look.mostSplatGrowth.value());
        EXPECT_EQ(splat.shade.color, kShade.color);
    }
}

TEST_F(BloodEffectsTest, ASplatLiesForItsLifeThenFadesOutOverItsLastFade) {
    const cs::BloodEnhancedLook falling = fallingStraight(look);
    blood.spray(goingDown(kSpray), falling, kShade, {});
    const std::size_t thrown = blood.droplets().size();
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
    EXPECT_LT(blood.splats().size(), thrown) << "the first gone";
}

TEST_F(BloodEffectsTest, AMistSplashesOutSlowingOpensOutAndFadesOutAsItGoes) {
    blood.spray(kSpray, look, kShade, {});
    ASSERT_EQ(blood.mists().size(), look.body.mists);
    const cs::BloodMist mist = blood.mists().front();
    const float widest = std::hypot(1.0F + look.body.mistSpread, look.body.mistSpread, look.body.mistSpread);
    EXPECT_LE(nx::length(mist.velocity), look.body.mostMistSpeed.value() * widest);
    for (const cs::BloodMist& each : blood.mists()) {
        EXPECT_GT(nx::length(each.velocity), 0.0F) << "none stays where the hit met the body";
    }
    const auto shownMist = [this](nx::Seconds now) {
        const std::span<const nx::render::BloodInstance> shown = blood.shown(now, look);
        const auto first =
            std::ranges::find(shown, nx::render::BloodShape::Mist, &nx::render::BloodInstance::shape);
        EXPECT_NE(first, shown.end());
        return *first;
    };
    const nx::render::BloodInstance made = shownMist({});
    EXPECT_FLOAT_EQ(made.radius.value(), mist.radius.value() * look.body.mistStartScale) << "small at first";
    EXPECT_FLOAT_EQ(made.opacity, look.body.mistOpacity) << "as opaque as it is made";
    EXPECT_FLOAT_EQ(made.light, kShade.light * look.mistLight) << "lit more than the droplets";
    EXPECT_EQ(made.seed, mist.seed) << "its smoke the same each frame";
    const nx::Seconds halfway = look.body.mistLife / 2.0F;
    const nx::render::BloodInstance grown = shownMist(halfway);
    // Halfway, a quarter of the way to its whole size, opening out faster as it goes.
    const float start = look.body.mistStartScale;
    EXPECT_NEAR(grown.radius.value(), mist.radius.value() * (start + ((1.0F - start) * 0.25F)), 1e-3F);
    EXPECT_NEAR(grown.opacity, look.body.mistOpacity, 1e-4F)
        << "wholly opaque until it fades out from halfway";
    const nx::render::BloodInstance fading = shownMist(look.body.mistLife * 0.75F);
    EXPECT_NEAR(fading.opacity, look.body.mistOpacity * 0.5F, 1e-3F) << "halfway through its fade";
    EXPECT_GT(fading.radius.value(), grown.radius.value());
    // A frame on, it moved by its velocity, which the air slowed.
    blood.update(kFrame, kFrame, *hulls, kGravity, look);
    const cs::BloodMist moved = blood.mists().front();
    EXPECT_NEAR(moved.position.x, mist.position.x + (mist.velocity.x * kFrame.count()), 1e-4F);
    EXPECT_NEAR(moved.velocity.x, mist.velocity.x * std::exp(-look.drag * kFrame.count()), 1e-3F);
    blood.update(look.body.mistLife, {}, *hulls, kGravity, look);
    EXPECT_TRUE(blood.mists().empty()) << "gone";
}

TEST_F(BloodEffectsTest, AHitInTheDarkIsLitAtLeastTheLooksLeastLight) {
    constexpr cs::BloodShade kInTheDark{.color = kShade.color, .light = 0.1F};
    blood.spray(kSpray, look, kInTheDark, {});
    ASSERT_FALSE(blood.droplets().empty());
    EXPECT_EQ(blood.droplets().front().shade.light, look.leastLight);
    EXPECT_EQ(blood.mists().front().shade.light, look.leastLight);
    const std::span<const nx::render::BloodInstance> shown = blood.shown({}, look);
    EXPECT_FLOAT_EQ(shown.back().light, look.leastLight * look.mistLight) << "a mist, lit more";
}

TEST_F(BloodEffectsTest, NoMistIsMadePastItsRoom) {
    cs::BloodEnhancedLook small = look;
    small.mostMists = 3;
    blood.spray(kSpray, small, kShade, {});
    EXPECT_EQ(blood.mists().size(), 3U) << "more asked for";
}

TEST_F(BloodEffectsTest, NoneIsMadePastItsRoomAndANewSplatTakesTheOldestsPlace) {
    cs::BloodEnhancedLook small = fallingStraight(look);
    small.mostDroplets = 6;
    small.mostSplats = 4;
    blood.spray(goingDown(kSpray), small, kShade, {});
    EXPECT_EQ(blood.droplets().size(), 6U) << "more asked for";
    std::ignore = play({}, 100, small);
    ASSERT_EQ(blood.splats().size(), 4U) << "the room for 4";
    blood.spray(goingDown(kSpray), small, kShade, nx::Seconds{1.0F});
    std::ignore = play(nx::Seconds{1.0F}, 100, small);
    ASSERT_EQ(blood.splats().size(), 4U);
    for (const cs::BloodSplat& splat : blood.splats()) {
        EXPECT_GT(splat.goesAt, small.splatLife + nx::Seconds{1.0F}) << "the newest kept";
    }
}

TEST_F(BloodEffectsTest, WhatIsShownIsTheSplatsUnderTheDropletsUnderTheMists) {
    blood.spray(kSpray, look, kShade, {});
    const std::size_t thrown = dropletsOf(look.body, kSpray.size);
    const std::span<const nx::render::BloodInstance> shown = blood.shown({}, look);
    ASSERT_EQ(shown.size(), thrown + look.body.mists);
    for (const nx::render::BloodInstance& droplet : shown.first(thrown)) {
        EXPECT_EQ(droplet.shape, nx::render::BloodShape::Droplet);
        EXPECT_EQ(droplet.color, kShade.color);
        EXPECT_EQ(droplet.light, kShade.light);
        EXPECT_FLOAT_EQ(droplet.opacity, 1.0F);
    }
    for (const nx::render::BloodInstance& mist : shown.last(look.body.mists)) {
        EXPECT_EQ(mist.shape, nx::render::BloodShape::Mist);
    }
}

} // namespace
