// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The game of a map as its server plays it: a player joining at its team's spawn point with its loadout
// and the starting money, moved by its commands tick by tick once the first freeze is over, the round
// ended in a draw by its death and its spawn again as the next starts, the game's restart, and what each
// tick's snapshot tells its client; players of both teams: each team's spawn points in turn, Game
// Commencing, a team eliminated and its winners paid, a player joining late watching until the next
// round, and players solid to each other; what a client is told of the other players, and how their models
// play; the players' shots and knives meeting each other's hitboxes, hurting them by where they meet them
// and through their armor, flinching them, sparing their teammates or not, and paying for a kill; on the
// BSP seed of the fuzz corpus (a floor at z = 0 in its hulls) given other entities, by the repository's
// data, each character's model of the shape of Counter-Strike's.

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
#include "entityFixtures.hpp"
#include "playerModel.hpp"
#include "temporaryDirectory.hpp"

namespace {

/// The box's counter-terrorist spawn points, low and far up, and two terrorist ones far from them.
constexpr std::string_view kEntities = R"({ "classname" "worldspawn" }
{ "classname" "light" "targetname" "lamp" "style" "32" "spawnflags" "1" }
{ "classname" "light_spot" "targetname" "sign" "style" "33" "pattern" "za" }
{ "classname" "info_player_start" "origin" "32 32 8" "angles" "0 90 0" }
{ "classname" "info_player_start" "origin" "16 16 2000" "angles" "0 180 0" }
{ "classname" "info_player_deathmatch" "origin" "300 300 8" "angles" "0 270 0" }
{ "classname" "info_player_deathmatch" "origin" "600 300 8" "angles" "0 0 0" })";

constexpr float kTolerance = 1e-3F;
/// How far off what it met a trace stops: the original's DIST_EPSILON, back along its way.
constexpr float kTraceEpsilon = 0.03125F;
/// Ticks past the first freeze of a map: mp_freezetime's default and its extra two seconds, 800 ticks.
constexpr std::uint32_t kPastTheFirstFreeze = 810;
/// Ticks past the next round's start after one ends (kRestartDelay, 500 ticks).
constexpr std::uint32_t kPastTheRestartDelay = 501;
/// Ticks past a restart sv_restart asks for one second on, read once a second.
constexpr std::uint32_t kPastARestart = 250;
/// Ticks past the restart Game Commencing asks for (kCommenceDelay, 300 ticks).
constexpr std::uint32_t kPastTheCommence = 301;
/// Ticks past a round's freeze, mp_freezetime's default (600 ticks).
constexpr std::uint32_t kPastAFreeze = 610;
/// Ticks past the time a player joining still spawns in its round (kJoinSpawnWindow, 2000 ticks).
constexpr std::uint32_t kPastTheJoinWindow = 2010;
/// Above a floor: a fall from there kills.
constexpr float kDeadlyHeight = 2000.0F;
/// Ticks a weapon drawn takes to be ready to attack, and more.
constexpr std::uint32_t kReadyTicks = 120;
/// How far ahead along +x of its attacker a victim of the attacks' tests stands, along y kDuelY; the
/// eye of either placed a unit above where it stands on the floor, and the ticks it takes to land.
constexpr float kVictimAhead = 40.0F;
constexpr float kDuelY = 1000.0F;
constexpr float kStandingEye = cs::kStandingViewHeight.value() + 1.0F;
constexpr std::uint32_t kLandingTicks = 20;

