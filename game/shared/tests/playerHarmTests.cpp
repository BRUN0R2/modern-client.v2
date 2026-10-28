// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// How a player is hurt and killed, as the original's CBasePlayer::TraceAttack, TakeDamage, Pain,
// TakeHealth, PostThink and CheckTimeBasedDamage do it: the repository's data/cstrike/playerHarm.toml and
// what its reader refuses, the damage of a fall by its speed, the landings that deal it, the share of a
// harm armor takes where it covers the player, the whole points a player's health loses until it dies,
// the pain it is heard in and the punch of its view by where it was met, the harms it keeps over time,
// and the drowned health given back.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.collision;
import nx.game;
import nx.platform;
import cs.shared;

namespace {

using cs::ArmorKind;
using cs::ArmorShares;
using cs::FallDamage;
using cs::HarmKind;
using cs::HitGroup;
using cs::MovementState;
using cs::PlayerHarm;
using cs::PlayerHarmRules;
using cs::PlayerStatus;
using cs::TimedHarms;
using nx::collision::Contents;

constexpr nx::TickRate kRate = nx::kDefaultTickRate;
constexpr nx::HitPoints kGivenBack{10.0F};
/// The armor's shares of a harm of the world's: ARMOR_RATIO and ARMOR_BONUS.
constexpr ArmorShares kArmor{};
constexpr HitGroup kGeneric = HitGroup::Generic;

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
    EXPECT_EQ(rules.headPain.files, (std::vector<std::string>{"player/headshot1.wav", "player/headshot2.wav",
                                                              "player/headshot3.wav"}));
    EXPECT_EQ(rules.helmetPain.files, (std::vector<std::string>{"player/bhit_helmet-1.wav"}));
    EXPECT_EQ(rules.kevlarPain.files, (std::vector<std::string>{"player/bhit_kevlar-1.wav"}));
    for (const cs::GameSound* const pain : {&rules.headPain, &rules.helmetPain, &rules.kevlarPain}) {
        EXPECT_EQ(pain->channel, cs::SoundChannel::Voice);
    }
    // ARMOR_RATIO, ARMOR_BONUS; TakeDamage's 0.35 of a teammate's attack, TakeDamageImpulse's.
    EXPECT_EQ(rules.armor.ratio, 0.5F);
    EXPECT_EQ(rules.armor.bonus, 0.5F);
    EXPECT_EQ(rules.attacks.teammateShare, cs::DamageShare{0.35});
    EXPECT_EQ(rules.attacks.knockback, nx::UnitsPerSecond{170.0F});
    EXPECT_EQ(rules.attacks.knockbackBelowSpeed, nx::UnitsPerSecond{300.0F});
    EXPECT_EQ(rules.attacks.flinchSpeedShare, 0.5F);
    EXPECT_EQ(rules.attacks.largeFlinchSpeedShare, 0.65F);
    EXPECT_EQ(rules.splat.files, (std::vector<std::string>{"common/bodysplat.wav"}));
    EXPECT_EQ(rules.splat.channel, cs::SoundChannel::Item);
    EXPECT_EQ(rules.death.files, (std::vector<std::string>{"player/die1.wav", "player/die2.wav",
                                                           "player/die3.wav", "player/death6.wav"}));
    EXPECT_EQ(rules.death.channel, cs::SoundChannel::Voice);
    EXPECT_EQ(rules.soundFiles().size(), 13U);
}

