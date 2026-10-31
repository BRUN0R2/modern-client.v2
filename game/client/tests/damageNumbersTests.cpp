// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The damage numbers: how a number shows as it ages, where over the head it is drawn, what it says,
// hud_damagenumbers, and the numbers showing, at most so many, gone once their life is over.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.console;
import nx.ui;
import nx.render;
import cs.shared;
import cs.client;

#include "consoleFixtures.hpp"
#include "squareFont.hpp"
#include "temporaryDirectory.hpp"

namespace {

constexpr float kTolerance = 1e-5F;

/// A look of round times: a life of a second, fading in over a tenth, growing from a quarter of its size
/// to 1.5 over 0.2, settling over the next 0.2, and over its last 0.4 fading out shrinking to half; its
/// digits a hundredth of the screen over the head, rising a tenth of it, up to 0.02 of it to a side.
[[nodiscard]] auto roundLook() -> cs::DamageNumberLook {
    return {.font = {.family = "Square", .weight = 400},
            .height = 0.05F,
            .gap = 0.01F,
            .color = {.red = 255, .green = 255, .blue = 255},
            .headColor = {.red = 255},
            .outline = 0.1F,
            .life = nx::Seconds{1.0F},
            .fadeIn = nx::Seconds{0.1F},
            .growTime = nx::Seconds{0.2F},
            .settleTime = nx::Seconds{0.2F},
            .fadeOut = nx::Seconds{0.4F},
            .startScale = 0.25F,
            .peakScale = 1.5F,
            .endScale = 0.5F,
            .rise = 0.1F,
            .spread = 0.02F,
            .mostNumbers = 2};
}

[[nodiscard]] auto poseAt(float seconds) -> cs::DamageNumberPose {
    const std::optional<cs::DamageNumberPose> pose =
        cs::damageNumberPoseAt(roundLook(), nx::Seconds{seconds});
    EXPECT_TRUE(pose.has_value()) << seconds;
    return pose.value_or(cs::DamageNumberPose{});
}

TEST(DamageNumberTest, ANumberStartsSmallAndClearThenPopsUpPastItsSizeAndSettles) {
    const cs::DamageNumberPose born = poseAt(0.0F);
    EXPECT_NEAR(born.scale, 0.25F, kTolerance);
    EXPECT_NEAR(born.opacity, 0.0F, kTolerance);
    EXPECT_NEAR(born.rise, 0.0F, kTolerance);
    EXPECT_LT(poseAt(0.05F).scale, poseAt(0.1F).scale) << "growing";
    EXPECT_LT(poseAt(0.1F).scale, poseAt(0.15F).scale);
    EXPECT_GT(poseAt(0.05F).opacity, 0.0F) << "fading in";
    const cs::DamageNumberPose peak = poseAt(0.2F);
    EXPECT_NEAR(peak.scale, 1.5F, kTolerance);
    EXPECT_NEAR(peak.opacity, 1.0F, kTolerance);
    EXPECT_GT(poseAt(0.3F).scale, 1.0F) << "settling";
    EXPECT_NEAR(poseAt(0.4F).scale, 1.0F, kTolerance);
    EXPECT_NEAR(poseAt(0.6F).scale, 1.0F, kTolerance) << "whole until it fades out";
    EXPECT_NEAR(poseAt(0.6F).opacity, 1.0F, kTolerance);
}

TEST(DamageNumberTest, ANumberRisesEasingOutAndFadesOutShrinkingUntilItsLifeIsOver) {
    EXPECT_GT(poseAt(0.25F).rise, 0.0F);
    EXPECT_GT(poseAt(0.5F).rise, poseAt(0.25F).rise);
    EXPECT_GT(poseAt(0.25F).rise - poseAt(0.0F).rise, poseAt(0.75F).rise - poseAt(0.5F).rise)
        << "faster at first";
    const cs::DamageNumberPose fading = poseAt(0.8F);
    EXPECT_NEAR(fading.opacity, 0.5F, kTolerance) << "halfway through its fade";
    EXPECT_NEAR(fading.scale, 0.75F, kTolerance) << "halfway to half its size";
    EXPECT_LT(poseAt(0.95F).opacity, fading.opacity);
    EXPECT_FALSE(cs::damageNumberPoseAt(roundLook(), nx::Seconds{1.0F}).has_value());
}

TEST(DamageNumberTest, ANumbersDigitsStandItsGapOverTheHeadItsHitMetAndRiseFromThereToASide) {
    constexpr float kScreenHeight = 1000.0F;
    constexpr float kFootBelowMiddle = 12.0F;
    const cs::DamageNumberLook look = roundLook();
    // A near head and a far one: the same gap over either.
    for (const nx::render::ScreenPoint head : {nx::render::ScreenPoint{.x = 960.0F, .y = 300.0F},
                                               nx::render::ScreenPoint{.x = 400.0F, .y = 520.0F}}) {
        const nx::render::ScreenPoint born =
            cs::damageNumberMiddleOf(look, poseAt(0.0F), 0.0F, head, kScreenHeight, kFootBelowMiddle);
        EXPECT_NEAR(born.x, head.x, kTolerance);
        EXPECT_NEAR(born.y + kFootBelowMiddle, head.y - 10.0F, kTolerance)
            << "its digits' foot 10 pixels over";
        const cs::DamageNumberPose risen = poseAt(0.5F);
        const nx::render::ScreenPoint later =
            cs::damageNumberMiddleOf(look, risen, -1.0F, head, kScreenHeight, kFootBelowMiddle);
        EXPECT_NEAR(later.x, head.x - 20.0F, kTolerance) << "the whole spread to the left";
        EXPECT_NEAR(born.y - later.y, risen.rise * 100.0F, kTolerance) << "its share of the rise up";
    }
}

TEST(DamageNumberTest, ANumberSaysTheWholePointsToTheNearestOneAtLeast) {
    EXPECT_EQ(cs::damageText(nx::Damage{31.6F}), "32");
    EXPECT_EQ(cs::damageText(nx::Damage{128.0F}), "128");
    EXPECT_EQ(cs::damageText(nx::Damage{0.2F}), "1");
}

TEST(DamageNumberTest, HudDamageNumbersIsArchivedAndOnByDefault) {
    nx::test::RecordingOutput output;
    nx::test::MemoryFiles files;
    nx::console::Console console{output, files};
    const auto cvars = cs::DamageNumberCvars::registerIn(console);
    ASSERT_TRUE(cvars.has_value());
    EXPECT_TRUE(cvars->shown.value());
    EXPECT_TRUE(cvars->shown.flags().test(nx::console::CvarFlag::Archive));
}

/// A catalog of the square font, which has no digits: the numbers are timed, not drawn.
class DamageNumbersTest : public testing::Test {
protected:
    DamageNumbersTest() {
        fonts.write("square.ttf", nx::test::SquareFont::build() | std::views::transform([](std::byte value) {
                                      return static_cast<char>(value);
                                  }) | std::ranges::to<std::string>());
        std::ignore = catalog.addFolder(fonts.path());
    }

