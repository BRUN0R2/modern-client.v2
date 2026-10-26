// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// How a player is hurt and killed, as the original's CBasePlayer::TakeDamage and PostThink do it: the
// repository's data/cstrike/playerHarm.toml and what its reader refuses, the damage of a fall by its
// speed, the landings that deal it, and the whole points a player's health loses until it dies.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.collision;
import nx.game;
import nx.platform;
import cs.shared;

namespace {

using cs::FallDamage;
using cs::MovementState;
using cs::PlayerHarmRules;
using cs::PlayerStatus;
using nx::collision::Contents;

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
    const nx::game::Harm hurt = cs::harmPlayer(status, nx::Damage{20.8F});
    EXPECT_EQ(hurt.damage, nx::Damage{20.0F});
    EXPECT_FALSE(hurt.isKilled);
    EXPECT_EQ(status.health, nx::HitPoints{80.0F});
    EXPECT_TRUE(status.isAlive());

    EXPECT_EQ(cs::harmPlayer(status, nx::Damage{0.9F}).damage, nx::Damage{}) << "less than a point: none";
    EXPECT_EQ(status.health, nx::HitPoints{80.0F});

    const nx::game::Harm killed = cs::harmPlayer(status, nx::Damage{125.0F});
    EXPECT_EQ(killed.damage, nx::Damage{125.0F});
    EXPECT_TRUE(killed.isKilled);
    EXPECT_EQ(status.health, nx::HitPoints{-45.0F}) << "below none, as the original's health goes";
    EXPECT_FALSE(status.isAlive());

    const nx::game::Harm again = cs::harmPlayer(status, nx::Damage{10.0F});
    EXPECT_EQ(again.damage, nx::Damage{}) << "a dead player takes none";
    EXPECT_FALSE(again.isKilled);
    EXPECT_EQ(status.health, nx::HitPoints{-45.0F});
}

TEST(PlayerHarmTest, APlayerLeftWithExactlyNoneIsKilled) {
    PlayerStatus status{.health = nx::HitPoints{20.0F}};
    EXPECT_TRUE(cs::harmPlayer(status, nx::Damage{20.9F}).isKilled);
    EXPECT_EQ(status.health, nx::HitPoints{});
}

} // namespace
