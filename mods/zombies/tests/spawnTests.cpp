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
import mod.zombies;
#include "boxMap.hpp"
#include "consoleFixtures.hpp"
#include "entityFixtures.hpp"
#include "playerModel.hpp"

namespace {
constexpr float kHalf = 0.5F;

auto floorMap(nx::HammerUnits width, nx::HammerUnits depth) -> std::vector<std::byte> {
    return nx::test::stretchedBoxMapWith(R"({ "classname" "worldspawn" })", width.value(), depth.value());
}

class HordeSpawnTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::filesystem::path data{NX_DATA_DIRECTORY};
        auto characterText = nx::platform::readTextFile(data / "cstrike" / "characters.toml");
        ASSERT_TRUE(characterText.has_value());
        auto characters = cs::readCharacters(*characterText, "characters.toml");
        ASSERT_TRUE(characters.has_value());
        const auto playerModel =
            nx::test::playerModelOf(nx::test::playerSequences()) |
            std::views::transform([](std::byte value) { return static_cast<char>(value); }) |
            std::ranges::to<std::string>();
        for (const cs::Character& character : characters->characters) {
            installation.write(cs::modelOf(character), playerModel);
        }
        nx::test::writeEntityModel(installation);
        auto vfs = nx::assets::Vfs::create(std::array{installation.path()});
        ASSERT_TRUE(vfs.has_value());
        auto read = cs::readGameContent(data, std::move(*vfs));
        ASSERT_TRUE(read.has_value()) << (read ? "" : nx::describe(read.error()));
        content.emplace(std::move(*read));
        auto settingsText = nx::platform::readTextFile(data / "mods" / "zombies.toml");
        ASSERT_TRUE(settingsText.has_value());
        auto parsed = zombies::readSettings(*settingsText, "zombies.toml");
        ASSERT_TRUE(parsed.has_value());
        settings = std::move(*parsed);
        settings.body = nx::test::entityDefinition();
        auto added = content->entities.add(content->vfs, settings.body, cs::ModelTextures::Skipped);
        ASSERT_TRUE(added.has_value());
        type = *added;
        auto registered = cs::GameCvars::registerIn(console);
        ASSERT_TRUE(registered.has_value());
        cvars.emplace(std::move(*registered));
        auto createdJobs = nx::jobs::JobSystem::create({.workerCount = workers});
        ASSERT_TRUE(createdJobs.has_value());
        jobs = std::move(*createdJobs);
    }

    /// How many ticks each zombie's turns are apart.
    [[nodiscard]] auto ticksPerThink() const -> std::uint32_t {
        return static_cast<std::uint32_t>(
            std::lround(settings.thinkInterval / nx::tickLengthOf(nx::kDefaultTickRate)));
    }

    void start(nx::HammerUnits width, nx::HammerUnits depth) {
        auto readMap = cs::GameMap::read(floorMap(width, depth), "spawnTest");
        ASSERT_TRUE(readMap.has_value()) << (readMap ? "" : nx::describe(readMap.error()));
        map = std::move(*readMap);
        if (!content || !cvars) {
            FAIL() << "SetUp must prepare content and cvars before starting a map";
        }
        auto started = cs::Game::start(*content, *map, *cvars);
        ASSERT_TRUE(started.has_value());
        game = std::move(*started);
        const auto file = std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "navigation.toml";
        auto text = nx::platform::readTextFile(file);
        ASSERT_TRUE(text.has_value());
        auto navigation = nx::game::readNavigationData(*text, "navigation.toml");
        ASSERT_TRUE(navigation.has_value());
        auto profile = cs::agentProfile(*navigation);
        ASSERT_TRUE(profile.has_value()) << (profile ? "" : nx::describe(profile.error()));
        auto built = nx::navigation::NavigationMesh::build(*jobs, game->navigationGeometry(), *profile,
                                                           navigation->build);
        ASSERT_TRUE(built.has_value()) << (built ? "" : nx::describe(built.error()));
        mesh.emplace(std::move(*built));
        auto created = zombies::Horde::create(*game, *mesh, *jobs, type, settings);
        ASSERT_TRUE(created.has_value()) << (created ? "" : nx::describe(created.error()));
        horde = std::move(*created);
        anchor = {.x = width.value() * kHalf,
                  .y = depth.value() * kHalf,
                  .z = nx::test::kBoxSide + cs::kStandingBox.max.z};
    }

    nx::test::TemporaryDirectory installation;
    nx::test::RecordingOutput output;
    nx::test::MemoryFiles files;
    nx::console::Console console{output, files};
    std::optional<cs::GameContent> content;
    std::optional<cs::GameCvars> cvars;
    std::unique_ptr<cs::GameMap> map;
    std::unique_ptr<cs::Game> game;
    std::uint32_t workers = 0;
    std::unique_ptr<nx::jobs::JobSystem> jobs;
    zombies::Settings settings{};
    cs::EntityTypeIndex type{};
    nx::Vec3 anchor{};
    std::optional<nx::navigation::NavigationMesh> mesh; ///< The map's, which the horde walks.
    std::unique_ptr<zombies::Horde> horde;              ///< Before its mesh goes.
};

