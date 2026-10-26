// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// How a player is hurt and killed, as the original's CBasePlayer::TakeDamage, TakeHealth, PostThink and
// CheckTimeBasedDamage do it: the repository's data/cstrike/playerHarm.toml and what its reader
// refuses, the damage of a fall by its speed, the landings that deal it, the whole points a player's
// health loses until it dies, the harms it keeps over time, and the drowned health given back.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.collision;
import nx.game;
import nx.platform;
import cs.shared;

namespace {

using cs::FallDamage;
using cs::HarmKind;
using cs::MovementState;
using cs::PlayerHarm;
using cs::PlayerHarmRules;
using cs::PlayerStatus;
using cs::TimedHarms;
using nx::collision::Contents;

constexpr nx::TickRate kRate = nx::kDefaultTickRate;
constexpr nx::HitPoints kGivenBack{10.0F};

/// The repository's data/cstrike/playerHarm.toml, read.
[[nodiscard]] auto repositoryRules() -> PlayerHarmRules {
    const std::filesystem::path file =
        std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "playerHarm.toml";
    auto text = nx::platform::readTextFile(file);
    EXPECT_TRUE(text.has_value());
    auto rules = cs::readPlayerHarmRules(text.value_or(std::string{}), "playerHarm.toml");
    EXPECT_TRUE(rules.has_value()) << (rules ? "" : nx::describe(rules.error()));
    return rules.value_or(PlayerHarmRules{});
}

/// The moment `seconds` after the map started, a whole number of ticks.
[[nodiscard]] auto at(double seconds) -> nx::GameTime {
    return nx::GameTime::atTick(std::llround(seconds * kRate.value()));
}

/// A harm of `damage` and `kind`.
[[nodiscard]] auto harm(float damage, HarmKind kind = HarmKind::Fall) -> PlayerHarm {
    return PlayerHarm{.damage = nx::Damage{damage}, .kind = kind};
}

/// Kept harms that drowned 3, 4 and 5, the player's head out of water again.
[[nodiscard]] auto drownedTwelve() -> TimedHarms {
    TimedHarms timed;
    for (const float damage : {3.0F, 4.0F, 5.0F}) {
        timed.took(HarmKind::Drown);
        timed.drowned(nx::Damage{damage});
    }
    timed.headOutOfWater();
    return timed;
}

/// A player in the air, falling at `speed` units a second.
[[nodiscard]] auto falling(float speed) -> MovementState {
    return MovementState{.velocity = {.z = -speed}};
}

/// A player on the ground, in `contents` up to its feet when it is wet.
[[nodiscard]] auto landed(Contents contents = Contents::Empty) -> MovementState {
    MovementState state{.isOnGround = true};
    if (contents != Contents::Empty) {
        state.waterLevel = cs::WaterLevel::Feet;
        state.waterType = contents;
    }
    return state;
}

TEST(PlayerHarmTest, TheRepositorysHarmIsTheOriginals) {
    const PlayerHarmRules rules = repositoryRules();
    EXPECT_EQ(rules.fall.safeSpeed, nx::UnitsPerSecond{500.0F});
    EXPECT_EQ(rules.fall.fatalSpeed, nx::UnitsPerSecond{1100.0F});
    EXPECT_EQ(rules.fall.damageAtFatalSpeed, nx::Damage{100.0F});
    EXPECT_EQ(rules.fall.scale, 1.25);
    EXPECT_EQ(rules.drowning.interval, nx::Seconds{1.0F});
    EXPECT_EQ(rules.drowning.start, nx::Damage{2.0F});
    EXPECT_EQ(rules.drowning.step, nx::Damage{1.0F});
    EXPECT_EQ(rules.drowning.most, nx::Damage{5.0F});
    EXPECT_EQ(rules.drowning.givenBack, nx::HitPoints{10.0F});
    EXPECT_EQ(rules.harmfulWater.slime, nx::Damage{4.0F});
    EXPECT_EQ(rules.harmfulWater.lava, nx::Damage{10.0F});
    EXPECT_EQ(rules.pain.files,
              (std::vector<std::string>{"player/bhit_flesh-1.wav", "player/bhit_flesh-2.wav",
                                        "player/bhit_flesh-3.wav"}));
    EXPECT_EQ(rules.pain.channel, cs::SoundChannel::Voice);
    EXPECT_EQ(rules.splat.files, (std::vector<std::string>{"common/bodysplat.wav"}));
    EXPECT_EQ(rules.splat.channel, cs::SoundChannel::Item);
    EXPECT_EQ(rules.death.files, (std::vector<std::string>{"player/die1.wav", "player/die2.wav",
                                                           "player/die3.wav", "player/death6.wav"}));
    EXPECT_EQ(rules.death.channel, cs::SoundChannel::Voice);
    EXPECT_EQ(rules.soundFiles().size(), 8U);
}

/// A file of the harm: `painFiles` for its pain, and a fall from `safeSpeed` to `fatalSpeed`.
[[nodiscard]] auto harmFile(std::string_view painFiles, float safeSpeed, float fatalSpeed) -> std::string {
    return std::format(R"(pain = {{ files = [{}], channel = "voice", volume = 1.0 }}
splat = {{ files = ["b.wav"], channel = "item", volume = 1.0 }}
death = {{ files = ["c.wav"], channel = "voice", volume = 1.0 }}

[fall]
safeSpeed = {:.1f}
fatalSpeed = {:.1f}
damageAtFatalSpeed = 100.0
scale = 1.25

[drowning]
interval = 1.0
start = 2.0
step = 1.0
most = 5.0
givenBack = 10.0

[harmfulWater]
slime = 4.0
lava = 10.0
)",
                       painFiles, safeSpeed, fatalSpeed);
}

