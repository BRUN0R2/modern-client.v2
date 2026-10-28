// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The client's particles (docs/05, section 10): the stream of blood a hit makes a head shed, as
// R_BloodStream makes it from the repository's data/cstrike/blood.toml, each particle moving and falling
// at its share of the world's gravity, fading out as it goes; the sparks a hit strikes off a helmet, as
// R_StreakSplash makes them, each streak falling, slowing across the ground and fading out as
// CL_DrawTracers moves it; none past the original's most.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.render;
import cs.shared;
import cs.client;

namespace {

/// A stream of the tests: at 50 units a second along x, its splash 10 particles and their companions.
constexpr cs::BloodStream kStream{
    .position = {.x = 10.0F, .y = 20.0F, .z = 30.0F}, .direction = {.x = 1.0F}, .color = 223, .speed = 50};
/// Its particles: the stream's 100, and 50 over 5 splashed, each with two companions.
constexpr std::size_t kStreamParticles = 100;
constexpr std::size_t kSplashParticles = 30;
/// A helmet's sparks of the tests, off a face looking back along x.
constexpr cs::StreakSplash kSplash{.position = {.x = 10.0F, .y = 20.0F, .z = 30.0F},
                                   .direction = {.x = -1.0F},
                                   .color = 5,
                                   .count = 22,
                                   .speed = nx::UnitsPerSecond{25.0F},
                                   .spread = nx::UnitsPerSecond{65.0F}};
/// A frame of the tests, a hundredth of a second.
constexpr nx::Seconds kFrame{0.01F};
/// The world's gravity, sv_gravity's.
constexpr nx::UnitsPerSecondSquared kGravity{800.0F};
constexpr float kTolerance = 1e-4F;

[[nodiscard]] auto repositoryLook() -> cs::BloodStreamLook {
    auto rules = cs::readBloodRules(
        nx::platform::readTextFile(std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "blood.toml")
            .value_or(std::string{}),
        "blood.toml");
    EXPECT_TRUE(rules.has_value());
    return std::move(rules).value_or(cs::BloodRules{}).streamLook;
}

void expectNear(nx::Vec3 actual, nx::Vec3 expected, std::string_view what) {
    EXPECT_NEAR(actual.x, expected.x, kTolerance) << what;
    EXPECT_NEAR(actual.y, expected.y, kTolerance) << what;
    EXPECT_NEAR(actual.z, expected.z, kTolerance) << what;
}

class ParticlesTest : public ::testing::Test {
protected:
    cs::BloodStreamLook look = repositoryLook();
    cs::Particles particles{1};
};

TEST_F(ParticlesTest, AStreamIsAHundredParticlesEachDippedLessAndAStepSlowerThanTheOneBefore) {
    particles.streamBlood(kStream, look, {});
    ASSERT_EQ(particles.shown().size(), kStreamParticles + kSplashParticles);
    const std::span<const cs::Particle> stream = particles.shown().first(kStreamParticles);
    expectNear(stream[0].velocity, {.x = 50.0F, .z = -2.5F}, "its speed, dipped by 0.05");
    expectNear(stream[10].velocity, {.x = 40.0F}, "ten steps slower, its dip run out");
    expectNear(stream[20].velocity, {.x = 30.0F, .z = 1.5F}, "rising after the tenth");
    for (std::size_t index = 50; index < kStreamParticles; ++index) {
        EXPECT_EQ(stream[index].velocity, nx::Vec3{}) << "none slower than still: " << index;
    }
    for (const cs::Particle& particle : stream) {
        EXPECT_EQ(particle.origin, kStream.position);
        EXPECT_EQ(particle.gravity, nx::Scale{0.4F});
        EXPECT_EQ(particle.goesAt, nx::Seconds{2.0F});
        EXPECT_GE(particle.color, 223U) << "the server's entry and 0 to 9 after it";
        EXPECT_LE(particle.color, 232U);
    }
}

TEST_F(ParticlesTest, ItsSplashIsAParticleForEachFiveOfItsSpeedWithTwoCompanionsADipLess) {
    particles.streamBlood(kStream, look, {});
    const std::span<const cs::Particle> splash = particles.shown().subspan(kStreamParticles);
    ASSERT_EQ(splash.size(), kSplashParticles);
    float dip = look.firstSplashDip;
    for (std::size_t group = 0; group < kSplashParticles / 3; ++group) {
        const cs::Particle& splashed = splash[group * 3];
        // Along x, its velocity is its speed: a share of 50 cut to a whole number, times that share and 1.7.
        const float speed = splashed.velocity.x;
        EXPECT_GE(speed, 0.0F);
        EXPECT_LE(speed, 50.0F * 1.7F);
        EXPECT_NEAR(splashed.velocity.z, -dip * speed, kTolerance) << group;
        EXPECT_EQ(splashed.origin, kStream.position);
        dip -= look.dipStep;
        for (std::size_t companion = 1; companion <= 2; ++companion) {
            const cs::Particle& with = splash[(group * 3) + companion];
            EXPECT_FLOAT_EQ(with.velocity.x, speed) << "as fast";
            EXPECT_NEAR(with.velocity.z, -dip * speed, kTolerance) << "a step less dipped";
            const nx::Vec3 off = with.origin - kStream.position;
            EXPECT_NEAR(off.x, off.y, kTolerance) << "as much off on each axis";
            EXPECT_NEAR(off.x, off.z, kTolerance);
            EXPECT_LE(std::abs(off.x), 1.0F);
        }
    }
    for (const cs::Particle& particle : splash) {
        EXPECT_EQ(particle.gravity, nx::Scale{0.2F});
        EXPECT_EQ(particle.goesAt, nx::Seconds{3.0F});
    }
}

TEST_F(ParticlesTest, AStreamsWayIsMadeOfLengthOneAndAStreamOfNoWayOnlyDips) {
    cs::BloodStream longer = kStream;
    longer.direction = {.x = 4.0F};
    particles.streamBlood(longer, look, {});
    expectNear(particles.shown().front().velocity, {.x = 50.0F, .z = -2.5F}, "as a way of length one");

    cs::Particles still{1};
    cs::BloodStream nowhere = kStream;
    nowhere.direction = {};
    still.streamBlood(nowhere, look, {});
    expectNear(still.shown().front().velocity, {.z = -2.5F}, "no way but its dip");
    expectNear(still.shown()[10].velocity, {}, "its dip run out");
}

TEST_F(ParticlesTest, ParticlesMoveThenFallAtTheirShareOfTheGravityAndGoAtTheirTime) {
    particles.streamBlood(kStream, look, {});
    const std::vector<cs::Particle> before = particles.shown() | std::ranges::to<std::vector>();
    particles.update(kFrame, kFrame, kGravity);
    ASSERT_EQ(particles.shown().size(), before.size());
    for (std::size_t index = 0; index < before.size(); ++index) {
        const cs::Particle& after = particles.shown()[index];
        expectNear(after.origin, before[index].origin + (before[index].velocity * kFrame.count()), "moved");
        const float fall = index < kStreamParticles ? 3.2F : 1.6F; // 800 times 0.4 and 0.2, a hundredth
        EXPECT_NEAR(after.velocity.z, before[index].velocity.z - fall, kTolerance) << index;
    }
    particles.update(nx::Seconds{2.0F}, kFrame, kGravity);
    EXPECT_EQ(particles.shown().size(), kSplashParticles) << "the stream gone at 2 seconds";
    particles.update(nx::Seconds{3.0F}, kFrame, kGravity);
    EXPECT_TRUE(particles.shown().empty());
}

TEST(ParticleAmountTest, AParticleIsOpaqueUntilItsLastSixteenthOfASecond) {
    const cs::Particle particle{.goesAt = nx::Seconds{1.0F}};
    EXPECT_EQ(cs::amountOf(particle, nx::Seconds{0.5F}), 255U);
    EXPECT_EQ(cs::amountOf(particle, nx::Seconds{1.0F - (1.0F / 16.0F)}), 255U);
    EXPECT_EQ(cs::amountOf(particle, nx::Seconds{1.0F - (1.0F / 32.0F)}), 127U) << "255 times a half";
    EXPECT_EQ(cs::amountOf(particle, nx::Seconds{1.0F}), 0U);
}

TEST_F(ParticlesTest, NoneIsMadePastTheOriginalsMost) {
    cs::BloodStream fastest = kStream;
    fastest.speed = 255; // 100 and 51 splashed with two companions each: 253
    for (std::uint32_t stream = 0; stream < 20; ++stream) {
        particles.streamBlood(fastest, look, {});
    }
    EXPECT_EQ(particles.shown().size(), cs::kMostParticles);
}

TEST_F(ParticlesTest, ASplashIsItsCountOfStreaksAlongItsWayEachDrawnOffAsMuchOnEachAxis) {
    particles.splashStreaks(kSplash, {});
    ASSERT_EQ(particles.shownStreaks().size(), 22U);
    for (const cs::Streak& streak : particles.shownStreaks()) {
        EXPECT_EQ(streak.origin, kSplash.position);
        // Its way has no y: what it has of y is what was drawn off it.
        const float off = streak.velocity.y;
        EXPECT_LE(std::abs(off), 65.0F);
        EXPECT_NEAR(streak.velocity.x, -25.0F + off, kTolerance);
        EXPECT_NEAR(streak.velocity.z, off, kTolerance);
        EXPECT_EQ(streak.color, (nx::render::RenderColor{.red = 255, .green = 167, .blue = 17}))
            << "sparks' yellow and orange";
        EXPECT_GE(streak.goesAt, nx::Seconds{0.1F});
        EXPECT_LE(streak.goesAt, nx::Seconds{0.5F});
    }
}

TEST_F(ParticlesTest, AStreakMovesThenFallsAtTheWholeGravitySlowingAcrossTheGround) {
    particles.splashStreaks(kSplash, {});
    const std::vector<cs::Streak> before = particles.shownStreaks() | std::ranges::to<std::vector>();
    particles.update(kFrame, kFrame, kGravity);
    ASSERT_EQ(particles.shownStreaks().size(), before.size()) << "none gone before a tenth";
    for (std::size_t index = 0; index < before.size(); ++index) {
        const cs::Streak& after = particles.shownStreaks()[index];
        const cs::Streak& was = before[index];
        expectNear(after.origin, was.origin + (was.velocity * kFrame.count()), "moved");
        EXPECT_NEAR(after.velocity.x, was.velocity.x * (1.0F - 0.009F), kTolerance) << "0.9 a second";
        EXPECT_NEAR(after.velocity.y, was.velocity.y * (1.0F - 0.009F), kTolerance);
        EXPECT_NEAR(after.velocity.z, was.velocity.z - 8.0F, kTolerance) << "800 a second, a hundredth";
    }
    particles.update(nx::Seconds{0.5F}, kFrame, kGravity);
    EXPECT_TRUE(particles.shownStreaks().empty()) << "all gone by half a second";
}

TEST(StreakTest, AStreakIsDrawnAsFarAsItMovesInItsTimeLeftATenthAtMostFadingInItsLastHalfSecond) {
    const cs::Streak streak{.velocity = {.x = 100.0F}, .goesAt = nx::Seconds{1.0F}};
    expectNear(cs::endOf(streak, {}), {.x = 10.0F}, "a tenth of a second of it");
    expectNear(cs::endOf(streak, nx::Seconds{0.95F}), {.x = 5.0F}, "the twentieth it has left");
    EXPECT_EQ(cs::amountOf(streak, nx::Seconds{0.5F}), 255U);
    EXPECT_EQ(cs::amountOf(streak, nx::Seconds{0.75F}), 127U) << "255 times a half";
    EXPECT_EQ(cs::amountOf(streak, nx::Seconds{1.0F}), 0U);
}

TEST(StreakTest, AStreakColorIsTheOriginalsByItsIndexTheTracerCvarsPastThem) {
    EXPECT_EQ(cs::streakColorOf(0), (nx::render::RenderColor{.red = 255, .green = 255, .blue = 255}));
    EXPECT_EQ(cs::streakColorOf(5), (nx::render::RenderColor{.red = 255, .green = 167, .blue = 17}));
    EXPECT_EQ(cs::streakColorOf(11), (nx::render::RenderColor{.red = 255, .green = 120, .blue = 70}));
    EXPECT_EQ(cs::streakColorOf(12), (nx::render::RenderColor{.red = 102, .green = 102, .blue = 51}));
}

TEST_F(ParticlesTest, ParticlesAndStreaksShareTheOriginalsMost) {
    cs::StreakSplash many = kSplash;
    many.count = 5000;
    particles.splashStreaks(many, {});
    EXPECT_EQ(particles.shownStreaks().size(), cs::kMostParticles);
    particles.streamBlood(kStream, look, {});
    EXPECT_TRUE(particles.shown().empty()) << "no room left";
}

} // namespace