TEST_F(HordeSpawnTest, SixtyFourBodiesFitAReachableBaseThatCannotHoldAStraightRow) {
    constexpr nx::HammerUnits kWidth{512};
    constexpr nx::HammerUnits kDepth{4096};
    constexpr std::uint32_t kPopulation = 64;
    constexpr nx::Degrees kForwardYaw{90};
    settings.spawnRadius = kDepth;
    ASSERT_NO_FATAL_FAILURE(start(kWidth, kDepth));
    auto spawned =
        horde->spawn(std::array{cs::SpawnPoint{.origin = anchor, .yaw = kForwardYaw}}, kPopulation);
    ASSERT_TRUE(spawned.has_value()) << (spawned ? "" : nx::describe(spawned.error()));
    EXPECT_EQ(horde->size(), kPopulation);
    ASSERT_EQ(game->entities().size(), kPopulation);
    ASSERT_TRUE(
        horde->spawn(std::array{cs::SpawnPoint{.origin = anchor, .yaw = kForwardYaw}}, 1).has_value());
    const auto bodies = game->entities();
    ASSERT_EQ(bodies.size(), kPopulation + 1);
    for (std::size_t first = 0; first < bodies.size(); ++first) {
        const double fromCallerX = double{bodies[first].origin.x} - double{anchor.x};
        const double fromCallerY = double{bodies[first].origin.y} - double{anchor.y};
        EXPECT_LE(std::hypot(fromCallerX, fromCallerY), double{settings.spawnRadius.value()});
        for (std::size_t second = first + 1; second < bodies.size(); ++second) {
            const double x = double{bodies[first].origin.x} - double{bodies[second].origin.x};
            const double y = double{bodies[first].origin.y} - double{bodies[second].origin.y};
            EXPECT_GE(std::hypot(x, y), double{settings.spawnSpacing.value()});
        }
    }
}

TEST_F(HordeSpawnTest, InsufficientSpaceRollsBackEveryBodyOfTheBatch) {
    constexpr nx::HammerUnits kAvailableSide{128};
    settings.spawnRadius = kAvailableSide;
    settings.spawnSpacing = kAvailableSide / kHalf;
    ASSERT_NO_FATAL_FAILURE(start(kAvailableSide, kAvailableSide));
    ASSERT_TRUE(horde->spawn(std::array{cs::SpawnPoint{.origin = anchor}}, 1).has_value());
    ASSERT_TRUE(horde->clear().has_value());
    ASSERT_TRUE(game->entities().empty());
    const auto failed = horde->spawn(std::array{cs::SpawnPoint{.origin = anchor}}, 2);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, zombies::ZombieError::SpawnFailed);
    EXPECT_TRUE(game->entities().empty());
    EXPECT_EQ(horde->size(), 0U);
}

TEST_F(HordeSpawnTest, AFullSmallBaseDoesNotSpillIntoTheRestOfTheMapOrRemoveExistingBodies) {
    constexpr nx::HammerUnits kWidth{512};
    constexpr nx::HammerUnits kDepth{4096};
    constexpr nx::HammerUnits kBaseRadius{128};
    settings.spawnRadius = kBaseRadius;
    ASSERT_NO_FATAL_FAILURE(start(kWidth, kDepth));
    const std::array base{cs::SpawnPoint{.origin = anchor}};
    ASSERT_TRUE(horde->spawn(base, 1).has_value());
    const auto existing = game->entities().front().entity;
    const auto failed = horde->spawn(base, settings.spawnBatchLimit);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, zombies::ZombieError::SpawnFailed);
    const auto remaining = game->entities();
    ASSERT_EQ(remaining.size(), 1U);
    EXPECT_EQ(remaining.front().entity, existing);
    EXPECT_EQ(horde->size(), 1U);
}

