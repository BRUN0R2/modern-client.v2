// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The game of a map as its server plays it: a player joining at its team's spawn point with its loadout
// and the starting money, moved by its commands tick by tick once the first freeze is over, the round
// ended in a draw by its death and its spawn again as the next starts, the game's restart, and what each
// tick's snapshot tells its client; on the BSP seed of the fuzz corpus given other entities, by the
// repository's data.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.console;
import nx.assets;
import cs.shared;
import cs.server;

#include "boxMap.hpp"
#include "consoleFixtures.hpp"
#include "temporaryDirectory.hpp"

namespace {

/// The box's counter-terrorist spawn points, low and far up, and a terrorist one.
constexpr std::string_view kEntities = R"({ "classname" "worldspawn" }
{ "classname" "info_player_start" "origin" "32 32 8" "angles" "0 90 0" }
{ "classname" "info_player_start" "origin" "16 16 2000" "angles" "0 180 0" }
{ "classname" "info_player_deathmatch" "origin" "48 48 8" "angles" "0 270 0" })";

constexpr float kTolerance = 1e-3F;
/// Ticks past the first freeze of a map: mp_freezetime's default and its extra two seconds, 800 ticks.
constexpr std::uint32_t kPastTheFirstFreeze = 810;
/// Ticks past the next round's start after one ends (kRestartDelay, 500 ticks).
constexpr std::uint32_t kPastTheRestartDelay = 501;
/// Ticks past a restart sv_restart asks for one second on, read once a second.
constexpr std::uint32_t kPastARestart = 250;

/// A game of the box, with the repository's data and an installation of no files.
class GameTest : public ::testing::Test {
protected:
    GameTest() {
        auto vfs = nx::assets::Vfs::create(std::array{m_installation.path()});
        EXPECT_TRUE(vfs.has_value());
        auto content = cs::readGameContent(NX_DATA_DIRECTORY, std::move(vfs).value());
        EXPECT_TRUE(content.has_value()) << (content ? "" : nx::describe(content.error()));
        m_content.emplace(std::move(content).value());
        auto map = cs::GameMap::read(nx::test::boxMapWith(kEntities), "box");
        EXPECT_TRUE(map.has_value()) << (map ? "" : nx::describe(map.error()));
        m_map = std::move(map).value_or(nullptr);
        auto cvars = cs::GameCvars::registerIn(m_console);
        EXPECT_TRUE(cvars.has_value());
        m_cvars.emplace(std::move(cvars).value());
        auto game = cs::Game::start(*m_content, *m_map, *m_cvars);
        EXPECT_TRUE(game.has_value()) << (game ? "" : nx::describe(game.error()));
        m_game = std::move(game).value_or(nullptr);
    }

    /// Plays `ticks` ticks, `player`'s command each looking along `view` and asking `move`; returns the
    /// snapshot of the last.
    auto play(cs::PlayerIndex player, std::uint32_t ticks, cs::MoveIntent move = {},
              nx::EulerAngles view = {}) -> cs::PlayerSnapshot {
        for (std::uint32_t tick = 0; tick < ticks; ++tick) {
            m_lastCommand = cs::CommandNumber{m_lastCommand.value + 1};
            const std::array commands{
                cs::PlayerCommand{.player = player,
                                  .command = cs::UserCommand{.number = m_lastCommand,
                                                             .duration = nx::Milliseconds{10.0F},
                                                             .viewAngles = view,
                                                             .move = move}}};
            m_game->tick(commands);
        }
        return m_game->snapshotFor(player);
    }

    [[nodiscard]] auto game() -> cs::Game& { return *m_game; }
    [[nodiscard]] auto content() const -> const cs::GameContent& { return *m_content; }
    [[nodiscard]] auto cvars() const -> const cs::GameCvars& { return *m_cvars; }

private:
    nx::test::TemporaryDirectory m_installation;
    nx::test::RecordingOutput m_output;
    nx::test::MemoryFiles m_files;
    nx::console::Console m_console{m_output, m_files};
    std::optional<cs::GameContent> m_content;
    std::unique_ptr<cs::GameMap> m_map;
    std::optional<cs::GameCvars> m_cvars;
    std::unique_ptr<cs::Game> m_game;
    cs::CommandNumber m_lastCommand{};
};

