// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// A map's rounds, as the original plays them: what its entities say it is played for, how a round
// whose time runs out ends by it and who wins, the lengths its cvars set, each round frozen as it
// starts (the first longer), its time run out, the next five seconds after its end with the lengths
// as they are then, sv_restart, and the time a player's HUD counts down.

#include <gtest/gtest.h>

import std;
import nx.core;
import cs.shared;

namespace {

using cs::MapObjective;
using cs::MapObjectives;
using cs::RoundEndReason;
using cs::RoundEvent;
using cs::RoundEvents;
using cs::RoundLengths;
using cs::Rounds;
using cs::Team;

constexpr nx::TickRate kRate = nx::kDefaultTickRate;
constexpr RoundLengths kLengths{.freeze = nx::Seconds{6.0F}, .round = nx::Seconds{300.0F}};
constexpr MapObjectives kBombMap{MapObjective::BombTarget};

/// The moment `seconds` after the map started, a whole number of ticks.
[[nodiscard]] auto at(double seconds) -> nx::GameTime {
    return nx::GameTime::atTick(std::llround(seconds * kRate.value()));
}

[[nodiscard]] auto events(std::initializer_list<RoundEvent> list) -> RoundEvents {
    return RoundEvents{list};
}

TEST(RoundsTest, AMapIsPlayedForWhatTheClassesOfItsEntitiesSay) {
    EXPECT_EQ(cs::objectiveOfClass("func_bomb_target"), MapObjective::BombTarget);
    EXPECT_EQ(cs::objectiveOfClass("info_bomb_target"), MapObjective::BombTarget);
    EXPECT_EQ(cs::objectiveOfClass("hostage_entity"), MapObjective::Hostages);
    EXPECT_EQ(cs::objectiveOfClass("func_escapezone"), MapObjective::EscapeZone);
    EXPECT_EQ(cs::objectiveOfClass("func_vip_safetyzone"), MapObjective::VipSafetyZone);
    EXPECT_EQ(cs::objectiveOfClass("func_buyzone"), std::nullopt);
    EXPECT_EQ(cs::objectiveOfClass("info_player_start"), std::nullopt);
}

TEST(RoundsTest, ATimeRunOutEndsTheRoundByTheFirstObjectiveOfItsMap) {
    EXPECT_EQ(cs::timeOutReasonOf({}), std::nullopt);
    EXPECT_EQ(cs::timeOutReasonOf({MapObjective::Hostages, MapObjective::BombTarget}),
              RoundEndReason::TargetSaved);
    EXPECT_EQ(cs::timeOutReasonOf({MapObjective::VipSafetyZone, MapObjective::Hostages}),
              RoundEndReason::HostagesNotRescued);
    EXPECT_EQ(cs::timeOutReasonOf({MapObjective::VipSafetyZone, MapObjective::EscapeZone}),
              RoundEndReason::TerroristsNotEscaped);
    EXPECT_EQ(cs::timeOutReasonOf({MapObjective::VipSafetyZone}), RoundEndReason::VipNotEscaped);
}

TEST(RoundsTest, ATimeRunOutIsWonByTheTeamThatHeldTheObjective) {
    EXPECT_EQ(cs::winnerOf(RoundEndReason::TargetSaved), Team::CounterTerrorists);
    EXPECT_EQ(cs::winnerOf(RoundEndReason::HostagesNotRescued), Team::Terrorists);
    EXPECT_EQ(cs::winnerOf(RoundEndReason::TerroristsNotEscaped), Team::CounterTerrorists);
    EXPECT_EQ(cs::winnerOf(RoundEndReason::VipNotEscaped), Team::Terrorists);
}

TEST(RoundsTest, TheCvarsLengthsAreCutToWholeSeconds) {
    const RoundLengths defaults = cs::roundLengthsOf(6.0F, 5.0F);
    EXPECT_EQ(defaults.freeze, nx::Seconds{6.0F});
    EXPECT_EQ(defaults.round, nx::Seconds{300.0F});
    const RoundLengths cut = cs::roundLengthsOf(6.9F, 2.501F);
    EXPECT_EQ(cut.freeze, nx::Seconds{6.0F});
    EXPECT_EQ(cut.round, nx::Seconds{150.0F});
}

TEST(RoundsTest, TheFirstFreezeOfAMapIsTwoSecondsLongerAndRunsOutIntoTheRoundsTime) {
    Rounds rounds = Rounds::start(kBombMap, kLengths, at(0.0));
    EXPECT_TRUE(rounds.isFrozen());
    EXPECT_EQ(rounds.remainingAt(at(0.0), kRate), nx::Seconds{8.0F});
    EXPECT_EQ(rounds.think(at(7.99), kRate, kLengths), RoundEvents{});
    EXPECT_TRUE(rounds.isFrozen());
    EXPECT_EQ(rounds.think(at(8.0), kRate, kLengths), events({RoundEvent::FreezeEnded}));
    EXPECT_FALSE(rounds.isFrozen());
    EXPECT_EQ(rounds.remainingAt(at(8.0), kRate), nx::Seconds{300.0F});
    EXPECT_EQ(rounds.end(), std::nullopt);
    EXPECT_EQ(rounds.restartAt(), std::nullopt);
}

TEST(RoundsTest, ARoundWhoseTimeRunsOutEndsAndTheNextStartsFrozenFiveSecondsLater) {
    Rounds rounds = Rounds::start(kBombMap, kLengths, at(0.0));
    std::ignore = rounds.think(at(8.0), kRate, kLengths);
    EXPECT_EQ(rounds.think(at(307.99), kRate, kLengths), RoundEvents{});
    EXPECT_EQ(rounds.think(at(308.0), kRate, kLengths), events({RoundEvent::Ended}));
    EXPECT_EQ(rounds.end(), RoundEndReason::TargetSaved);
    EXPECT_EQ(rounds.restartAt(), at(313.0));
    EXPECT_FALSE(rounds.isFrozen());
    EXPECT_EQ(rounds.think(at(312.99), kRate, kLengths), RoundEvents{});
    EXPECT_EQ(rounds.think(at(313.0), kRate, kLengths), events({RoundEvent::RoundStarted}));
    EXPECT_TRUE(rounds.isFrozen());
    EXPECT_EQ(rounds.end(), std::nullopt);
    EXPECT_EQ(rounds.restartAt(), std::nullopt);
    EXPECT_EQ(rounds.remainingAt(at(313.0), kRate), nx::Seconds{6.0F});
}

TEST(RoundsTest, TheNextRoundIsAsLongAsTheLengthsSayAsItStarts) {
    Rounds rounds = Rounds::start(kBombMap, kLengths, at(0.0));
    std::ignore = rounds.think(at(8.0), kRate, kLengths);
    std::ignore = rounds.think(at(308.0), kRate, kLengths);
    const RoundLengths shorter{.freeze = nx::Seconds{3.0F}, .round = nx::Seconds{60.0F}};
    std::ignore = rounds.think(at(313.0), kRate, shorter);
    EXPECT_EQ(rounds.remainingAt(at(313.0), kRate), nx::Seconds{3.0F});
    // Lengths changed later wait for the round after.
    EXPECT_EQ(rounds.think(at(316.0), kRate, kLengths), events({RoundEvent::FreezeEnded}));
    EXPECT_EQ(rounds.remainingAt(at(316.0), kRate), nx::Seconds{60.0F});
}

TEST(RoundsTest, TheRoundsOfAMapPlayedForNothingRunOnPastTheirTime) {
    Rounds rounds = Rounds::start({}, kLengths, at(0.0));
    std::ignore = rounds.think(at(8.0), kRate, kLengths);
    EXPECT_EQ(rounds.think(at(400.0), kRate, kLengths), RoundEvents{});
    EXPECT_EQ(rounds.end(), std::nullopt);
    EXPECT_LT(rounds.remainingAt(at(400.0), kRate), nx::Seconds{});
}

TEST(RoundsTest, ARestartedGameStartsItsNextRoundWhenAskedTheRoundPlayingOnUntilThen) {
    Rounds rounds = Rounds::start(kBombMap, kLengths, at(0.0));
    std::ignore = rounds.think(at(8.0), kRate, kLengths);
    rounds.restartGameIn(nx::Seconds{3.0F}, at(20.0), kRate);
    EXPECT_EQ(rounds.restartAt(), at(23.0));
    EXPECT_EQ(rounds.think(at(22.99), kRate, kLengths), RoundEvents{});
    EXPECT_EQ(rounds.end(), std::nullopt);
    EXPECT_EQ(rounds.think(at(23.0), kRate, kLengths), events({RoundEvent::GameRestarted}));
    EXPECT_TRUE(rounds.isFrozen());
    EXPECT_EQ(rounds.remainingAt(at(23.0), kRate), nx::Seconds{6.0F});
    // The game restarted once: the round after starts as rounds do.
    std::ignore = rounds.think(at(29.0), kRate, kLengths);
    std::ignore = rounds.think(at(329.0), kRate, kLengths);
    EXPECT_EQ(rounds.think(at(334.0), kRate, kLengths), events({RoundEvent::RoundStarted}));
}

TEST(RoundsTest, ARoundThatEndsBeforeTheRestartRestartsTheGameFiveSecondsAfterItsEnd) {
    Rounds rounds = Rounds::start(kBombMap, kLengths, at(0.0));
    std::ignore = rounds.think(at(8.0), kRate, kLengths);
    rounds.restartGameIn(nx::Seconds{60.0F}, at(300.0), kRate);
    EXPECT_EQ(rounds.think(at(308.0), kRate, kLengths), events({RoundEvent::Ended}));
    EXPECT_EQ(rounds.restartAt(), at(313.0));
    EXPECT_EQ(rounds.think(at(313.0), kRate, kLengths), events({RoundEvent::GameRestarted}));
}

TEST(RoundsTest, AFreezeThatRunsOutAsTheNextRoundIsDueEndsBeforeItStarts) {
    Rounds rounds = Rounds::start(kBombMap, kLengths, at(0.0));
    rounds.restartGameIn(nx::Seconds{8.0F}, at(0.0), kRate);
    EXPECT_EQ(rounds.think(at(8.0), kRate, kLengths),
              events({RoundEvent::FreezeEnded, RoundEvent::GameRestarted}));
    EXPECT_TRUE(rounds.isFrozen());
}

TEST(RoundsTest, TheHudCountsTheWholeSecondsItWasToldDown) {
    const Rounds rounds = Rounds::start(kBombMap, kLengths, at(0.0));
    const cs::RoundTimer timer = rounds.timerAt(at(0.0), kRate);
    EXPECT_EQ(timer.seconds, 8U);
    EXPECT_EQ(timer.toldAt, at(0.0));
    EXPECT_EQ(timer.shownAt(at(0.0), kRate), 8U);
    EXPECT_EQ(timer.shownAt(at(0.01), kRate), 7U);
    EXPECT_EQ(timer.shownAt(at(1.0), kRate), 7U);
    EXPECT_EQ(timer.shownAt(at(1.01), kRate), 6U);
    EXPECT_EQ(timer.shownAt(at(7.5), kRate), 0U);
    EXPECT_EQ(timer.shownAt(at(60.0), kRate), 0U);
    // Told part of a second into one, it cuts it: the original's message carries whole seconds.
    EXPECT_EQ(rounds.timerAt(at(0.5), kRate).seconds, 7U);
}

} // namespace