TEST_F(HordeSpawnTest, ZombiesMakingWayForEachOtherNeverTurnTheirBacksOnTheWayTheyGo) {
    // Their bodies slow down by friction, and are driven against the way they go to slow down sooner, as they
    // make way for each other: each faces the way it goes all the same, rather than turn its back on it.
    constexpr nx::HammerUnits kWidth{512};
    constexpr nx::HammerUnits kDepth{2048};
    constexpr float kWayLength = 1536.0F;
    constexpr std::uint32_t kPopulation = 12;
    constexpr nx::Degrees kForwardYaw{90};
    constexpr std::uint32_t kTicks = 600;
    constexpr float kWalking = 0.25F; ///< Of its full speed: it walks, at least.
    settings.spawnRadius = nx::HammerUnits{kWidth.value() * kHalf};
    ASSERT_NO_FATAL_FAILURE(start(kWidth, kDepth));
    const nx::Vec3 from = anchor - nx::Vec3{.y = kWayLength * kHalf};
    ASSERT_TRUE(horde->spawn(std::array{cs::SpawnPoint{.origin = from, .yaw = kForwardYaw}}, kPopulation)
                    .has_value());
    const nx::Vec3 goal = from + nx::Vec3{.y = kWayLength, .z = cs::kStandingBox.min.z};
    for (const cs::EntityState& body : game->entities()) {
        nx::agents::Agent* const agent = game->entityComponent<nx::agents::Agent>(body.entity);
        ASSERT_NE(agent, nullptr);
        agent->brain.setGoal(goal);
    }
    std::uint32_t turnedBack = 0;
    for (std::uint32_t tick = 0; tick < kTicks; ++tick) {
        const std::span<const cs::EntityCommand> commands = horde->commands();
        const cs::EntityStates bodies = game->entities();
        for (const cs::EntityCommand& command : commands) {
            const auto body = std::ranges::find(bodies, command.entity, &cs::EntityState::entity);
            ASSERT_NE(body, bodies.end());
            const nx::Vec3 velocity{.x = body->velocity.x, .y = body->velocity.y};
            const float speed = nx::length(velocity);
            if (speed > kWalking * settings.body.speed.value() &&
                nx::dot(nx::angleVectors(command.command.viewAngles).forward, velocity / speed) < 0.0F) {
                ++turnedBack;
            }
        }
        game->tick({}, commands);
    }
    EXPECT_EQ(turnedBack, 0U) << "ticks a zombie walking faced against the way it went";
}

TEST_F(HordeSpawnTest, ZombiesInAFileSeeThePlayerAheadPastThoseBeforeThem) {
    // A file of zombies down a strip, the player ahead at its end: each sees it past the bodies before it,
    // as a bot sees, rather than their bodies across its sight.
    constexpr nx::HammerUnits kWidth{128};
    constexpr nx::HammerUnits kDepth{2048};
    constexpr float kWayLength = 1536.0F;
    constexpr std::uint32_t kPopulation = 6;
    constexpr nx::Degrees kForwardYaw{90};
    settings.spawnRadius = nx::HammerUnits{kWidth.value() * 2.0F};
    ASSERT_NO_FATAL_FAILURE(start(kWidth, kDepth));
    const nx::Vec3 from = anchor - nx::Vec3{.y = kWayLength * kHalf};
    ASSERT_TRUE(horde->spawn(std::array{cs::SpawnPoint{.origin = from, .yaw = kForwardYaw}}, kPopulation)
                    .has_value());
    const cs::PlayerIndex player =
        game->join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    for (std::uint32_t tick = 0; tick < ticksPerThink(); ++tick) {
        game->placeEye(player, from + nx::Vec3{.y = kWayLength, .z = cs::kStandingViewHeight.value()});
        game->tick({}, horde->commands());
    }
    for (const cs::EntityState& body : game->entities()) {
        const nx::agents::Agent* const agent = game->entityComponent<nx::agents::Agent>(body.entity);
        ASSERT_NE(agent, nullptr);
        EXPECT_EQ(agent->behavior, nx::agents::Behavior::Chase) << body.origin.y;
    }
}

