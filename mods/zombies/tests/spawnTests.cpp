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
        auto createdJobs = nx::jobs::JobSystem::create({.workerCount = 0});
        ASSERT_TRUE(createdJobs.has_value());
        jobs = std::move(*createdJobs);
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
        auto created = zombies::Horde::create(*jobs, *game, *navigation, type, settings);
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
    std::unique_ptr<nx::jobs::JobSystem> jobs;
    zombies::Settings settings{};
    cs::EntityTypeIndex type{};
    nx::Vec3 anchor{};
    std::unique_ptr<zombies::Horde> horde;
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