TEST(PlayerHarmTest, TheReaderRefusesAFatalSpeedNotAboveTheSafeOneAndASoundWithoutAFile) {
    EXPECT_TRUE(cs::readPlayerHarmRules(harmFile(R"("a.wav")", 500.0F, 1100.0F), "harm.toml").has_value());
    EXPECT_FALSE(cs::readPlayerHarmRules(harmFile(R"("a.wav")", 500.0F, 500.0F), "same.toml").has_value());
    EXPECT_FALSE(cs::readPlayerHarmRules(harmFile(R"("a.wav")", 600.0F, 500.0F), "slower.toml").has_value());
    EXPECT_FALSE(cs::readPlayerHarmRules(harmFile("", 500.0F, 1100.0F), "silent.toml").has_value());
}

TEST(PlayerHarmTest, AFallDealsItsSpeedBeyondTheSafeOneInProportion) {
    // FlPlayerFallDamage: (speed - 500) times 100 / 600 in float, times 1.25.
    const FallDamage fall{};
    EXPECT_EQ(cs::fallDamageOf(fall, nx::UnitsPerSecond{600.0F}),
              nx::Damage{static_cast<float>(double{100.0F * (100.0F / 600.0F)} * 1.25)});
    EXPECT_EQ(cs::fallDamageOf(fall, nx::UnitsPerSecond{1100.0F}), nx::Damage{125.0F});
    EXPECT_EQ(cs::fallDamageOf(fall, nx::UnitsPerSecond{500.0F}), nx::Damage{});
}

TEST(PlayerHarmTest, OnlyALandingFasterThanTheSafeSpeedOutOfWaterHurts) {
    const FallDamage fall{};
    EXPECT_EQ(cs::landingDamageOf(fall, falling(600.0F), landed()),
              std::optional{cs::fallDamageOf(fall, nx::UnitsPerSecond{600.0F})});
    EXPECT_EQ(cs::landingDamageOf(fall, falling(500.0F), landed()), std::nullopt) << "the safe speed itself";
    EXPECT_EQ(cs::landingDamageOf(fall, falling(900.0F), falling(908.0F)), std::nullopt)
        << "still in the air";
    EXPECT_EQ(cs::landingDamageOf(fall, landed(), landed()), std::nullopt) << "on the ground before";
    EXPECT_EQ(cs::landingDamageOf(fall, falling(900.0F), landed(Contents::Water)), std::nullopt);
    EXPECT_TRUE(cs::landingDamageOf(fall, falling(900.0F), landed(Contents::Slime)).has_value())
        << "slime spares no one";
    EXPECT_EQ(cs::landingDamageOf(FallDamage{.scale = 0.0}, falling(900.0F), landed()), std::nullopt)
        << "a fall that deals none";
}

TEST(PlayerHarmTest, APlayerLosesTheWholePointsOfADamageAndDiesWithNoneLeft) {
    PlayerStatus status{};
    TimedHarms timed;
    const nx::game::Harm hurt = cs::harmPlayer(status, timed, harm(20.8F));
    EXPECT_EQ(hurt.damage, nx::Damage{20.0F});
    EXPECT_FALSE(hurt.isKilled);
    EXPECT_EQ(status.health, nx::HitPoints{80.0F});
    EXPECT_TRUE(status.isAlive());

    EXPECT_EQ(cs::harmPlayer(status, timed, harm(0.9F)).damage, nx::Damage{}) << "less than a point: none";
    EXPECT_EQ(status.health, nx::HitPoints{80.0F});

    const nx::game::Harm killed = cs::harmPlayer(status, timed, harm(125.0F));
    EXPECT_EQ(killed.damage, nx::Damage{125.0F});
    EXPECT_TRUE(killed.isKilled);
    EXPECT_EQ(status.health, nx::HitPoints{-45.0F}) << "below none, as the original's health goes";
    EXPECT_FALSE(status.isAlive());

    const nx::game::Harm again = cs::harmPlayer(status, timed, harm(10.0F));
    EXPECT_EQ(again.damage, nx::Damage{}) << "a dead player takes none";
    EXPECT_FALSE(again.isKilled);
    EXPECT_EQ(status.health, nx::HitPoints{-45.0F});
}

TEST(PlayerHarmTest, APlayerLeftWithExactlyNoneIsKilled) {
    PlayerStatus status{.health = nx::HitPoints{20.0F}};
    TimedHarms timed;
    EXPECT_TRUE(cs::harmPlayer(status, timed, harm(20.9F)).isKilled);
    EXPECT_EQ(status.health, nx::HitPoints{});
}