TEST_F(GameTest, APlayerJoinsAtItsTeamsSpawnPointWithItsLoadoutAndTheStartingMoney) {
    const cs::PlayerIndex player = game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier);
    const cs::PlayerSnapshot joined = game().snapshotFor(player);
    // One unit above the first counter-terrorist spawn point, turned the way it faces, placed there.
    EXPECT_EQ(joined.player.origin, (nx::Vec3{.x = 32.0F, .y = 32.0F, .z = 9.0F}));
    ASSERT_TRUE(joined.turnedView.has_value());
    EXPECT_EQ(joined.turnedView->yaw, nx::Degrees{90.0F});
    EXPECT_TRUE(joined.wasPlaced);
    EXPECT_EQ(joined.status.health, cs::kSpawnHealth);
    EXPECT_EQ(joined.status.money, cs::kStartMoney);
    // The counter-terrorists' loadout: the knife and the USP, which it draws, the heavier.
    EXPECT_EQ(joined.inventory.weapons().size(), 2U);
    const cs::OwnedWeapon* const held = joined.inventory.held();
    ASSERT_NE(held, nullptr);
    EXPECT_EQ(content().weapons.weapon(held->weapon).id, "usp");
    // The first round is frozen: its HUD counts the freeze down.
    EXPECT_TRUE(game().rounds().isFrozen());
}

TEST_F(GameTest, EachTickTellsTheClientWhatItMadeAndPlacedNothingAgain) {
    const cs::PlayerIndex player = game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier);
    const cs::PlayerSnapshot joined = game().snapshotFor(player);
    const cs::PlayerSnapshot first = play(player, 1);
    EXPECT_EQ(first.time.since(joined.time, first.rate), nx::tickLengthOf(first.rate));
    EXPECT_FALSE(first.turnedView.has_value()) << "turned as it joined, not again";
    EXPECT_FALSE(first.wasPlaced);
    // The knife, drawn first as it joined, is heard in the first tick.
    const auto deployed = [this](const cs::SoundMade& made) {
        return made.sound == &*content().weapons.weapon(*content().weapons.indexOf("knife")).view.drawSound;
    };
    EXPECT_TRUE(std::ranges::any_of(first.sounds, deployed));
    const cs::PlayerSnapshot second = play(player, 1);
    EXPECT_TRUE(second.sounds.empty()) << "a tick's sounds are its own";
}

TEST_F(GameTest, APlayerMovesByItsCommandsOnceTheFirstFreezeIsOver) {
    const cs::PlayerIndex player = game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier);
    const cs::PlayerSnapshot landed = play(player, kPastTheFirstFreeze);
    ASSERT_FALSE(game().rounds().isFrozen());
    EXPECT_TRUE(landed.player.isOnGround);
    EXPECT_EQ(landed.player.maxSpeed, landed.inventory.maxSpeed()) << "the USP's speed, not the freeze's";
    // Along the yaw of 90 degrees: +y.
    const cs::PlayerSnapshot walked =
        play(player, 10, {.forward = nx::UnitsPerSecond{250.0F}}, {.yaw = nx::Degrees{90.0F}});
    EXPECT_GT(walked.player.origin.y, landed.player.origin.y);
    EXPECT_NEAR(walked.player.origin.x, landed.player.origin.x, kTolerance);
}

