// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The kill feed: which pictures a row shows, where its parts go, the line the console shows of a death,
// hud_deathnotice_time, and the rows showing, at most so many, gone once they showed for its time.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;
import nx.assets;
import nx.rhi;
import nx.render;
import nx.ui;
import cs.shared;
import cs.client;

#include "consoleFixtures.hpp"
#include "squareFont.hpp"

namespace {

constexpr std::string_view kList = R"(6
d_knife    640 640hud1  192 0   48 16
d_knife    320 320hud1  10  10  10 10
d_usp      640 640hud1  192 32  32 16
d_sg550    640 640hud16 192 48  48 16
d_skull    640 640hud1  224 240 32 16
d_headshot 640 640hud1  0   240 36 16
)";

/// The entries of `list`.
[[nodiscard]] auto entriesOf(std::string_view list) -> std::vector<nx::assets::SpriteListEntry> {
    auto entries = nx::assets::parseSpriteList(list);
    EXPECT_TRUE(entries.has_value());
    return std::move(entries).value_or(std::vector<nx::assets::SpriteListEntry>{});
}

/// The pictures of kList, none found failing the test.
[[nodiscard]] auto picturesOf(cs::HudSprites& sprites) -> cs::KillPictures {
    auto pictures = cs::KillPictures::of(entriesOf(kList), sprites);
    EXPECT_TRUE(pictures.has_value());
    return std::move(pictures).value();
}

/// A death of `victim`, a terrorist, killed by the counter-terrorist `killer` with no weapon.
[[nodiscard]] auto killOf(std::string_view victim, std::string_view killer = "Killer") -> cs::KillNotice {
    return {.killer = cs::NoticedPlayer{.name = std::string{killer}, .team = cs::Team::CounterTerrorists},
            .victim = {.name = std::string{victim}, .team = cs::Team::Terrorists}};
}

TEST(KillPicturesTest, AKillShowsItsWeaponsPictureForScreens640Wide) {
    cs::HudSprites sprites;
    const cs::KillPictures pictures = picturesOf(sprites);
    EXPECT_EQ(pictures.icon("knife").rect,
              (nx::assets::SpriteRect{.x = 192, .y = 0, .width = 48, .height = 16}));
    EXPECT_EQ(pictures.icon("usp").rect,
              (nx::assets::SpriteRect{.x = 192, .y = 32, .width = 32, .height = 16}));
    EXPECT_EQ(pictures.head().rect, (nx::assets::SpriteRect{.x = 0, .y = 240, .width = 36, .height = 16}));
    EXPECT_NE(pictures.icon("sg550").image, pictures.icon("knife").image) << "its sprite another";
    EXPECT_EQ(pictures.icon("usp").image, pictures.icon("knife").image) << "each sprite once";
}

TEST(KillPicturesTest, TheSkullShowsForNoWeaponAndForOneOfNoPicture) {
    cs::HudSprites sprites;
    const cs::KillPictures pictures = picturesOf(sprites);
    const nx::assets::SpriteRect skull{.x = 224, .y = 240, .width = 32, .height = 16};
    EXPECT_EQ(pictures.icon(std::nullopt).rect, skull);
    EXPECT_EQ(pictures.icon("drakar").rect, skull) << "a weapon made as the game runs";
}

TEST(KillPicturesTest, AListWithoutTheSkullOrTheHeadIsRefused) {
    cs::HudSprites sprites;
    const auto noSkull = cs::KillPictures::of(entriesOf("1\nd_headshot 640 640hud1 0 240 36 16\n"), sprites);
    ASSERT_FALSE(noSkull.has_value());
    EXPECT_EQ(noSkull.error().code, cs::HudError::PictureMissing);
    const auto noHead = cs::KillPictures::of(entriesOf("1\nd_skull 640 640hud1 224 240 32 16\n"), sprites);
    ASSERT_FALSE(noHead.has_value());
    EXPECT_EQ(noHead.error().code, cs::HudError::PictureMissing);
}

TEST(KillFeedLayoutTest, ARowEndsAtTheRightEdgeItsKillerBeforeItsPicturesThenItsVictim) {
    const cs::KillRowLayout layout = cs::layoutKillRow({.killer = 60, .icon = 48, .head = 36, .victim = 50},
                                                       0, {.width = 1280, .height = 720});
    EXPECT_EQ(layout.victimX, 1230);
    EXPECT_EQ(layout.headX, 1194);
    EXPECT_EQ(layout.iconX, 1146);
    EXPECT_EQ(layout.killerX, 1081) << "5 before the picture";
    EXPECT_EQ(layout.y, 50) << "a fifteenth of 720 and 2";
}

TEST(KillFeedLayoutTest, ARowOfNoKillerNorHeadShowsItsPictureAndItsVictim) {
    const cs::KillRowLayout layout =
        cs::layoutKillRow({.icon = 32, .victim = 50}, 0, {.width = 640, .height = 480});
    EXPECT_EQ(layout.killerX, std::nullopt);
    EXPECT_EQ(layout.headX, std::nullopt);
    EXPECT_EQ(layout.iconX, 558);
    EXPECT_EQ(layout.victimX, 590);
    EXPECT_EQ(layout.y, 34);
}

