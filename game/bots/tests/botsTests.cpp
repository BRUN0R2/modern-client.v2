// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.console;
import nx.assets;
import nx.jobs;
import nx.game;
import nx.agents;
import cs.shared;
import cs.server;
import cs.bots;

#include "boxMap.hpp"
#include "consoleFixtures.hpp"
#include "playerModel.hpp"
#include "temporaryDirectory.hpp"

namespace {

/// A counter-terrorist spawn point facing +x and a terrorist one, on the stretched box's floor.
constexpr std::string_view kEntities = R"({ "classname" "worldspawn" }
{ "classname" "info_player_start" "origin" "1000 1000 8" "angles" "0 0 0" }
{ "classname" "info_player_deathmatch" "origin" "1000 1600 8" "angles" "0 0 0" })";
/// How far the stretched box goes along x and y.
constexpr float kMapSide = 2048.0F;
/// Where the tests stand their players: along y kRowY, their eyes a unit above where they stand.
constexpr float kRowY = 1000.0F;
constexpr float kStandingEye = cs::kStandingViewHeight.value() + 1.0F;
/// How far from a bot the players of the tests stand, ahead of it or behind.
constexpr float kApart = 300.0F;
/// How far above the floor players stand that do not fall, as no command moves them.
constexpr float kAboveTheFloor = 100.0F;
/// Ticks of bot_quota's interval, 0.3 seconds, and one more.
constexpr std::uint32_t kPastAQuotaCheck = 31;
/// Ticks past the first freeze of a map: mp_freezetime's default and its extra two seconds, 800 ticks.
constexpr std::uint32_t kPastTheFirstFreeze = 810;
/// Ticks past the restart Game Commencing asks for (kCommenceDelay, 300 ticks).
constexpr std::uint32_t kPastTheCommence = 301;
/// Ticks past a round's freeze, mp_freezetime's default (600 ticks).
constexpr std::uint32_t kPastAFreeze = 610;
/// Ticks a bot is given to kill an enemy it sees once the freeze is over.
constexpr std::uint32_t kKillingTicks = 1000;
/// Above the floor: a fall from there kills.
constexpr float kDeadlyHeight = 2000.0F;

/// A game of the stretched box with the repository's data, an installation of the characters' models
/// alone, and its bots on the box's mesh.
class BotsTest : public ::testing::Test {
protected:
    BotsTest() {
        const std::filesystem::path data{NX_DATA_DIRECTORY};
        auto characters = cs::readCharacters(
            nx::platform::readTextFile(data / "cstrike" / "characters.toml").value_or(std::string{}),
            "characters.toml");
        EXPECT_TRUE(characters.has_value());
        const std::string model =
            nx::test::playerModelOf(nx::test::playerSequences()) |
            std::views::transform([](std::byte value) { return static_cast<char>(value); }) |
            std::ranges::to<std::string>();
        for (const cs::Character& character : characters.value_or(cs::Characters{}).characters) {
            installation.write(cs::modelOf(character), model);
        }
        auto vfs = nx::assets::Vfs::create(std::array{installation.path()});
        EXPECT_TRUE(vfs.has_value());
        auto read = cs::readGameContent(data, std::move(vfs).value());
        EXPECT_TRUE(read.has_value()) << (read ? "" : nx::describe(read.error()));
        content.emplace(std::move(read).value());
        auto readMap = cs::GameMap::read(nx::test::stretchedBoxMapWith(kEntities, kMapSide, kMapSide), "box");
        EXPECT_TRUE(readMap.has_value()) << (readMap ? "" : nx::describe(readMap.error()));
        map = std::move(readMap).value_or(nullptr);
        auto registered = cs::GameCvars::registerIn(console);
        EXPECT_TRUE(registered.has_value());
        cvars.emplace(std::move(registered).value());
        auto started = cs::Game::start(*content, *map, *cvars);
        EXPECT_TRUE(started.has_value());
        game = std::move(started).value_or(nullptr);
        auto created = nx::jobs::JobSystem::create({.workerCount = 0});
        EXPECT_TRUE(created.has_value());
        jobs = std::move(created).value_or(nullptr);
        auto navigation = nx::game::readNavigationData(
            nx::platform::readTextFile(data / "cstrike" / "navigation.toml").value_or(std::string{}),
            "navigation.toml");
        EXPECT_TRUE(navigation.has_value());
        auto rules = cs::readBotRules(
            nx::platform::readTextFile(data / "cstrike" / "bots.toml").value_or(std::string{}), "bots.toml");
        EXPECT_TRUE(rules.has_value());
        auto made = cs::Bots::create(*jobs, *game, navigation.value(), std::move(rules).value());
        EXPECT_TRUE(made.has_value()) << (made ? "" : nx::describe(made.error()));
        bots = std::move(made).value_or(nullptr);
    }

