// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The game as its player's client knows it: the ticks a frame's time brings due, a quarter of a second
// at most but for a tool's frame, the commands numbered one after the other, the eye between the last two
// ticks but where the game placed it, what the ticks made kept until the frame takes it, and the brush
// entities drawn where the server says.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.assets;
import nx.render;
import cs.shared;
import cs.client;

#include "boxMap.hpp"
#include "temporaryDirectory.hpp"

namespace {

constexpr float kTolerance = 1e-4F;
constexpr nx::Seconds kTick = nx::tickLengthOf(nx::kDefaultTickRate);

/// A client's game of the box, of no studio models, told first of a player standing at `origin`.
class ClientGameTest : public ::testing::Test {
protected:
    ClientGameTest() {
        auto vfs = nx::assets::Vfs::create(std::array{m_installation.path()});
        EXPECT_TRUE(vfs.has_value());
        auto content = cs::readGameContent(NX_DATA_DIRECTORY, std::move(vfs).value());
        EXPECT_TRUE(content.has_value()) << (content ? "" : nx::describe(content.error()));
        m_content.emplace(std::move(content).value());
        auto map = nx::assets::BspView::parse(m_mapBytes);
        EXPECT_TRUE(map.has_value());
        m_map.emplace(std::move(map).value());
        m_models.emplace(cs::MapModels::read(m_content->vfs, *m_map));
    }

    /// What the server tells after the tick that ended at `tick`, of a player standing at `origin`.
    [[nodiscard]] auto snapshotAt(std::int64_t tick, nx::Vec3 origin) const -> cs::PlayerSnapshot {
        return cs::PlayerSnapshot{.time = nx::GameTime::atTick(tick),
                                  .player = cs::MovementState{.origin = origin},
                                  .inventory = cs::Inventory{m_content->weapons}};
    }