/// A file of the harm: `painFiles` for its pain, and a fall from `safeSpeed` to `fatalSpeed`.
[[nodiscard]] auto harmFile(std::string_view painFiles, float safeSpeed, float fatalSpeed) -> std::string {
    return std::format(R"(pain = {{ files = [{}], channel = "voice", volume = 1.0 }}
headPain = {{ files = ["d.wav"], channel = "voice", volume = 1.0 }}
helmetPain = {{ files = ["e.wav"], channel = "voice", volume = 1.0 }}
kevlarPain = {{ files = ["f.wav"], channel = "voice", volume = 1.0 }}
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

[armor]
ratio = 0.5
bonus = 0.5

[attacks]
teammateShare = 0.35
knockback = 170.0
knockbackBelowSpeed = 300.0
flinchSpeedShare = 0.5
largeFlinchSpeedShare = 0.65
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
    const cs::PlayerHarmed hurt = cs::harmPlayer(status, timed, harm(20.8F), kGeneric, kArmor);
    EXPECT_EQ(hurt.damage, nx::Damage{20.0F});
    EXPECT_FALSE(hurt.isKilled);
    EXPECT_EQ(status.health, nx::HitPoints{80.0F});
    EXPECT_TRUE(status.isAlive());

    EXPECT_EQ(cs::harmPlayer(status, timed, harm(0.9F), kGeneric, kArmor).damage, nx::Damage{})
        << "less than a point: none";
    EXPECT_EQ(status.health, nx::HitPoints{80.0F});

    const cs::PlayerHarmed killed = cs::harmPlayer(status, timed, harm(125.0F), kGeneric, kArmor);
    EXPECT_EQ(killed.damage, nx::Damage{125.0F});
    EXPECT_TRUE(killed.isKilled);
    EXPECT_EQ(status.health, nx::HitPoints{-45.0F}) << "below none, as the original's health goes";
    EXPECT_FALSE(status.isAlive());

    const cs::PlayerHarmed again = cs::harmPlayer(status, timed, harm(10.0F), kGeneric, kArmor);
    EXPECT_EQ(again.damage, nx::Damage{}) << "a dead player takes none";
    EXPECT_FALSE(again.isKilled);
    EXPECT_EQ(status.health, nx::HitPoints{-45.0F});
}

TEST(PlayerHarmTest, APlayerLeftWithExactlyNoneIsKilled) {
    PlayerStatus status{.health = nx::HitPoints{20.0F}};
    TimedHarms timed;
    EXPECT_TRUE(cs::harmPlayer(status, timed, harm(20.9F), kGeneric, kArmor).isKilled);
    EXPECT_EQ(status.health, nx::HitPoints{});
}

TEST(PlayerHarmTest, ArmorTakesItsShareWhereItCoversThePlayer) {
    const HitGroup chest = HitGroup::Chest;
    const HitGroup head = HitGroup::Head;
    const HitGroup leg = HitGroup::LeftLeg;
    // An AK-47's 36 in the chest of kevlar: the health takes 0.5 times 1.55 of it, the armor half the rest.
    PlayerStatus kevlar{.armor = nx::ArmorPoints{100.0F}, .armorKind = ArmorKind::Kevlar};
    TimedHarms timed;
    const ArmorShares akRatio{.ratio = static_cast<float>(0.5 * 1.55)};
    const cs::PlayerHarmed shot =
        cs::harmPlayer(kevlar, timed, harm(36.0F, HarmKind::Bullet), chest, akRatio);
    EXPECT_TRUE(shot.isArmored);
    EXPECT_EQ(shot.damage, nx::Damage{27.0F}) << "27.9, its whole points";
    EXPECT_EQ(kevlar.health, nx::HitPoints{73.0F});
    EXPECT_FLOAT_EQ(kevlar.armor.value(), 100.0F - ((36.0F - (akRatio.ratio * 36.0F)) * 0.5F));

    // In the head, only a helmet takes a share; in the legs, nothing does.
    PlayerStatus vest{.armor = nx::ArmorPoints{100.0F}, .armorKind = ArmorKind::Kevlar};
    EXPECT_FALSE(cs::harmPlayer(vest, timed, harm(60.0F, HarmKind::Bullet), head, kArmor).isArmored);
    EXPECT_EQ(vest.health, nx::HitPoints{40.0F});
    PlayerStatus helmet{.armor = nx::ArmorPoints{100.0F}, .armorKind = ArmorKind::KevlarAndHelmet};
    EXPECT_TRUE(cs::harmPlayer(helmet, timed, harm(60.0F, HarmKind::Bullet), head, kArmor).isArmored);
    EXPECT_EQ(helmet.health, nx::HitPoints{70.0F});
    EXPECT_EQ(helmet.armor, nx::ArmorPoints{85.0F});
    EXPECT_FALSE(cs::harmPlayer(helmet, timed, harm(20.0F, HarmKind::Bullet), leg, kArmor).isArmored);
    EXPECT_EQ(helmet.health, nx::HitPoints{50.0F});

    // Nor a fall nor drowning, wherever the player was last met; lava and slime it does.
    PlayerStatus world{.armor = nx::ArmorPoints{100.0F}, .armorKind = ArmorKind::Kevlar};
    EXPECT_FALSE(cs::harmPlayer(world, timed, harm(10.0F, HarmKind::Fall), chest, kArmor).isArmored);
    EXPECT_FALSE(cs::harmPlayer(world, timed, harm(10.0F, HarmKind::Drown), chest, kArmor).isArmored);
    EXPECT_TRUE(cs::harmPlayer(world, timed, harm(10.0F, HarmKind::Burn), chest, kArmor).isArmored);
    EXPECT_EQ(world.health, nx::HitPoints{75.0F});
}

TEST(PlayerHarmTest, ArmorWornOutGivesWhatItsPointsTookAndIsNoneThen) {
    // 40 would wear 10 of its 2 points: they take 4 of it, the health the other 36.
    PlayerStatus worn{.armor = nx::ArmorPoints{2.0F}, .armorKind = ArmorKind::KevlarAndHelmet};
    TimedHarms timed;
    const cs::PlayerHarmed shot =
        cs::harmPlayer(worn, timed, harm(40.0F, HarmKind::Bullet), HitGroup::Stomach, kArmor);
    EXPECT_TRUE(shot.isArmored);
    EXPECT_EQ(shot.damage, nx::Damage{36.0F});
    EXPECT_EQ(worn.armor, nx::ArmorPoints{});
    EXPECT_EQ(worn.armorKind, ArmorKind::None);
}

TEST(PlayerHarmTest, APlayerIsHeardInPainByWhereItWasMetAndItsArmor) {
    const PlayerHarmRules rules = repositoryRules();
    const PlayerStatus bare{};
    const PlayerStatus helmet{.armor = nx::ArmorPoints{50.0F}, .armorKind = ArmorKind::KevlarAndHelmet};
    EXPECT_EQ(&cs::painOf(rules, bare, HitGroup::Head, false), &rules.headPain);
    EXPECT_EQ(&cs::painOf(rules, helmet, HitGroup::Head, true), &rules.helmetPain);
    EXPECT_EQ(&cs::painOf(rules, helmet, HitGroup::Chest, true), &rules.kevlarPain);
    EXPECT_EQ(&cs::painOf(rules, helmet, HitGroup::Generic, true), &rules.kevlarPain);
    EXPECT_EQ(&cs::painOf(rules, helmet, HitGroup::RightLeg, true), &rules.pain);
    EXPECT_EQ(&cs::painOf(rules, bare, HitGroup::Chest, false), &rules.pain);
}

TEST(PlayerHarmTest, AHitPunchesTheViewOfAPlayerItBleeds) {
    nx::Random random{1, "punch"};
    nx::EulerAngles head{};
    cs::punchByHit(head, HitGroup::Head, nx::Damage{4.0F}, ArmorKind::Kevlar, random);
    EXPECT_FLOAT_EQ(head.pitch.value(), -2.0F) << "half of it up";
    EXPECT_LE(std::abs(head.roll.value()), 4.0F) << "as much as it either way";
    cs::punchByHit(head, HitGroup::Head, nx::Damage{400.0F}, ArmorKind::None, random);
    EXPECT_FLOAT_EQ(head.pitch.value(), -12.0F);
    EXPECT_FLOAT_EQ(std::abs(head.roll.value()), 9.0F) << "at most";

    nx::EulerAngles chest{.yaw = nx::Degrees{1.0F}};
    cs::punchByHit(chest, HitGroup::Chest, nx::Damage{30.0F}, ArmorKind::None, random);
    EXPECT_FLOAT_EQ(chest.pitch.value(), -3.0F);
    cs::punchByHit(chest, HitGroup::Stomach, nx::Damage{125.0F}, ArmorKind::None, random);
    EXPECT_FLOAT_EQ(chest.pitch.value(), -4.0F);
    EXPECT_FLOAT_EQ(chest.yaw.value(), 1.0F) << "the rest as it was";

    // Armor that stops the blood, and the arms and legs, leave it as it was.
    for (const auto& [hit, kind] :
         {std::pair{HitGroup::Head, ArmorKind::KevlarAndHelmet},
          std::pair{HitGroup::Chest, ArmorKind::Kevlar}, std::pair{HitGroup::LeftArm, ArmorKind::None},
          std::pair{HitGroup::RightLeg, ArmorKind::None}}) {
        nx::EulerAngles punch{.pitch = nx::Degrees{-1.0F}};
        cs::punchByHit(punch, hit, nx::Damage{50.0F}, kind, random);
        EXPECT_EQ(punch, (nx::EulerAngles{.pitch = nx::Degrees{-1.0F}})) << nx::enumName(hit);
    }
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
    EXPECT_TRUE(cs::harmPlayer(dying, kept, harm(4.0F, HarmKind::Acid), kGeneric, kArmor).isKilled);
    std::ignore = kept.check(kGivenBack, at(2.0), kRate);
    kept.drowned(nx::Damage{3.0F});
    kept.headOutOfWater();
    EXPECT_EQ(kept.check(kGivenBack, at(2.5), kRate), nx::HitPoints{}) << "looked at at 2 seconds";

    TimedHarms none;
    EXPECT_EQ(cs::harmPlayer(dying, none, harm(4.0F, HarmKind::Acid), kGeneric, kArmor).damage, nx::Damage{});
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