TEST(KillFeedLayoutTest, EachRowIs20PixelsUnderTheOneBefore) {
    const nx::rhi::Extent2d size{.width = 1920, .height = 1080};
    const cs::KillRowWidths widths{.icon = 32, .victim = 50};
    EXPECT_EQ(cs::layoutKillRow(widths, 0, size).y, 74);
    EXPECT_EQ(cs::layoutKillRow(widths, 3, size).y, 134);
}

TEST(KillLineTest, TheConsoleTellsWhoKilledWhomWithWhat) {
    EXPECT_EQ(cs::killLineOf(killOf("Victim"), "ak47"), "Killer killed Victim with ak47");
    EXPECT_EQ(cs::killLineOf(killOf("Victim"), std::nullopt), "Killer killed Victim");
}

TEST(KillLineTest, ATeammatesKillIsToldAsSoNoWeaponNamed) {
    cs::KillNotice kill = killOf("Victim");
    kill.victim.team = cs::Team::CounterTerrorists;
    EXPECT_EQ(cs::killLineOf(kill, "ak47"), "Killer killed his teammate Victim");
}

TEST(KillLineTest, ADeathOfNoKillerIsADeath) {
    cs::KillNotice fall = killOf("Victim");
    fall.killer.reset();
    EXPECT_EQ(cs::killLineOf(fall, std::nullopt), "Victim died");
}

TEST(KillFeedCvarsTest, ARowShowsForSixSecondsUntilTheCvarSaysOtherwise) {
    nx::test::RecordingOutput output;
    nx::test::MemoryFiles files;
    nx::console::Console console{output, files};
    const auto cvars = cs::KillFeedCvars::registerIn(console);
    ASSERT_TRUE(cvars.has_value());
    EXPECT_EQ(cvars->shownFor(), nx::Seconds{6.0F});
    EXPECT_FALSE(cvars->rowTime.flags().test(nx::console::CvarFlag::Archive));
    ASSERT_TRUE(cvars->rowTime.set(2.5F).has_value());
    EXPECT_EQ(cvars->shownFor(), nx::Seconds{2.5F});
    ASSERT_TRUE(cvars->rowTime.set(-1.0F).has_value());
    EXPECT_EQ(cvars->shownFor(), nx::Seconds{}) << "none below none";
}

class KillFeedTest : public ::testing::Test {
protected:
    KillFeedTest() {
        auto font =
            nx::ui::Font::open(std::make_shared<const std::vector<std::byte>>(nx::test::SquareFont::build()),
                               {.cellHeight = 20});
        EXPECT_TRUE(font.has_value());
        auto atlas = nx::ui::GlyphAtlas::build(*font, U"AB");
        EXPECT_TRUE(atlas.has_value());
        feed.emplace(std::move(atlas).value());
        auto weapons = cs::WeaponCatalog::make({}, {});
        EXPECT_TRUE(weapons.has_value());
        catalog.emplace(std::move(weapons).value());
    }

    /// Lets the rows that showed for `rowTime` go at `now`.
    void appendAt(nx::Seconds now, nx::Seconds rowTime = cs::kKillRowTime) {
        std::vector<nx::render::ScreenQuad> quads;
        feed->append(quads, {.width = 640, .height = 480}, now, rowTime);
        EXPECT_TRUE(quads.empty()) << "nothing before it is loaded";
    }

    std::optional<cs::WeaponCatalog> catalog;
    std::optional<cs::KillFeed> feed;
};

TEST_F(KillFeedTest, ARowGoesOnceItShowedForItsTime) {
    feed->show(std::array{killOf("A")}, *catalog, nx::Seconds{1.0F});
    feed->show(std::array{killOf("B")}, *catalog, nx::Seconds{3.0F});
    EXPECT_EQ(feed->size(), 2U);
    appendAt(nx::Seconds{7.0F});
    EXPECT_EQ(feed->size(), 2U) << "the first for exactly its six seconds";
    appendAt(nx::Seconds{7.5F});
    EXPECT_EQ(feed->size(), 1U);
    appendAt(nx::Seconds{9.5F});
    EXPECT_EQ(feed->size(), 0U);
}

TEST_F(KillFeedTest, AtMostFourRowsShowTheOldestGoingForANewOne) {
    const std::array kills{killOf("A"), killOf("B"), killOf("A"), killOf("B"), killOf("A"), killOf("B")};
    feed->show(kills, *catalog, nx::Seconds{0.0F});
    EXPECT_EQ(feed->size(), cs::kMostKillRows);
}

TEST_F(KillFeedTest, AMapsEndForgetsItsRows) {
    feed->show(std::array{killOf("A")}, *catalog, nx::Seconds{0.0F});
    feed->clear();
    EXPECT_EQ(feed->size(), 0U);
}

} // namespace
