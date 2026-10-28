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
import nx.navigation;
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
/// bot_prefix's default: the bots named after nothing.
constexpr std::string_view kNoPrefix;
/// The profiles bot_difficulty 3 draws from in the repository's data: the expert and the elite levels'.
constexpr std::size_t kExpertProfiles = 18;
/// Ticks of bot_quota's interval, 0.3 seconds.
constexpr std::uint32_t kPastAQuotaCheck = 30;
/// Ticks past the first freeze of a map: mp_freezetime's default and its extra two seconds, 800 ticks.
constexpr std::uint32_t kPastTheFirstFreeze = 810;
/// Ticks past the restart Game Commencing asks for (kCommenceDelay, 300 ticks).
constexpr std::uint32_t kPastTheCommence = 301;
/// Ticks past a round's freeze, mp_freezetime's default (600 ticks).
constexpr std::uint32_t kPastAFreeze = 610;
/// Ticks a bot is given to kill an enemy it sees once the freeze is over.
constexpr std::uint32_t kKillingTicks = 1000;
/// Ticks an expert bot is given to see an enemy, react to it and turn to fire at it: two seconds.
constexpr std::uint32_t kReactingTicks = 200;
/// The ticks of an expert's reaction time, 0.2 seconds.
constexpr std::uint32_t kExpertReactionTicks = 20;
/// The most a bot's view turns in a command, its spring's: far less than the enemy's way off it.
constexpr float kMostTurnPerCommand = 10.0F;
/// How far off the way to its enemy a bot's view may be as it presses: the angle twice HalfHumanWidth
/// spans at the range of the test, and its aim's error.
constexpr float kAimedWithin = 5.0F;
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
        auto profile = cs::standingPlayerProfile(navigation.value());
        EXPECT_TRUE(profile.has_value()) << (profile ? "" : nx::describe(profile.error()));
        auto built = nx::navigation::NavigationMesh::build(*jobs, game->navigationGeometry(), profile.value(),
                                                           navigation.value().build);
        EXPECT_TRUE(built.has_value()) << (built ? "" : nx::describe(built.error()));
        mesh.emplace(std::move(built).value());
        auto made = cs::Bots::create(*game, *mesh, *jobs, std::move(rules).value());
        EXPECT_TRUE(made.has_value()) << (made ? "" : nx::describe(made.error()));
        bots = std::move(made).value_or(nullptr);
    }

    /// Plays `ticks` ticks of the bots' commands alone.
    void play(std::uint32_t ticks) {
        for (std::uint32_t tick = 0; tick < ticks; ++tick) {
            game->tick(bots->commands());
        }
    }

    /// Stands `player` on the floor at `x` along y kRowY plus `y`, a unit above it.
    void standAt(cs::PlayerIndex player, float x, float y = 0.0F) {
        game->placeEye(player, {.x = x, .y = kRowY + y, .z = kStandingEye});
    }

    /// The command of the one bot of the game for the next tick, which the game then plays.
    auto step() -> cs::UserCommand {
        const std::vector<cs::PlayerCommand> made = bots->commands();
        EXPECT_EQ(made.size(), 1U);
        game->tick(made);
        return made.front().command;
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
    std::optional<nx::navigation::NavigationMesh> mesh; ///< The map's, which the bots walk.
    std::unique_ptr<cs::Bots> bots;                     ///< Before the game it plays in and its mesh go.
};