TEST_F(HordeSpawnTest, EachZombieThinksAtATickOfItsOwnOnceAThinkInterval) {
    constexpr nx::HammerUnits kSide{2048};
    constexpr float kPlayerAway = 800.0F;
    constexpr std::uint32_t kPopulation = 40;
    settings.spawnRadius = kSide;
    settings.fieldOfView = nx::Degrees{360.0F}; // the player seen whichever way each faces
    ASSERT_NO_FATAL_FAILURE(start(kSide, kSide));
    ASSERT_TRUE(horde->spawn(std::array{cs::SpawnPoint{.origin = anchor}}, kPopulation).has_value());
    const cs::PlayerIndex player =
        game->join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const auto chasing = [this] {
        std::uint32_t count = 0;
        for (const cs::EntityState& body : game->entities()) {
            const nx::agents::Agent* const agent = game->entityComponent<nx::agents::Agent>(body.entity);
            count += agent->behavior == nx::agents::Behavior::Chase ? 1U : 0U;
        }
        return count;
    };
    std::uint32_t before = 0;
    for (std::uint32_t tick = 0; tick < ticksPerThink(); ++tick) {
        game->placeEye(player, anchor + nx::Vec3{.y = kPlayerAway, .z = cs::kStandingViewHeight.value()});
        game->tick({}, horde->commands());
        const std::uint32_t now = chasing();
        EXPECT_LT(now - before, kPopulation) << "not all at one tick";
        before = now;
    }
    EXPECT_EQ(before, kPopulation) << "all within the interval";
}

TEST_F(HordeSpawnTest, AZombieWhoseTurnItIsNotKeepsWalkingAsItDecided) {
    constexpr nx::HammerUnits kSide{2048};
    constexpr float kPlayerAway = 800.0F;
    constexpr std::uint32_t kPopulation = 8;
    settings.spawnRadius = kSide;
    settings.fieldOfView = nx::Degrees{360.0F}; // the player seen whichever way each faces
    ASSERT_NO_FATAL_FAILURE(start(kSide, kSide));
    ASSERT_TRUE(horde->spawn(std::array{cs::SpawnPoint{.origin = anchor}}, kPopulation).has_value());
    const cs::PlayerIndex player =
        game->join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    for (std::uint32_t tick = 0; tick < 2 * ticksPerThink(); ++tick) {
        game->placeEye(player, anchor + nx::Vec3{.y = kPlayerAway, .z = cs::kStandingViewHeight.value()});
        game->tick({}, horde->commands());
    }
    for (std::uint32_t tick = 0; tick < ticksPerThink(); ++tick) {
        game->placeEye(player, anchor + nx::Vec3{.y = kPlayerAway, .z = cs::kStandingViewHeight.value()});
        const std::span<const cs::EntityCommand> asked = horde->commands();
        ASSERT_EQ(asked.size(), kPopulation) << "at every tick, each";
        for (const cs::EntityCommand& command : asked) {
            EXPECT_GT(std::hypot(command.command.move.forward.value(), command.command.move.side.value()),
                      0.0F);
        }
        game->tick({}, asked);
    }
}

TEST_F(HordeSpawnTest, AZombieFarFromEveryPlayerThinksAtFewerOfItsTurns) {
    constexpr nx::HammerUnits kSide{2048};
    constexpr float kPlayerAway = 20000.0F;
    constexpr std::uint32_t kPopulation = 8;
    constexpr std::uint32_t kFarthestTurnsPerThink = 8;
    settings.spawnRadius = kSide;
    settings.fieldOfView = nx::Degrees{360.0F};
    settings.sightRange = nx::HammerUnits{2.0F * kPlayerAway};
    ASSERT_NO_FATAL_FAILURE(start(kSide, kSide));
    ASSERT_TRUE(horde->spawn(std::array{cs::SpawnPoint{.origin = anchor}}, kPopulation).has_value());
    const cs::PlayerIndex player =
        game->join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    const auto idle = [this] {
        std::uint32_t count = 0;
        for (const cs::EntityState& body : game->entities()) {
            count +=
                game->entityComponent<nx::agents::Agent>(body.entity)->behavior == nx::agents::Behavior::Idle
                    ? 1U
                    : 0U;
        }
        return count;
    };
    const auto after = [&](std::uint32_t ticks) {
        for (std::uint32_t tick = 0; tick < ticks; ++tick) {
            game->placeEye(player, anchor + nx::Vec3{.y = kPlayerAway, .z = cs::kStandingViewHeight.value()});
            game->tick({}, horde->commands());
        }
    };
    after(ticksPerThink());
    EXPECT_EQ(idle(), kPopulation) << "none has thought yet, at one of its turns";
    after((kFarthestTurnsPerThink - 1) * ticksPerThink());
    EXPECT_LT(idle(), kPopulation) << "it has by its eighth";
}