    nx::test::TemporaryDirectory fonts;
    nx::ui::FontCatalog catalog;
};

TEST_F(DamageNumbersTest, NumbersOfAFamilyTheSystemHasNoneOfCannotShow) {
    cs::DamageNumberLook look = roundLook();
    look.font.family = "Nowhere";
    const auto numbers = cs::DamageNumbers::open(catalog, look, 1080);
    ASSERT_FALSE(numbers.has_value());
    EXPECT_EQ(numbers.error().code, cs::DamageNumbersError::NoFont);
}

TEST_F(DamageNumbersTest, EachHitShowsItsOwnNumberTheOldestGoingFirstAndEachGoneAfterItsLife) {
    const cs::DamageNumberLook look = roundLook();
    auto numbers = cs::DamageNumbers::open(catalog, look, 1080);
    ASSERT_TRUE(numbers.has_value()) << nx::describe(numbers.error());
    // Three pellets of a shot, each a number of its own, of which the look keeps the two newest.
    const std::array pellets{cs::DamageDealt{.damage = nx::Damage{20}},
                             cs::DamageDealt{.damage = nx::Damage{20}},
                             cs::DamageDealt{.damage = nx::Damage{20}, .isHead = true}};
    numbers->show(std::span{pellets}.first(1), nx::Seconds{});
    EXPECT_EQ(numbers->size(), 1U);
    numbers->show(pellets, nx::Seconds{0.5F});
    EXPECT_EQ(numbers->size(), 2U) << "at most the look's most";
    std::vector<nx::render::ScreenQuad> quads;
    const nx::Mat4 view = nx::render::viewProjectionMatrix({}, 16.0F / 9.0F);
    numbers->append(quads, view, {.width = 1920, .height = 1080}, nx::Seconds{1.4F});
    EXPECT_EQ(numbers->size(), 2U) << "both born at half a second";
    EXPECT_TRUE(quads.empty()) << "not drawn before its glyphs are on the GPU";
    numbers->append(quads, view, {.width = 1920, .height = 1080}, nx::Seconds{1.5F});
    EXPECT_EQ(numbers->size(), 0U);
}

} // namespace