TEST_F(BotsTest, ABotJoinsTheTeamOfFewerPlayersAndLeavesItsSlotWhenKicked) {
    const cs::PlayerIndex human = game->join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex bot = bots->add(std::nullopt, cs::BotDifficulty::Easy, kNoPrefix);
    EXPECT_EQ(game->ownStateOf(bot).team, cs::Team::CounterTerrorists) << "SelectDefaultTeam";
    EXPECT_TRUE(game->ownStateOf(bot).isAlive);
    EXPECT_EQ(game->playerCount(), 2U);
    bots->kickAll();
    EXPECT_EQ(bots->size(), 0U);
    EXPECT_EQ(game->playerCount(), 1U);
    EXPECT_EQ(game->teamCounts()[cs::Team::CounterTerrorists].players(), 0U);
    // The slot it left is the next player's.
    EXPECT_EQ(game->join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player"), bot);
    EXPECT_NE(bot, human);
}

TEST_F(BotsTest, ABotsBodyIsAnAgentWhoseTypeAnotherKindMayRegisterToo) {
    const cs::PlayerIndex bot = bots->add(cs::Team::Terrorists, cs::BotDifficulty::Easy, kNoPrefix);
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
        std::ignore = game->join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    }
    bots->keepQuota(2, cs::BotDifficulty::Easy, kNoPrefix);
    EXPECT_EQ(bots->size(), 1U);
    bots->keepQuota(2, cs::BotDifficulty::Easy, kNoPrefix);
    EXPECT_EQ(bots->size(), 1U) << "once in its interval";
    // Its joining commenced the game: past the restart, the second joins the round played.
    play(kPastTheCommence);
    bots->keepQuota(2, cs::BotDifficulty::Easy, kNoPrefix);
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
    bots->keepQuota(1, cs::BotDifficulty::Easy, kNoPrefix);
    EXPECT_TRUE(std::ranges::equal(bots->players(), std::array{joined[0]})) << "the dead one first";
}

TEST_F(BotsTest, EachBotPlaysWithALevelOfSkillOfTheDifficultyItJoinsAt) {
    const auto levelOf = [this](cs::BotDifficulty difficulty) {
        return bots->skillLevelOf(bots->add(cs::Team::CounterTerrorists, difficulty, kNoPrefix)).name;
    };
    for (const auto& [difficulty, names] :
         {std::pair{cs::BotDifficulty::Easy, std::vector<std::string>{"easy", "fair"}},
          std::pair{cs::BotDifficulty::Normal, std::vector<std::string>{"fair", "normal", "tough"}},
          std::pair{cs::BotDifficulty::Hard, std::vector<std::string>{"tough", "hard", "veryHard"}},
          std::pair{cs::BotDifficulty::Expert, std::vector<std::string>{"expert", "elite"}}}) {
        EXPECT_TRUE(std::ranges::contains(names, levelOf(difficulty))) << nx::enumName(difficulty);
    }
}

TEST_F(BotsTest, EachBotPlaysAsAProfileOfItsDifficultyNoOneElsePlaysAsNamedAfterBotPrefix) {
    std::vector<std::string> names;
    for (std::uint32_t added = 0; added < 3; ++added) {
        const cs::PlayerIndex bot =
            bots->add(cs::Team::CounterTerrorists, cs::BotDifficulty::Expert, kNoPrefix);
        names.emplace_back(game->nameOf(bot));
        const cs::BotSkillLevel& level = bots->skillLevelOf(bot);
        EXPECT_TRUE(std::ranges::contains(level.profiles, names.back())) << "its level's profile";
    }
    std::ranges::sort(names);
    EXPECT_EQ(std::ranges::unique(names).begin(), names.end()) << "each a profile no one else plays as";
    const cs::PlayerIndex prefixed = bots->add(cs::Team::Terrorists, cs::BotDifficulty::Expert, "[NX]");
    EXPECT_TRUE(game->nameOf(prefixed).starts_with("[NX] ")) << game->nameOf(prefixed);
}

TEST_F(BotsTest, ABotJoinsAsAProfileByItsNameInAnyCaseWhileNoOneHasIt) {
    const auto added = bots->addProfile("abner", cs::Team::Terrorists, kNoPrefix);
    ASSERT_TRUE(added.has_value());
    EXPECT_EQ(game->nameOf(*added), "Abner") << "the profile's own name";
    EXPECT_EQ(bots->skillLevelOf(*added).name, "easy");
    EXPECT_EQ(bots->addProfile("Abner", std::nullopt, "[NX]").error().code, cs::BotJoinError::ProfileInGame)
        << "a bot plays as it, whatever its prefix";
    EXPECT_EQ(bots->addProfile("Nobody", std::nullopt, kNoPrefix).error().code,
              cs::BotJoinError::UnknownProfile);
    std::ignore = game->join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Basil");
    EXPECT_EQ(bots->addProfile("Basil", std::nullopt, kNoPrefix).error().code,
              cs::BotJoinError::ProfileInGame)
        << "a player has its name";
}