TEST_F(HordeSpawnTest, ANoiseOfOneTickIsHeardByEachZombieAtItsTurn) {
    // Zombies that cannot see it: only hearing tells them of the player, and a noise lasts one tick.
    constexpr nx::HammerUnits kSide{2048};
    constexpr std::uint32_t kPopulation = 16;
    constexpr std::uint32_t kMostTicksToAShot = 4096;
    settings.spawnRadius = kSide;
    settings.sightRange = nx::HammerUnits{1.0F};
    ASSERT_NO_FATAL_FAILURE(start(kSide, kSide));
    ASSERT_TRUE(horde->spawn(std::array{cs::SpawnPoint{.origin = anchor}}, kPopulation).has_value());
    const cs::PlayerIndex player =
        game->join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
    cs::CommandNumber number{};
    for (std::uint32_t tick = 0; tick < kMostTicksToAShot && game->noises().empty(); ++tick) {
        number = cs::CommandNumber{number.value + 1};
        cs::UserCommand command{.number = number, .duration = nx::tickLengthOf(game->time().rate)};
        command.buttons.set(cs::Button::Attack);
        game->tick(std::array{cs::PlayerCommand{.player = player, .command = command}});
    }
    ASSERT_FALSE(game->noises().empty()) << "the shot's";
    for (std::uint32_t tick = 0; tick < ticksPerThink(); ++tick) {
        game->tick({}, horde->commands());
    }
    for (const cs::EntityState& body : game->entities()) {
        const nx::agents::Agent* const agent = game->entityComponent<nx::agents::Agent>(body.entity);
        ASSERT_NE(agent, nullptr);
        EXPECT_EQ(agent->behavior, nx::agents::Behavior::Investigate) << "it went by the noise, at its turn";
    }
}

/// A horde of HordeSpawnTest whose bodies `workerCount` workers share out.
class HordeAtWork final : public HordeSpawnTest {
public:
    explicit HordeAtWork(std::uint32_t workerCount) {
        workers = workerCount;
        SetUp();
    }

    /// Where `count` bodies chasing a player stand after `ticks`.
    [[nodiscard]] auto placesAfter(std::uint32_t count, std::uint32_t ticks) -> std::vector<nx::Vec3> {
        constexpr nx::HammerUnits kSide{2048};
        constexpr float kPlayerAway = 800.0F;
        settings.spawnRadius = kSide;
        start(kSide, kSide);
        EXPECT_TRUE(horde->spawn(std::array{cs::SpawnPoint{.origin = anchor}}, count).has_value());
        const cs::PlayerIndex player =
            game->join(cs::Team::CounterTerrorists, cs::AutoSwitch::ToHeavier, "Player");
        game->placeEye(player, anchor + nx::Vec3{.y = kPlayerAway, .z = cs::kStandingViewHeight.value()});
        for (std::uint32_t tick = 0; tick < ticks; ++tick) {
            game->tick({}, horde->commands(), jobs.get());
        }
        return game->entities() | std::views::transform(&cs::EntityState::origin) |
               std::ranges::to<std::vector>();
    }

private:
    void TestBody() override {}
};

TEST(HordeWorkersTest, AHordeMovesTheSameWithAnyNumberOfWorkers) {
    // Batches of what it perceives, decides and moves.
    constexpr std::uint32_t kBodies = 64;
    constexpr std::uint32_t kTicks = 300;
    const std::vector<nx::Vec3> played = HordeAtWork{0}.placesAfter(kBodies, kTicks);
    EXPECT_EQ(played.size(), kBodies);
    EXPECT_EQ(HordeAtWork{3}.placesAfter(kBodies, kTicks), played);
}

TEST_F(HordeSpawnTest, AMissingBaseIsRejectedBeforeCreatingABody) {
    constexpr nx::HammerUnits kSide{512};
    ASSERT_NO_FATAL_FAILURE(start(kSide, kSide));
    const auto failed = horde->spawn({}, 1);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, zombies::ZombieError::SpawnFailed);
    EXPECT_TRUE(game->entities().empty());
    EXPECT_EQ(horde->size(), 0U);
}
} // namespace