/// A game of the box, with the repository's data and an installation of the characters' models alone.
class GameTest : public ::testing::Test {
protected:
    GameTest() {
        const std::filesystem::path charactersFile =
            std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "characters.toml";
        auto characters = cs::readCharacters(
            nx::platform::readTextFile(charactersFile).value_or(std::string{}), "characters.toml");
        EXPECT_TRUE(characters.has_value());
        const std::string model =
            nx::test::playerModelOf(nx::test::playerSequences()) |
            std::views::transform([](std::byte value) { return static_cast<char>(value); }) |
            std::ranges::to<std::string>();
        for (const cs::Character& character : characters.value_or(cs::Characters{}).characters) {
            m_installation.write(cs::modelOf(character), model);
        }
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

    /// Plays a tick of `player`'s command looking along `view` with `buttons` down, asking to draw `weapon`;
    /// returns its snapshot.
    auto press(cs::PlayerIndex player, nx::Flags<cs::Button> buttons, nx::EulerAngles view = {},
               std::optional<cs::WeaponIndex> weapon = std::nullopt) -> cs::PlayerSnapshot {
        m_lastCommand = cs::CommandNumber{m_lastCommand.value + 1};
        const std::array commands{
            cs::PlayerCommand{.player = player,
                              .command = cs::UserCommand{.number = m_lastCommand,
                                                         .duration = nx::Milliseconds{10.0F},
                                                         .viewAngles = view,
                                                         .buttons = buttons,
                                                         .weaponSelect = weapon}}};
        m_game->tick(commands);
        return m_game->snapshotFor(player);
    }

    /// `attacker` standing on the floor at x 0 and y `attackerY`, looking along +x, `weapon` drawn and
    /// ready; `victim` standing kVictimAhead further along +x at y kDuelY, its back to it, aiming, its
    /// legs along +x: placed a unit short of there first, its legs then turned the way it moved
    /// (StudioEstimateGait).
    void standOff(cs::PlayerIndex attacker, cs::PlayerIndex victim, std::string_view weapon,
                  float attackerY = kDuelY) {
        m_game->placeEye(victim, {.x = kVictimAhead - 1.0F, .y = kDuelY, .z = kStandingEye});
        std::ignore = play(victim, 1);
        m_game->placeEye(victim, {.x = kVictimAhead, .y = kDuelY, .z = kStandingEye});
        std::ignore = play(victim, kLandingTicks);
        placeAttacker(attacker, attackerY);
        std::ignore = press(attacker, {}, {}, m_content->weapons.indexOf(weapon));
        std::ignore = play(attacker, kReadyTicks);
    }

    /// `attacker` standing on the floor at x 0 and y `y`.
    void placeAttacker(cs::PlayerIndex attacker, float y = kDuelY) {
        m_game->placeEye(attacker, {.y = y, .z = kStandingEye});
        std::ignore = play(attacker, kLandingTicks);
    }

    /// How `seen` is shown to `viewer`'s client.
    [[nodiscard]] auto shown(cs::PlayerIndex viewer, cs::PlayerIndex seen) -> cs::PlayerEntityState {
        const cs::PlayerSnapshot snapshot = m_game->snapshotFor(viewer);
        const auto found = std::ranges::find(snapshot.players, seen, &cs::PlayerEntityState::player);
        EXPECT_NE(found, snapshot.players.end());
        return found != snapshot.players.end() ? *found : cs::PlayerEntityState{};
    }

    /// Whether `player` was heard making `sound` in the last tick.
    [[nodiscard]] auto made(cs::PlayerIndex player, const cs::GameSound& sound) -> bool {
        return std::ranges::contains(m_game->snapshotFor(player).sounds, &sound, &cs::SoundMade::sound);
    }

    /// Plays `ticks` ticks of no player's command.
    void idle(std::uint32_t ticks) {
        for (std::uint32_t tick = 0; tick < ticks; ++tick) {
            m_game->tick({});
        }
    }

    /// Kills `player` by a fall from kDeadlyHeight; returns the snapshot of its death.
    auto killByFall(cs::PlayerIndex player) -> cs::PlayerSnapshot {
        const cs::PlayerSnapshot before = m_game->snapshotFor(player);
        m_game->placeEye(player,
                         {.x = before.player.origin.x, .y = before.player.origin.y, .z = kDeadlyHeight});
        cs::PlayerSnapshot fallen = play(player, 1);
        for (std::uint32_t tick = 0; tick < kPastAFreeze && fallen.status.isAlive(); ++tick) {
            fallen = play(player, 1);
        }
        EXPECT_FALSE(fallen.status.isAlive()) << "killed by its landing";
        return fallen;
    }

    [[nodiscard]] auto game() -> cs::Game& { return *m_game; }
    [[nodiscard]] auto content() const -> const cs::GameContent& { return *m_content; }
    [[nodiscard]] auto cvars() const -> const cs::GameCvars& { return *m_cvars; }
    [[nodiscard]] auto sequence(std::string_view label) const -> nx::assets::SequenceIndex {
        return nx::assets::SequenceIndex{nx::test::playerSequenceIndex(label)};
    }

    [[nodiscard]] auto entityType() -> nx::Result<cs::EntityTypeIndex, cs::EntityError> {
        nx::test::writeEntityModel(m_installation);
        auto vfs = nx::assets::Vfs::create(std::array{m_installation.path()});
        if (!vfs) {
            return nx::fail(cs::EntityError::ModelUnreadable, std::move(vfs.error()));
        }
        return m_content->entities.add(*vfs, nx::test::entityDefinition(), cs::ModelTextures::Skipped);
    }

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
    const cs::PlayerIndex player =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
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

TEST_F(GameTest, APlayerPlaysByTheNameTheServerGivesItAmongTheOthers) {
    const cs::PlayerIndex first =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, " Bruno ");
    const cs::PlayerIndex second = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "bruno");
    EXPECT_EQ(game().nameOf(first), "Bruno");
    EXPECT_EQ(game().nameOf(second), "(1)bruno") << "another has it, in any case";
    EXPECT_EQ(game().playerNamed("(1)bruno"), second);
    EXPECT_FALSE(game().playerNamed("BRUNO").has_value()) << "found by its name exactly";
    game().leave(first);
    EXPECT_FALSE(game().playerNamed("Bruno").has_value());
    EXPECT_EQ(game().nameOf(game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Bruno")),
              "Bruno")
        << "free again once its player left";
}

TEST_F(GameTest, GenericEntitiesConsumeNoPlayerSlotAndCanBeRemovedOnlyByTheirCurrentHandle) {
    const auto player = game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const auto type = entityType();
    ASSERT_TRUE(type.has_value()) << (type ? "" : nx::describe(type.error()));
    const auto created = game().createEntity(*type, {.origin = {.x = 200, .y = 32, .z = 40}});
    ASSERT_TRUE(created.has_value()) << (created ? "" : nx::describe(created.error()));
    const auto snapshot = game().snapshotFor(player);
    EXPECT_TRUE(snapshot.players.empty());
    ASSERT_EQ(snapshot.entities.size(), 1U);
    EXPECT_EQ(snapshot.entities.front().entity, *created);
    ASSERT_TRUE(game().removeEntity(*created).has_value());
    EXPECT_FALSE(game().removeEntity(*created).has_value());
    EXPECT_TRUE(game().snapshotFor(player).entities.empty());
}

TEST_F(GameTest, EntityMeleeHurtsAPlayerByTheNormalDeathPipelineAtItsConfiguredInterval) {
    const auto player = game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    game().placeEye(player, {.x = 0, .y = 0, .z = 53});
    const auto type = entityType();
    ASSERT_TRUE(type.has_value()) << (type ? "" : nx::describe(type.error()));
    const auto created = game().createEntity(*type, {.origin = {.x = 50, .z = 40}});
    ASSERT_TRUE(created.has_value()) << (created ? "" : nx::describe(created.error()));
    cs::UserCommand attack{.duration = nx::Milliseconds{1000}, .viewAngles = {.yaw = nx::Degrees{180}}};
    attack.buttons.set(cs::Button::Attack);
    const std::array commands{cs::EntityCommand{.entity = *created, .command = attack}};
    game().tick({}, commands);
    EXPECT_EQ(game().snapshotFor(player).status.health, nx::HitPoints{80});
    for (std::uint32_t tick = 0; tick < 99; ++tick) {
        game().tick({}, commands);
    }
    EXPECT_EQ(game().snapshotFor(player).status.health, nx::HitPoints{80});
    for (std::uint32_t tick = 0; tick < 301; ++tick) {
        game().tick({}, commands);
    }
    const auto dead = game().snapshotFor(player);
    EXPECT_FALSE(dead.status.isAlive());
    EXPECT_TRUE(dead.player.isDead);
    EXPECT_TRUE(dead.inventory.weapons().empty());
}

TEST_F(GameTest, EntitySightIsBlockedByAnotherSolidEntity) {
    const auto player = game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    // The synthetic map's clip hulls put the body's origin at z = 0 on the floor.
    game().placeEye(player, {.x = 0, .y = 0, .z = cs::kStandingViewHeight.value()});
    const auto type = entityType();
    ASSERT_TRUE(type.has_value());
    const auto observer = game().createEntity(*type, {.origin = {.x = 150, .z = 40}});
    ASSERT_TRUE(observer.has_value());
    const auto blocker = game().createEntity(*type, {.origin = {.x = 75, .z = 40}});
    ASSERT_TRUE(blocker.has_value());
    const auto targets = game().perceptionTargets();
    ASSERT_EQ(targets.size(), 1U);
    const auto states = game().entities();
    const auto found = std::ranges::find(states, *observer, &cs::EntityState::entity);
    ASSERT_NE(found, states.end());
    const nx::Vec3 eye = found->origin + nx::Vec3{.z = cs::kStandingViewHeight.value()};
    EXPECT_FALSE(game().sees(*observer, eye, targets.front()));
    ASSERT_TRUE(game().removeEntity(*blocker).has_value());
    EXPECT_TRUE(game().sees(*observer, eye, targets.front()));
}

TEST_F(GameTest, PerceivedFeetUseThePlayersActiveStanceIndependentlyOfTheAimPoint) {
    constexpr float kStandingEyeAboveFeet = 53.0F;
    constexpr float kCrouchedEyeAboveFeet = 30.0F;
    const auto player = game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    std::ignore = play(player, kPastTheFirstFreeze);
    const auto standing = game().perceptionTargets();
    ASSERT_EQ(standing.size(), 1U);
    EXPECT_FLOAT_EQ(standing.front().eye.z - standing.front().feet.z, kStandingEyeAboveFeet);
    cs::UserCommand duck{.duration = nx::tickLengthOf(game().time().rate)};
    duck.buttons.set(cs::Button::Duck);
    const std::array commands{cs::PlayerCommand{.player = player, .command = duck}};
    // Simulate a full second, beyond the 400 ms ground duck transition.
    for (std::uint32_t tick = 0; tick < game().time().rate.value(); ++tick) {
        game().tick(commands);
    }
    ASSERT_TRUE(game().snapshotFor(player).player.isDucked);
    const auto crouched = game().perceptionTargets();
    ASSERT_EQ(crouched.size(), 1U);
    EXPECT_EQ(crouched.front().entity, standing.front().entity);
    EXPECT_FLOAT_EQ(crouched.front().eye.z - crouched.front().feet.z, kCrouchedEyeAboveFeet);
}

TEST_F(GameTest, LocalWeaponsHitAnimatedGenericEntityHitboxesAndKillThem) {
    const auto player = game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const auto type = entityType();
    ASSERT_TRUE(type.has_value()) << (type ? "" : nx::describe(type.error()));
    const auto created = game().createEntity(*type, {.origin = {.x = 200, .y = 32, .z = 40}});
    ASSERT_TRUE(created.has_value()) << (created ? "" : nx::describe(created.error()));
    game().give(player, *content().weapons.indexOf("ak47"));
    std::ignore = play(player, kPastTheFirstFreeze);
    cs::UserCommand attack{.number = cs::CommandNumber{1}, .duration = nx::Milliseconds{10}};
    attack.buttons.set(cs::Button::Attack);
    const std::array commands{cs::PlayerCommand{.player = player, .command = attack}};
    for (std::uint32_t tick = 0; tick < 120; ++tick) {
        game().tick(commands);
    }
    const auto snapshot = game().snapshotFor(player);
    ASSERT_EQ(snapshot.entities.size(), 1U);
    EXPECT_EQ(snapshot.entities.front().health, nx::HitPoints{});
    EXPECT_EQ(snapshot.entities.front().animation.sequence,
              content().entities.type(snapshot.entities.front().type).sequences.death);
}

TEST_F(GameTest, EachTickTellsTheClientWhatItMadeAndPlacedNothingAgain) {
    const cs::PlayerIndex player =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
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
    const cs::PlayerIndex player =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
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
    const cs::PlayerIndex player =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
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
    const cs::PlayerIndex player = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
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
    EXPECT_EQ(restarted->player.origin, (nx::Vec3{.x = 600.0F, .y = 300.0F, .z = 9.0F}))
        << "the terrorists' next spawn point";
    EXPECT_EQ(restarted->status.money, cs::kStartMoney);
    EXPECT_FALSE(std::ranges::contains(restarted->inventory.weapons(), *ak47, &cs::OwnedWeapon::weapon))
        << "with its team's loadout alone";
    EXPECT_TRUE(game().rounds().isFrozen()) << "a restarted game starts frozen";
}

TEST_F(GameTest, EachPlayerOfATeamSpawnsAtItsTeamsNextSpawnPoint) {
    const cs::PlayerIndex first =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex second =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex third =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    EXPECT_EQ(game().snapshotFor(first).player.origin, (nx::Vec3{.x = 32.0F, .y = 32.0F, .z = 9.0F}));
    EXPECT_EQ(game().snapshotFor(second).player.origin, (nx::Vec3{.x = 16.0F, .y = 16.0F, .z = 2001.0F}));
    EXPECT_EQ(game().snapshotFor(third).player.origin, (nx::Vec3{.x = 32.0F, .y = 32.0F, .z = 9.0F}))
        << "the first after the last";
    EXPECT_FALSE(game().rounds().isGameStarted()) << "no terrorists yet";
}

TEST_F(GameTest, TheSecondTeamsFirstPlayerCommencesTheGame) {
    const cs::PlayerIndex counterTerrorist =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex terrorist = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    EXPECT_EQ(game().rounds().end(), cs::RoundEndReason::GameCommencing);
    EXPECT_TRUE(game().rounds().isGameStarted());
    EXPECT_FALSE(game().rounds().isFrozen()) << "unfrozen until the restart";
    idle(kPastTheCommence);
    EXPECT_TRUE(game().rounds().isFrozen()) << "the restarted game's first round";
    EXPECT_EQ(game().rounds().end(), std::nullopt);
    for (const cs::PlayerIndex player : {counterTerrorist, terrorist}) {
        const cs::PlayerSnapshot restarted = game().snapshotFor(player);
        EXPECT_TRUE(restarted.status.isAlive());
        EXPECT_EQ(restarted.status.money, cs::kStartMoney);
    }
}

TEST_F(GameTest, ATeamEliminatedLosesTheRoundAndItsWinnersArePaid) {
    const cs::PlayerIndex counterTerrorist =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex terrorist = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    idle(kPastTheCommence);
    std::ignore = killByFall(terrorist);
    EXPECT_EQ(game().rounds().end(), cs::RoundEndReason::CounterTerroristsWin);
    idle(kPastTheRestartDelay);
    // The box has no bomb target: RR_CTS_WIN for the winners, the first loser bonus for the losers.
    EXPECT_EQ(game().snapshotFor(counterTerrorist).status.money, nx::Money{cs::kStartMoney.value() + 3000});
    EXPECT_EQ(game().snapshotFor(terrorist).status.money, nx::Money{cs::kStartMoney.value() + 1400});
    EXPECT_TRUE(game().snapshotFor(terrorist).status.isAlive()) << "spawned again";

    std::ignore = killByFall(counterTerrorist);
    EXPECT_EQ(game().rounds().end(), cs::RoundEndReason::TerroristsWin);
}

TEST_F(GameTest, APlayerJoiningLateWatchesUntilTheNextRound) {
    std::ignore = game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    std::ignore = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    idle(kPastTheCommence + kPastAFreeze);
    const cs::PlayerIndex early = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    EXPECT_TRUE(game().snapshotFor(early).status.isAlive()) << "within 20 seconds of the round's start";
    idle(kPastTheJoinWindow);
    const cs::PlayerIndex late = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerSnapshot watching = game().snapshotFor(late);
    EXPECT_FALSE(watching.status.isAlive());
    EXPECT_TRUE(watching.player.isDead);
    EXPECT_TRUE(watching.inventory.weapons().empty());
    EXPECT_EQ(game().rounds().end(), std::nullopt) << "its team's others alive";
    EXPECT_EQ(watching.status.money, cs::kStartMoney);
}

TEST_F(GameTest, PlayersAreSolidToEachOther) {
    const cs::PlayerIndex walker =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex standing = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    idle(kPastTheCommence + kPastAFreeze);
    // On the floor, a box's width and a half apart along x.
    game().placeEye(walker, {.y = 1000.0F, .z = cs::kStandingViewHeight.value() + 1.0F});
    game().placeEye(standing, {.x = 48.0F, .y = 1000.0F, .z = cs::kStandingViewHeight.value() + 1.0F});
    const cs::PlayerSnapshot walked = play(walker, 50, {.forward = nx::UnitsPerSecond{250.0F}});
    EXPECT_GT(walked.player.origin.x, 1.0F) << "it walked";
    EXPECT_LE(walked.player.origin.x, 16.0F + kTolerance) << "stopped by the other's box";
}

TEST_F(GameTest, AClientIsToldOfTheOtherPlayersAsTheirModelsPlay) {
    const cs::PlayerIndex counterTerrorist =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex terrorist = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    idle(kPastTheCommence);
    std::ignore = play(terrorist, 1, {}, {.pitch = nx::Degrees{30.0F}, .yaw = nx::Degrees{45.0F}});
    const cs::PlayerSnapshot seen = game().snapshotFor(counterTerrorist);
    ASSERT_EQ(seen.players.size(), 1U) << "the other player alone";
    const cs::PlayerEntityState& other = seen.players.front();
    EXPECT_EQ(other.player, terrorist);
    EXPECT_EQ(content().characters.character(other.character).team, cs::Team::Terrorists);
    ASSERT_TRUE(other.weapon.has_value());
    EXPECT_EQ(content().weapons.weapon(*other.weapon).id, "glock18") << "the heavier of its loadout, drawn";
    EXPECT_EQ(other.origin, game().snapshotFor(terrorist).player.origin);
    EXPECT_FLOAT_EQ(other.angles.pitch.value(), -10.0F) << "a third of the view's, negated";
    EXPECT_FLOAT_EQ(other.angles.yaw.value(), 45.0F);
    // Standing with the Glock: it aims it one-handed, its legs idle.
    EXPECT_EQ(other.sequence.value, nx::test::playerSequenceIndex("ref_aim_onehanded"));
    EXPECT_EQ(other.gaitSequence.value, nx::test::playerSequenceIndex("idle1"));
}

TEST_F(GameTest, AKilledPlayersModelPlaysADeathItsLegsNoGait) {
    const cs::PlayerIndex counterTerrorist =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex terrorist = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    idle(kPastTheCommence);
    std::ignore = killByFall(terrorist);
    const cs::PlayerSnapshot seen = game().snapshotFor(counterTerrorist);
    ASSERT_EQ(seen.players.size(), 1U);
    const cs::PlayerEntityState& dead = seen.players.front();
    EXPECT_GE(dead.sequence.value, nx::test::playerSequenceIndex("death1"));
    EXPECT_LE(dead.sequence.value, nx::test::playerSequenceIndex("crouch_die"));
    EXPECT_EQ(dead.gaitSequence.value, 0U);
    EXPECT_FALSE(dead.weapon.has_value()) << "its weapons gone with it";
}

TEST_F(GameTest, AKnifeMeetsAPlayersHeadByItsHitboxesAndHurtsItFourTimesAsMuch) {
    const cs::PlayerIndex attacker =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex victim = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    idle(kPastTheCommence + kPastAFreeze);
    standOff(attacker, victim, "knife");
    // A slash at the eye's height meets its head: 15 times 4.
    std::ignore = press(attacker, {cs::Button::Attack});
    const cs::PlayerSnapshot hurt = game().snapshotFor(victim);
    EXPECT_EQ(hurt.status.health, nx::HitPoints{40.0F});
    EXPECT_FLOAT_EQ(hurt.player.punch.pitch.value(), -12.0F) << "half of 60 up, 12 at most";
    EXPECT_TRUE(made(victim, content().playerHarm.headPain));
    EXPECT_TRUE(made(attacker, *content().weapons.findWeapon("knife")->melee->slash.bodySound));
    EXPECT_EQ(shown(attacker, victim).sequence, sequence("head_flinch")) << "the knife's is a small flinch";
}

TEST_F(GameTest, AHelmetTakesItsShareOfAHitInTheHeadAndSpillsNoBlood) {
    const cs::PlayerIndex attacker =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex victim = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    idle(kPastTheCommence + kPastAFreeze);
    standOff(attacker, victim, "knife");
    game().giveArmor(victim, cs::ArmorKind::KevlarAndHelmet);
    std::ignore = press(attacker, {cs::Button::Attack});
    // The knife's ratio, 0.5 times 1.7: 51 of the 60 to the health, half the rest off the armor.
    const cs::PlayerSnapshot hurt = game().snapshotFor(victim);
    EXPECT_EQ(hurt.status.health, nx::HitPoints{49.0F});
    EXPECT_FLOAT_EQ(hurt.status.armor.value(), 95.5F);
    EXPECT_EQ(hurt.status.armorKind, cs::ArmorKind::KevlarAndHelmet);
    EXPECT_EQ(hurt.player.punch, nx::EulerAngles{});
    EXPECT_TRUE(made(victim, content().playerHarm.helmetPain));
    EXPECT_TRUE(hurt.bloodMarks.empty());
    EXPECT_TRUE(hurt.bloodStreams.empty()) << "the helmet keeps the head's stream in";
    // It strikes sparks off the helmet where the blade met it, off the face it met.
    ASSERT_EQ(hurt.streakSplashes.size(), 1U);
    const cs::StreakSplash& sparks = hurt.streakSplashes.front();
    EXPECT_EQ(sparks.color, content().playerHarm.helmetSparks.color);
    EXPECT_EQ(sparks.count, content().playerHarm.helmetSparks.count);
    EXPECT_EQ(sparks.speed, content().playerHarm.helmetSparks.speed);
    EXPECT_EQ(sparks.spread, content().playerHarm.helmetSparks.spread);
    EXPECT_NEAR(nx::length(sparks.direction), 1.0F, kTolerance) << "a face's normal";
    EXPECT_NEAR(sparks.position.x, kVictimAhead, cs::kStandingBox.max.x);
}

TEST_F(GameTest, ATeammatesAttackSparksOffAHelmetThoughItHurtsNone) {
    const cs::PlayerIndex attacker =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex teammate =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    std::ignore = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    idle(kPastTheCommence + kPastAFreeze);
    standOff(attacker, teammate, "knife");
    game().giveArmor(teammate, cs::ArmorKind::KevlarAndHelmet);
    std::ignore = press(attacker, {cs::Button::Attack});
    const cs::PlayerSnapshot spared = game().snapshotFor(teammate);
    EXPECT_EQ(spared.status.health, cs::kSpawnHealth) << "mp_friendlyfire 0";
    EXPECT_EQ(spared.streakSplashes.size(), 1U) << "TraceAttack sparks before TakeDamage spares it";
}

TEST_F(GameTest, AStabInThePlayersBackKillsItAndPaysItsKiller) {
    const cs::PlayerIndex attacker =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex victim = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    idle(kPastTheCommence + kPastAFreeze);
    standOff(attacker, victim, "knife");
    const nx::Money before = game().snapshotFor(attacker).status.money;
    // Too short for a line to meet its hitboxes, the stab's ducked hull meets its box: no group, its 65
    // three times as much in its back.
    std::ignore = press(attacker, {cs::Button::Attack2});
    const cs::PlayerSnapshot killed = game().snapshotFor(victim);
    EXPECT_FALSE(killed.status.isAlive());
    EXPECT_EQ(killed.status.health, nx::HitPoints{100.0F - 195.0F});
    EXPECT_EQ(game().snapshotFor(attacker).status.money, nx::Money{before.value() + 300})
        << "an enemy's kill";
    EXPECT_EQ(game().rounds().end(), cs::RoundEndReason::CounterTerroristsWin);
    EXPECT_TRUE(made(victim, content().playerHarm.death));
}

TEST_F(GameTest, AShotMeetsAPlayersHitboxesNotItsBoxNorAShieldItHasNot) {
    const cs::PlayerIndex attacker =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex victim = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    idle(kPastTheCommence + kPastAFreeze);
    // 12 units to its right: within its box and its shield's hitbox, beside the others.
    standOff(attacker, victim, "usp", kDuelY - 12.0F);
    std::ignore = press(attacker, {cs::Button::Attack});
    const cs::PlayerSnapshot missed = game().snapshotFor(victim);
    EXPECT_EQ(missed.status.health, cs::kSpawnHealth);
    EXPECT_FALSE(made(victim, content().playerHarm.pain));
    EXPECT_FALSE(made(victim, content().playerHarm.headPain));

    // Straight at it, the round meets its head; the USP's 34, less by the way, four times as much, kills.
    placeAttacker(attacker);
    std::ignore = play(attacker, kReadyTicks);
    const cs::PlayerSnapshot headShot = press(attacker, {cs::Button::Attack});
    EXPECT_FALSE(game().snapshotFor(victim).status.isAlive());
    EXPECT_EQ(shown(attacker, victim).sequence, sequence("head"));
    // Its head streams its blood the way the round went, as fast as a byte holds: over 130 times 5.
    ASSERT_EQ(headShot.bloodStreams.size(), 1U);
    const cs::BloodStream& stream = headShot.bloodStreams.front();
    EXPECT_EQ(stream.color, content().blood.stream.color);
    EXPECT_EQ(stream.speed, 255U);
    EXPECT_GT(stream.direction.x, 0.99F) << "toward the victim";
    EXPECT_NEAR(stream.position.x, kVictimAhead, cs::kStandingBox.max.x);

    // Dead, its body is not solid: a round goes through where it lies.
    std::ignore = play(attacker, kReadyTicks);
    const nx::Money paid = game().snapshotFor(attacker).status.money;
    std::ignore = press(attacker, {cs::Button::Attack});
    EXPECT_EQ(game().snapshotFor(attacker).status.money, paid) << "no other kill";
    EXPECT_FALSE(made(victim, content().playerHarm.headPain));
}

TEST_F(GameTest, AHitShedsBloodOnTheFloorBehindAPlayerWithNoArmorWhereItCoversIt) {
    const cs::PlayerIndex attacker =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex victim = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    idle(kPastTheCommence + kPastAFreeze);
    game().give(attacker, *content().weapons.indexOf("ak47"));
    standOff(attacker, victim, "ak47");
    // Down into its chest: its blood goes on down behind it, onto the box's floor, level with the
    // players' origins.
    const nx::EulerAngles down{.pitch = nx::Degrees{14.0F}};
    const float floor = game().snapshotFor(victim).player.origin.z;
    std::vector<cs::BloodMark> marks;
    for (std::uint32_t shot = 0; shot < 2; ++shot) {
        const cs::PlayerSnapshot shooting = press(attacker, {cs::Button::Attack}, down);
        std::ranges::copy(shooting.bloodMarks, std::back_inserter(marks));
        // Its blood sprayed where the round met its chest: 35 doubled, over 10.
        ASSERT_EQ(shooting.bloodSprays.size(), 1U);
        EXPECT_EQ(shooting.bloodSprays.front().size, 7U);
        EXPECT_EQ(shooting.bloodSprays.front().color, content().blood.spray.color);
        EXPECT_NEAR(shooting.bloodSprays.front().position.x, kVictimAhead, cs::kStandingBox.max.x);
        EXPECT_TRUE(shooting.bloodStreams.empty()) << "a head's alone";
        std::ignore = play(attacker, kReadyTicks);
    }
    ASSERT_TRUE(game().snapshotFor(victim).status.isAlive()) << "35 twice";
    ASSERT_FALSE(marks.empty()) << "one in three of the ways that meet the floor";
    for (const cs::BloodMark& mark : marks) {
        EXPECT_NEAR(mark.position.z, floor, kTraceEpsilon) << "on the floor";
        EXPECT_GT(mark.position.x, kVictimAhead) << "behind it";
        EXPECT_EQ(mark.on.model, nx::assets::kWorldModel);
        EXPECT_LT(mark.decal, content().blood.decals.size());
    }

    // A vest keeps the chest's blood in.
    game().giveArmor(victim, cs::ArmorKind::Kevlar);
    const cs::PlayerSnapshot vested = press(attacker, {cs::Button::Attack}, down);
    EXPECT_TRUE(vested.bloodMarks.empty());
    EXPECT_TRUE(vested.bloodSprays.empty());
    EXPECT_LT(game().snapshotFor(victim).status.armor.value(), 100.0F) << "it met the vest";
}

TEST_F(GameTest, AHeavyWeaponsHitButInTheLegsFlinchesAPlayerLargeKnockingItBack) {
    const cs::PlayerIndex attacker =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex victim = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    idle(kPastTheCommence + kPastAFreeze);
    game().give(attacker, *content().weapons.indexOf("ak47"));
    standOff(attacker, victim, "ak47");
    // Down into its chest (the box's floor is level with the players' origins): the AK-47's 36, cut to 35
    // by the way it went.
    const nx::EulerAngles down{.pitch = nx::Degrees{14.0F}};
    std::ignore = press(attacker, {cs::Button::Attack}, down);
    const cs::PlayerSnapshot hurt = game().snapshotFor(victim);
    EXPECT_EQ(hurt.status.health, nx::HitPoints{65.0F});
    EXPECT_NEAR(hurt.player.velocity.x, 170.0F, kTolerance) << "knocked back away from its attacker";
    EXPECT_EQ(shown(attacker, victim).sequence, sequence("head_flinch")) << "the chest's";
}

TEST_F(GameTest, ATeammatesAttackHurtsNoneButWithFriendlyFireItsShareAndItsKillCosts) {
    const cs::PlayerIndex attacker =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex teammate =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    std::ignore = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    idle(kPastTheCommence + kPastAFreeze);
    standOff(attacker, teammate, "knife");
    std::ignore = press(attacker, {cs::Button::Attack});
    EXPECT_EQ(game().snapshotFor(teammate).status.health, cs::kSpawnHealth) << "mp_friendlyfire 0";
    EXPECT_EQ(game().snapshotFor(teammate).player.punch, nx::EulerAngles{}) << "nor bleeds";

    std::ignore = cvars().friendlyFire.set(1.0F);
    std::ignore = play(attacker, kReadyTicks);
    std::ignore = press(attacker, {cs::Button::Attack});
    EXPECT_EQ(game().snapshotFor(teammate).status.health, nx::HitPoints{79.0F}) << "60 times 0.35, 21";
    // Two stabs in its back, 68 each, kill it: the killer pays 3300, of its 800.
    for (int stab = 0; stab < 2; ++stab) {
        std::ignore = play(attacker, kReadyTicks);
        std::ignore = press(attacker, {cs::Button::Attack2});
    }
    EXPECT_FALSE(game().snapshotFor(teammate).status.isAlive());
    EXPECT_EQ(game().snapshotFor(attacker).status.money, nx::Money{}) << "none below none";
}

TEST_F(GameTest, TheConsoleGivesWeaponsAndNoclipAndAToolPlacesTheEye) {
    const cs::PlayerIndex player = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
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

TEST_F(GameTest, APlayerLeavingFreesItsSlotAndIsNoLongerCountedNorShown) {
    const cs::PlayerIndex first =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex second = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex third = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    game().leave(second);
    EXPECT_EQ(game().playerCount(), 2U);
    EXPECT_EQ(game().teamCounts()[cs::Team::Terrorists].players(), 1U);
    EXPECT_EQ(game().snapshotFor(first).players.size(), 1U);
    EXPECT_EQ(game().snapshotFor(first).players[0].player, third);
    EXPECT_EQ(game().perceptionTargets().size(), 2U);
    idle(1);
    EXPECT_EQ(game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player"), second)
        << "its slot";
    EXPECT_EQ(game().ownStateOf(second).team, cs::Team::CounterTerrorists);
}

TEST_F(GameTest, TheDefaultTeamIsTheOneOfFewerPlayers) {
    std::ignore = game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    EXPECT_EQ(game().defaultTeam(), cs::Team::Terrorists);
    std::ignore = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    std::ignore = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    EXPECT_EQ(game().defaultTeam(), cs::Team::CounterTerrorists);
}

TEST_F(GameTest, ASpotIsSeenThroughTheBodiesButNotThroughTheWorld) {
    const cs::PlayerIndex watcher =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex between = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    standOff(watcher, between, "knife");
    const nx::Vec3 eye = game().ownStateOf(watcher).eye;
    const nx::Vec3 beyond = eye + nx::Vec3{.x = 2.0F * kVictimAhead};
    const cs::PerceptionTarget body = game().perceptionTargets()[1];
    ASSERT_EQ(body.player, between);
    EXPECT_TRUE(game().seesSpot(eye, beyond)) << "the body in between leaves the line be";
    EXPECT_FALSE(game().seesSpot(eye, beyond - nx::Vec3{.z = 2.0F * kStandingEye})) << "under the floor";
    EXPECT_TRUE(body.feet.z < body.origin.z && !body.isDucked);
    EXPECT_EQ(game().ownStateOf(watcher).feet,
              game().ownStateOf(watcher).eye -
                  nx::Vec3{.z = cs::kStandingViewHeight.value() - cs::kStandingBox.min.z});
}

TEST_F(GameTest, EveryClientReceivesTheGamesWorldAndSwitchableLightPatterns) {
    constexpr std::size_t kSwitchableStyle = 32;
    const cs::PlayerIndex first =
        game().join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const cs::PlayerIndex second = game().join(cs::Team::Terrorists, cs::AutoSwitch::ToHeavier, "Player");
    cs::PlayerSnapshot snapshot = game().snapshotFor(first);
    EXPECT_EQ(snapshot.lightStyles.patterns[0], "m");
    EXPECT_EQ(snapshot.lightStyles.patterns[4], "mamamamamama");
    EXPECT_EQ(snapshot.lightStyles.patterns[32], "a");
    EXPECT_EQ(snapshot.lightStyles.patterns[33], "za");
    EXPECT_TRUE(snapshot.lightStyles.patterns[34].empty());
    EXPECT_EQ(game().snapshotFor(second).lightStyles, snapshot.lightStyles);
    snapshot.lightStyles.patterns[kSwitchableStyle] = "z";
    idle(1);
    EXPECT_EQ(game().snapshotFor(first).lightStyles.patterns[32], "a")
        << "a snapshot owns its copy and cannot mutate the game";
}

} // namespace