    /// Plays `ticks` ticks of the bots' commands alone.
    void play(std::uint32_t ticks) {
        for (std::uint32_t tick = 0; tick < ticks; ++tick) {
            game->tick(bots->commands());
        }
    }

    /// Stands `player` on the floor at `x` along y kRowY, a unit above it.
    void standAt(cs::PlayerIndex player, float x) {
        game->placeEye(player, {.x = x, .y = kRowY, .z = kStandingEye});
    }

    nx::test::TemporaryDirectory installation;
    nx::test::RecordingOutput output;
    nx::test::MemoryFiles files;
    nx::console::Console console{output, files};
    std::optional<cs::GameContent> content;
    std::optional<cs::GameCvars> cvars;
    std::unique_ptr<cs::GameMap> map;
    std::unique_ptr<cs::Game> game;
    std::unique_ptr<nx::jobs::JobSystem> jobs;
    std::unique_ptr<cs::Bots> bots; ///< Before the game it plays in goes.
};

TEST_F(BotsTest, ABotJoinsTheTeamOfFewerPlayersAndLeavesItsSlotWhenKicked) {
    const cs::PlayerIndex human = game->join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier);
    const cs::PlayerIndex bot = bots->add(std::nullopt);
    EXPECT_EQ(game->ownStateOf(bot).team, cs::Team::CounterTerrorists) << "SelectDefaultTeam";
    EXPECT_TRUE(game->ownStateOf(bot).isAlive);
    EXPECT_EQ(game->playerCount(), 2U);
    bots->kickAll();
    EXPECT_EQ(bots->size(), 0U);
    EXPECT_EQ(game->playerCount(), 1U);
    EXPECT_EQ(game->teamCounts()[cs::Team::CounterTerrorists].players(), 0U);
    // The slot it left is the next player's.
    EXPECT_EQ(game->join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier), bot);
    EXPECT_NE(bot, human);
}

TEST_F(BotsTest, ABotsBodyIsAnAgentWhoseTypeAnotherKindMayRegisterToo) {
    const cs::PlayerIndex bot = bots->add(cs::Team::Terrorists);
    const nx::agents::Agent* const agent = game->playerComponent<nx::agents::Agent>(bot);
    ASSERT_NE(agent, nullptr);
    EXPECT_TRUE(agent->brain.memories().empty());
    EXPECT_NE(game->playerComponent<cs::BotDriver>(bot), nullptr);
    EXPECT_TRUE(game->registerEntityComponent<nx::agents::Agent>("nx::agents::Agent").has_value())
        << "as the test zombies register it in the same game";
}

TEST_F(BotsTest, TheQuotaAddsOneBotEachIntervalAndKicksADeadOneFirst) {
    // Three counter-terrorists: each bot joins the terrorists, the team of fewer players.
    constexpr std::uint32_t kHumans = 3;
    for (std::uint32_t human = 0; human < kHumans; ++human) {
        std::ignore = game->join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier);
    }
    bots->keepQuota(2);
    EXPECT_EQ(bots->size(), 1U);
    bots->keepQuota(2);
    EXPECT_EQ(bots->size(), 1U) << "once in its interval";
    // Its joining commenced the game: past the restart, the second joins the round played.
    play(kPastTheCommence);
    bots->keepQuota(2);
    ASSERT_EQ(bots->size(), 2U);
    const std::vector<cs::PlayerIndex> joined = bots->players() | std::ranges::to<std::vector>();
    // Kill the second by a fall; then the counter-terrorists are the larger team, which has no bot.
    ASSERT_EQ(game->ownStateOf(joined[0]).team, cs::Team::Terrorists);
    ASSERT_EQ(game->ownStateOf(joined[1]).team, cs::Team::Terrorists);
    game->placeEye(joined[1], {.x = 1500.0F, .y = kRowY, .z = kDeadlyHeight});
    for (std::uint32_t tick = 0; tick < kPastTheFirstFreeze && game->ownStateOf(joined[1]).isAlive; ++tick) {
        play(1);
    }
    ASSERT_FALSE(game->ownStateOf(joined[1]).isAlive) << "killed by its landing";
    play(kPastAQuotaCheck);
    bots->keepQuota(1);
    EXPECT_TRUE(std::ranges::equal(bots->players(), std::array{joined[0]})) << "the dead one first";
}