    [[nodiscard]] auto models() const -> const cs::MapModels& { return *m_models; }

private:
    nx::test::TemporaryDirectory m_installation;
    std::vector<std::byte> m_mapBytes = nx::test::boxMapWith(R"({ "classname" "worldspawn" })");
    std::optional<cs::GameContent> m_content;
    std::optional<nx::assets::BspView> m_map;
    std::optional<cs::MapModels> m_models;
};

/// The eye of a player standing at `origin`.
[[nodiscard]] auto eyeAt(nx::Vec3 origin) -> nx::Vec3 {
    return origin + nx::Vec3{.z = cs::kStandingViewHeight.value()};
}

void expectNear(nx::Vec3 actual, nx::Vec3 expected) {
    EXPECT_NEAR(actual.x, expected.x, kTolerance);
    EXPECT_NEAR(actual.y, expected.y, kTolerance);
    EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

TEST_F(ClientGameTest, AFrameBringsATickDueForEachHundredthOfASecondAQuarterOfASecondAtMost) {
    cs::ClientGame client{models(), snapshotAt(0, {})};
    EXPECT_EQ(client.ticksDue(kTick * 2.5F, cs::CatchUp::Limited), 2U);
    EXPECT_EQ(client.ticksDue(kTick * 0.6F, cs::CatchUp::Limited), 1U) << "what the last frame left over";
    // A stalled frame plays as many as a quarter of a second brings; a tool's frame all of its time.
    cs::ClientGame stalled{models(), snapshotAt(0, {})};
    cs::ClientGame quarter{models(), snapshotAt(0, {})};
    EXPECT_EQ(stalled.ticksDue(nx::Seconds{1.0F}, cs::CatchUp::Limited),
              quarter.ticksDue(nx::Seconds{0.25F}, cs::CatchUp::Whole));
    cs::ClientGame whole{models(), snapshotAt(0, {})};
    EXPECT_EQ(whole.ticksDue(kTick * 100.5F, cs::CatchUp::Whole), 100U);
}

TEST_F(ClientGameTest, ItsCommandsAreNumberedOneAfterTheOther) {
    cs::ClientGame client{models(), snapshotAt(0, {})};
    const nx::EulerAngles view{.yaw = nx::Degrees{45.0F}};
    const cs::UserCommand first = client.standingCommand(view);
    const cs::UserCommand second = client.standingCommand(view);
    EXPECT_EQ(first.number.value, 1U);
    EXPECT_EQ(second.number.value, 2U);
    EXPECT_EQ(first.viewAngles, view);
    EXPECT_EQ(first.move.forward, nx::UnitsPerSecond{}) << "asking nothing";
}

TEST_F(ClientGameTest, TheEyeIsShownBetweenTheLastTwoTicksButWhereTheGamePlacedIt) {
    const nx::Vec3 start{.x = 0.0F};
    const nx::Vec3 moved{.x = 10.0F};
    cs::ClientGame client{models(), snapshotAt(0, start)};
    ASSERT_EQ(client.ticksDue(kTick * 1.5F, cs::CatchUp::Limited), 1U);
    client.receive(snapshotAt(1, moved));
    expectNear(client.eye(), eyeAt({.x = 5.0F}));

    cs::PlayerSnapshot placed = snapshotAt(2, {.x = 100.0F});
    placed.wasPlaced = true;
    ASSERT_EQ(client.ticksDue(kTick, cs::CatchUp::Limited), 1U);
    client.receive(std::move(placed));
    expectNear(client.eye(), eyeAt({.x = 100.0F}));
}

TEST_F(ClientGameTest, WhatTheTicksMadeIsKeptUntilTheFrameTakesIt) {
    cs::ClientGame client{models(), snapshotAt(0, {})};
    const cs::GameSound first{.files = {"first.wav"}};
    const cs::GameSound second{.files = {"second.wav"}};
    cs::PlayerSnapshot tick1 = snapshotAt(1, {});
    tick1.sounds = {cs::SoundMade{.sound = &first, .at = tick1.time}};
    tick1.impacts = {cs::Impact{.position = {.x = 1.0F}}};
    tick1.turnedView = nx::EulerAngles{.yaw = nx::Degrees{90.0F}};
    cs::PlayerSnapshot tick2 = snapshotAt(2, {});
    tick2.sounds = {cs::SoundMade{.sound = &second, .at = tick2.time}};
    tick2.impacts = {cs::Impact{.position = {.x = 2.0F}}};
    client.receive(std::move(tick1));
    client.receive(std::move(tick2));

    const std::vector<cs::SoundMade> sounds = client.takeSounds();
    ASSERT_EQ(sounds.size(), 2U);
    EXPECT_EQ(sounds[0].sound, &first);
    EXPECT_EQ(sounds[1].sound, &second);
    EXPECT_TRUE(client.takeSounds().empty());
    const std::vector<cs::Impact> impacts = client.takeImpacts();
    ASSERT_EQ(impacts.size(), 2U);
    EXPECT_FLOAT_EQ(impacts[1].position.x, 2.0F);
    EXPECT_EQ(client.takeTurnedView(), (nx::EulerAngles{.yaw = nx::Degrees{90.0F}}));
    EXPECT_FALSE(client.takeTurnedView().has_value());
}

TEST_F(ClientGameTest, AFrameShowsTheMomentATickBehindTheGame) {
    cs::ClientGame client{models(), snapshotAt(100, {})};
    const cs::Hearing hearing = client.hearing();
    EXPECT_NEAR(hearing.from.since(nx::GameTime::atTick(99), hearing.rate).count(), 0.0F, kTolerance);
    EXPECT_EQ(hearing.rate, nx::kDefaultTickRate);
}

TEST_F(ClientGameTest, BrushEntitiesAreDrawnWhereTheServerSaysTheyAre) {
    cs::PlayerSnapshot told = snapshotAt(1, {});
    told.brushEntities = {cs::BrushEntityState{.model = nx::assets::ModelIndex{3},
                                               .origin = {.x = 1.0F, .y = 2.0F, .z = 3.0F},
                                               .angles = {.yaw = nx::Degrees{90.0F}},
                                               .waveHeight = nx::HammerUnits{2.0F},
                                               .showsAlternateTextures = true}};
    const cs::ClientGame client{models(), std::move(told)};
    const std::vector<nx::render::ModelPlacement> placements = client.placements();
    ASSERT_EQ(placements.size(), 1U);
    EXPECT_EQ(placements[0].model, nx::assets::ModelIndex{3});
    EXPECT_EQ(placements[0].origin, (nx::Vec3{.x = 1.0F, .y = 2.0F, .z = 3.0F}));
    EXPECT_EQ(placements[0].angles.yaw, nx::Degrees{90.0F});
    EXPECT_EQ(placements[0].waveHeight, nx::HammerUnits{2.0F});
    EXPECT_TRUE(placements[0].showsAlternateTextures);
    EXPECT_TRUE(client.hostages().empty()) << "no hostage told of";
}

TEST_F(ClientGameTest, TheServerSetsTheLightStylesAndAnUpdateClearsRemovedPatterns) {
    cs::PlayerSnapshot first = snapshotAt(0, {});
    first.lightStyles.patterns[4] = "ma";
    first.lightStyles.patterns[32] = "z";
    cs::ClientGame client{models(), std::move(first)};
    EXPECT_EQ(client.lightStylesAt(nx::Seconds{})[4], 264);
    EXPECT_EQ(client.lightStylesAt(nx::Seconds{0.15F})[4], 0);
    EXPECT_EQ(client.lightStylesAt(nx::Seconds{})[32], 550);

    cs::PlayerSnapshot changed = snapshotAt(1, {});
    changed.lightStyles.patterns[4] = "az";
    client.receive(changed);
    EXPECT_EQ(client.lightStylesAt(nx::Seconds{})[4], 0);
    EXPECT_EQ(client.lightStylesAt(nx::Seconds{0.15F})[4], 550);
    EXPECT_EQ(client.lightStylesAt(nx::Seconds{})[32], 256);
    changed.time = nx::GameTime::atTick(2);
    client.receive(std::move(changed));
    EXPECT_EQ(client.lightStylesAt(nx::Seconds{0.15F})[4], 550)
        << "the phase follows the map's time, including when the patterns are unchanged";
}

} // namespace