TEST_F(GameTest, ThePlayersDeathEndsTheRoundInADrawAndItSpawnsAgainAsTheNextStarts) {
    const cs::PlayerIndex player = game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier);
    // Up where its second spawn point is: 2000 units above the floor.
    game().placeEye(player, {.x = 16.0F, .y = 16.0F, .z = 2000.0F});
    cs::PlayerSnapshot fallen = play(player, 1);
    for (std::uint32_t tick = 0; tick < 1000 && fallen.status.isAlive(); ++tick) {
        fallen = play(player, 1);
    }
    ASSERT_FALSE(fallen.status.isAlive()) << "killed by its landing";
    EXPECT_TRUE(fallen.player.isDead);
    EXPECT_TRUE(fallen.inventory.weapons().empty()) << "its weapons gone with it";
    EXPECT_EQ(game().rounds().end(), cs::RoundEndReason::RoundDraw);
    const auto heard = [&fallen](const cs::GameSound& sound) {
        return std::ranges::any_of(fallen.sounds,
                                   [&sound](const cs::SoundMade& made) { return made.sound == &sound; });
    };
    EXPECT_TRUE(heard(content().playerHarm.death));

    // The next round starts kRestartDelay after: it spawns whole at the next of its team's points.
    const cs::PlayerSnapshot respawned = play(player, kPastTheRestartDelay);
    EXPECT_TRUE(respawned.status.isAlive());
    EXPECT_EQ(respawned.status.health, cs::kSpawnHealth);
    EXPECT_FALSE(respawned.player.isDead);
    EXPECT_NEAR(respawned.player.origin.x, 16.0F, kTolerance) << "its team's second spawn point";
    EXPECT_FALSE(respawned.inventory.weapons().empty()) << "given its loadout again";
}

TEST_F(GameTest, SvRestartRestartsTheGameWithTheStartingMoney) {
    const cs::PlayerIndex player = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier);
    const std::optional<cs::WeaponIndex> ak47 = content().weapons.indexOf("ak47");
    ASSERT_TRUE(ak47.has_value());
    game().give(player, *ak47);
    ASSERT_TRUE(cvars().restart.set(1.0F).has_value());
    // Read once a second, the restart one second after: the player spawns as it joined.
    std::optional<cs::PlayerSnapshot> restarted;
    for (std::uint32_t tick = 0; tick < kPastARestart && !restarted; ++tick) {
        if (cs::PlayerSnapshot snapshot = play(player, 1); snapshot.wasPlaced) {
            restarted = std::move(snapshot);
        }
    }
    ASSERT_TRUE(restarted.has_value());
    EXPECT_EQ(cvars().restart.value(), 0.0F) << "set back to 0 as it is asked for";
    EXPECT_EQ(restarted->player.origin, (nx::Vec3{.x = 48.0F, .y = 48.0F, .z = 9.0F}))
        << "the terrorists' only spawn point";
    EXPECT_EQ(restarted->status.money, cs::kStartMoney);
    EXPECT_FALSE(std::ranges::contains(restarted->inventory.weapons(), *ak47, &cs::OwnedWeapon::weapon))
        << "with its team's loadout alone";
    EXPECT_TRUE(game().rounds().isFrozen()) << "a restarted game starts frozen";
}

TEST_F(GameTest, TheConsoleGivesWeaponsAndNoclipAndAToolPlacesTheEye) {
    const cs::PlayerIndex player = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier);
    const std::optional<cs::WeaponIndex> ak47 = content().weapons.indexOf("ak47");
    ASSERT_TRUE(ak47.has_value());
    game().give(player, *ak47);
    const cs::PlayerSnapshot given = game().snapshotFor(player);
    EXPECT_TRUE(std::ranges::contains(given.inventory.weapons(), *ak47, &cs::OwnedWeapon::weapon));
    EXPECT_EQ(content().weapons.weapon(given.inventory.held()->weapon).id, "ak47") << "drawn, the heavier";
    EXPECT_TRUE(game().toggleNoclip(player));
    EXPECT_EQ(game().snapshotFor(player).player.moveType, cs::MoveType::Noclip);
    EXPECT_FALSE(game().toggleNoclip(player));
    game().placeEye(player, {.x = 10.0F, .y = 20.0F, .z = 100.0F});
    const cs::PlayerSnapshot placed = game().snapshotFor(player);
    EXPECT_TRUE(placed.wasPlaced);
    EXPECT_EQ(placed.player.origin + nx::Vec3{.z = placed.player.viewHeight.value()},
              (nx::Vec3{.x = 10.0F, .y = 20.0F, .z = 100.0F}));
}

} // namespace