TEST_F(BotsTest, ABotFiresEveryOtherCommandAtTheChestOfAnEnemyInItsCone) {
    const cs::PlayerIndex bot = bots->add(cs::Team::CounterTerrorists);
    const cs::PlayerIndex enemy = game->join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier);
    // The bot faces +x as its spawn point does; both stand above the box's floor for lines, which is
    // level with a standing player's origin, so that the bot sees the enemy's chest.
    game->placeEye(bot, {.y = kRowY, .z = kStandingEye + kAboveTheFloor});
    game->placeEye(enemy, {.x = kApart, .y = kRowY, .z = kStandingEye + kAboveTheFloor});
    const std::vector<cs::PlayerCommand> first = bots->commands();
    ASSERT_EQ(first.size(), 1U);
    EXPECT_TRUE(first[0].command.buttons.test(cs::Button::Attack));
    EXPECT_EQ(first[0].command.move.forward, nx::UnitsPerSecond{}) << "it stands to fire";
    const nx::Vec3 chest = game->ownStateOf(enemy).eye - nx::Vec3{.z = cs::kStandingViewHeight.value()};
    const nx::EulerAngles facing = nx::vectorAngles(chest - game->ownStateOf(bot).eye);
    EXPECT_NEAR(first[0].command.viewAngles.yaw.value(), facing.yaw.value(), 1e-3F);
    EXPECT_NEAR(first[0].command.viewAngles.pitch.value(), facing.pitch.value(), 1e-3F) << "at its chest";
    EXPECT_FALSE(bots->commands()[0].command.buttons.test(cs::Button::Attack))
        << "released for the next round";
    EXPECT_TRUE(bots->commands()[0].command.buttons.test(cs::Button::Attack));
}

TEST_F(BotsTest, ABotFiresAtNoTeammateAndSeesNoEnemyBehindIt) {
    const cs::PlayerIndex bot = bots->add(cs::Team::CounterTerrorists);
    const cs::PlayerIndex teammate = game->join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier);
    const cs::PlayerIndex enemy = game->join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier);
    standAt(bot, kMapSide * 0.5F);
    standAt(teammate, (kMapSide * 0.5F) + kApart);
    standAt(enemy, (kMapSide * 0.5F) - kApart);
    // It sees no one to pursue: it draws a place of the mesh to hunt, then goes there.
    EXPECT_FALSE(bots->commands()[0].command.buttons.test(cs::Button::Attack));
    const cs::UserCommand hunting = bots->commands()[0].command;
    EXPECT_FALSE(hunting.buttons.test(cs::Button::Attack));
    EXPECT_GT(hunting.move.forward.value(), 0.0F) << "along its way, facing it";
}

TEST_F(BotsTest, ABotKillsAnEnemyItSeesOnceTheFreezeIsOver) {
    const cs::PlayerIndex bot = bots->add(cs::Team::CounterTerrorists);
    const cs::PlayerIndex enemy = game->join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier);
    // Both teams have players: Game Commencing restarts the game, spawning them again.
    play(kPastTheCommence);
    standAt(bot, 0.0F);
    standAt(enemy, kApart);
    play(kPastAFreeze);
    EXPECT_TRUE(game->ownStateOf(enemy).isAlive) << "its attack held back while frozen";
    for (std::uint32_t tick = 0; tick < kKillingTicks && game->ownStateOf(enemy).isAlive; ++tick) {
        play(1);
    }
    EXPECT_FALSE(game->ownStateOf(enemy).isAlive);
    EXPECT_EQ(game->rounds().end(), cs::RoundEndReason::CounterTerroristsWin);
}

} // namespace