TEST_F(BotsTest, ABotLeavesByItsNameExactly) {
    ASSERT_TRUE(bots->addProfile("Abner", cs::Team::Terrorists, "[NX]").has_value());
    EXPECT_FALSE(bots->kickNamed("Abner")) << "its name is after bot_prefix";
    EXPECT_FALSE(bots->kickNamed("[nx] abner"));
    EXPECT_TRUE(bots->kickNamed("[NX] Abner"));
    EXPECT_EQ(bots->size(), 0U);
    EXPECT_EQ(game->playerCount(), 0U);
}

TEST_F(BotsTest, WithEveryProfileOfItsDifficultyInTheGameABotStillJoinsItsNameNumbered) {
    for (std::size_t added = 0; added < kExpertProfiles; ++added) {
        std::ignore = bots->add(cs::Team::CounterTerrorists, cs::BotDifficulty::Expert, kNoPrefix);
    }
    const cs::PlayerIndex numbered = bots->add(cs::Team::Terrorists, cs::BotDifficulty::Expert, kNoPrefix);
    EXPECT_TRUE(game->nameOf(numbered).starts_with("(1)")) << game->nameOf(numbered);
    EXPECT_EQ(bots->size(), kExpertProfiles + 1);
}

TEST_F(BotsTest, ABotFiresOnlyItsReactionTimeAfterItSeesAnEnemyAndOnceItsViewTurnedToIt) {
    const cs::PlayerIndex bot = bots->add(cs::Team::CounterTerrorists, cs::BotDifficulty::Expert, kNoPrefix);
    const cs::PlayerIndex enemy = game->join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    // The bot faces +x as its spawn point does; the enemy stands in its cone, half a right angle off.
    standAt(bot, 0.0F);
    standAt(enemy, kApart, kApart);
    constexpr float kEnemyYaw = 45.0F;
    nx::Degrees view = game->ownStateOf(bot).view.yaw;
    std::optional<std::uint32_t> firstPress;
    for (std::uint32_t tick = 0; tick < kReactingTicks && !firstPress; ++tick) {
        const cs::UserCommand command = step();
        EXPECT_LE(std::abs(nx::normalizeAngle(command.viewAngles.yaw - view).value()), kMostTurnPerCommand)
            << "turned by its spring, at " << tick;
        view = command.viewAngles.yaw;
        if (command.buttons.test(cs::Button::Attack)) {
            firstPress = tick;
            EXPECT_NEAR(command.viewAngles.yaw.value(), kEnemyYaw, kAimedWithin) << "aimed at it";
        }
    }
    ASSERT_TRUE(firstPress.has_value());
    EXPECT_GE(*firstPress, kExpertReactionTicks) << "not before its reaction time";
}

TEST_F(BotsTest, ABotFiresAtNoTeammateAndSeesNoEnemyBehindIt) {
    const cs::PlayerIndex bot = bots->add(cs::Team::CounterTerrorists, cs::BotDifficulty::Easy, kNoPrefix);
    const cs::PlayerIndex teammate =
        game->join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex enemy = game->join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    standAt(bot, kMapSide * 0.5F);
    standAt(teammate, (kMapSide * 0.5F) + kApart);
    standAt(enemy, (kMapSide * 0.5F) - kApart);
    // It sees no one to pursue: it draws a place of the mesh to hunt, then goes there.
    EXPECT_FALSE(bots->commands()[0].command.buttons.test(cs::Button::Attack));
    const cs::UserCommand hunting = bots->commands()[0].command;
    EXPECT_FALSE(hunting.buttons.test(cs::Button::Attack));
    EXPECT_GT(hunting.move.forward.value(), 0.0F) << "along its way, facing it";
}

TEST_F(BotsTest, AnExpertBotKillsAnEnemyItSeesOnceTheFreezeIsOver) {
    const cs::PlayerIndex bot = bots->add(cs::Team::CounterTerrorists, cs::BotDifficulty::Expert, kNoPrefix);
    const cs::PlayerIndex enemy = game->join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
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