TEST(PlayerHarmTest, WhatDrowningTookIsGivenBackTenAtATimeTwoSecondsApart) {
    TimedHarms timed = drownedTwelve();
    EXPECT_EQ(timed.check(kGivenBack, at(1.99), kRate), nx::HitPoints{})
        << "two seconds from the map's start";
    EXPECT_EQ(timed.check(kGivenBack, at(2.0), kRate), nx::HitPoints{10.0F});
    EXPECT_EQ(timed.check(kGivenBack, at(3.99), kRate), nx::HitPoints{});
    EXPECT_EQ(timed.check(kGivenBack, at(4.0), kRate), nx::HitPoints{2.0F}) << "the rest";
    EXPECT_EQ(timed.check(kGivenBack, at(6.0), kRate), nx::HitPoints{}) << "all is given back";
}

TEST(PlayerHarmTest, NothingIsGivenBackWithTheHeadUnderWaterNorAfterTheSpawn) {
    TimedHarms under = drownedTwelve();
    under.headUnderWater(false);
    EXPECT_EQ(under.check(kGivenBack, at(2.0), kRate), nx::HitPoints{});
    under.headOutOfWater();
    EXPECT_EQ(under.check(kGivenBack, at(4.0), kRate), nx::HitPoints{10.0F}) << "out of water again";

    TimedHarms spawned = drownedTwelve();
    spawned.spawned();
    spawned.headOutOfWater();
    EXPECT_EQ(spawned.check(kGivenBack, at(2.0), kRate), nx::HitPoints{}) << "what it took is forgotten";
}

TEST(PlayerHarmTest, TheKeptHarmsAreLookedAtOnlyWhileOneIsKept) {
    // Acid is kept two looks after its first: looked at 2, 4 and 6 seconds, and no more after, so that
    // the giving back of a drowning at 8.5 seconds does not wait for a look two seconds after 8.
    TimedHarms timed;
    timed.took(HarmKind::Acid);
    for (const double seconds : {2.0, 4.0, 6.0, 8.0}) {
        EXPECT_EQ(timed.check(kGivenBack, at(seconds), kRate), nx::HitPoints{}) << seconds;
    }
    timed.took(HarmKind::Drown);
    timed.drowned(nx::Damage{3.0F});
    timed.headOutOfWater();
    EXPECT_EQ(timed.check(kGivenBack, at(8.5), kRate), nx::HitPoints{3.0F});

    // Looked at 2, 4 and 6 seconds, acid holds the giving back at 7 seconds until 8.
    TimedHarms held;
    held.took(HarmKind::Acid);
    for (const double seconds : {2.0, 4.0, 6.0}) {
        std::ignore = held.check(kGivenBack, at(seconds), kRate);
    }
    held.took(HarmKind::Drown);
    held.drowned(nx::Damage{3.0F});
    held.headOutOfWater();
    EXPECT_EQ(held.check(kGivenBack, at(7.0), kRate), nx::HitPoints{});
    EXPECT_EQ(held.check(kGivenBack, at(8.0), kRate), nx::HitPoints{3.0F});
}

TEST(PlayerHarmTest, AHarmThatKillsIsKeptButNoneIsKeptByTheDead) {
    // Acid kept by a harm that kills holds the looks two seconds apart; the dead keep nothing.
    PlayerStatus dying{.health = nx::HitPoints{4.0F}};
    TimedHarms kept;
    EXPECT_TRUE(cs::harmPlayer(dying, kept, harm(4.0F, HarmKind::Acid)).isKilled);
    std::ignore = kept.check(kGivenBack, at(2.0), kRate);
    kept.drowned(nx::Damage{3.0F});
    kept.headOutOfWater();
    EXPECT_EQ(kept.check(kGivenBack, at(2.5), kRate), nx::HitPoints{}) << "looked at at 2 seconds";

    TimedHarms none;
    EXPECT_EQ(cs::harmPlayer(dying, none, harm(4.0F, HarmKind::Acid)).damage, nx::Damage{});
    EXPECT_EQ(none.check(kGivenBack, at(2.0), kRate), nx::HitPoints{}) << "nothing kept, nothing looked at";
    none.drowned(nx::Damage{3.0F});
    none.headOutOfWater();
    EXPECT_EQ(none.check(kGivenBack, at(2.5), kRate), nx::HitPoints{3.0F});
}

TEST(PlayerHarmTest, HealthIsGivenBackUpToWhatAPlayerSpawnsWith) {
    PlayerStatus status{.health = nx::HitPoints{50.0F}};
    cs::healPlayer(status, nx::HitPoints{10.0F});
    EXPECT_EQ(status.health, nx::HitPoints{60.0F});
    status.health = nx::HitPoints{95.0F};
    cs::healPlayer(status, nx::HitPoints{10.0F});
    EXPECT_EQ(status.health, cs::kSpawnHealth);
    PlayerStatus dead{.health = nx::HitPoints{}};
    cs::healPlayer(dead, nx::HitPoints{10.0F});
    EXPECT_EQ(dead.health, nx::HitPoints{}) << "the dead are given back none";
}

} // namespace
