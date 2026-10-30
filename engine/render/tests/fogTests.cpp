// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// How much a fog hides by distance, as the original's three OpenGL curves do, within its start, its end
// and its opacity; the fogs a renderer takes; its block for the shaders; and a fog as it comes, goes and
// changes.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.render;

namespace {

using nx::HammerUnits;
using nx::Seconds;
using nx::render::Fog;
using nx::render::FogCurve;
using nx::render::FogTransition;

constexpr float kTolerance = 1.0e-5F;
constexpr float kDensity = 0.002F;

[[nodiscard]] auto mapFog() -> Fog {
    return Fog{.color = {.red = 0.25F, .green = 0.5F, .blue = 0.75F},
               .curve = FogCurve::ExponentialSquared,
               .density = kDensity};
}

TEST(FogTest, WithItsStartAt0AndNoEndAFogIsTheOriginalsCurve) {
    for (const float distance : {0.0F, 100.0F, 500.0F, 2000.0F}) {
        const float scaled = kDensity * distance;
        EXPECT_NEAR(nx::render::fogAmountAt(mapFog(), HammerUnits{distance}),
                    1.0F - std::exp(-(scaled * scaled)), kTolerance)
            << "GL_EXP2 at " << distance;
        Fog exponential = mapFog();
        exponential.curve = FogCurve::Exponential;
        EXPECT_NEAR(nx::render::fogAmountAt(exponential, HammerUnits{distance}), 1.0F - std::exp(-scaled),
                    kTolerance)
            << "GL_EXP at " << distance;
    }
    const Fog linear{.curve = FogCurve::Linear, .start = HammerUnits{100.0F}, .end = HammerUnits{300.0F}};
    EXPECT_EQ(nx::render::fogAmountAt(linear, HammerUnits{50.0F}), 0.0F);
    EXPECT_NEAR(nx::render::fogAmountAt(linear, HammerUnits{200.0F}), 0.5F, kTolerance) << "GL_LINEAR";
    EXPECT_EQ(nx::render::fogAmountAt(linear, HammerUnits{400.0F}), 1.0F);
}

TEST(FogTest, NothingNearerThanItsStartIsHiddenNorMoreThanAtItsEndAndAtMostItsOpacity) {
    Fog limited = mapFog();
    limited.start = HammerUnits{200.0F};
    limited.end = HammerUnits{700.0F};
    limited.opacity = 0.6F;
    EXPECT_EQ(nx::render::fogAmountAt(limited, HammerUnits{150.0F}), 0.0F) << "nearer than its start";
    const float atEnd = nx::render::fogAmountAt(limited, HammerUnits{700.0F});
    const float scaled = kDensity * 500.0F;
    EXPECT_NEAR(atEnd, 0.6F * (1.0F - std::exp(-(scaled * scaled))), kTolerance)
        << "its curve past its start";
    EXPECT_EQ(nx::render::fogAmountAt(limited, HammerUnits{5000.0F}), atEnd) << "held from its end on";
    EXPECT_LE(nx::render::fogAmountAt(limited, HammerUnits{1.0e9F}), limited.opacity);
}

TEST(FogTest, ARendererTakesOnlyAFogWithinItsLimits) {
    EXPECT_TRUE(nx::render::isValid(mapFog()));
    const auto withEach = [](auto change) {
        Fog fog = mapFog();
        change(fog);
        return nx::render::isValid(fog);
    };
    EXPECT_FALSE(withEach([](Fog& fog) { fog.color.red = 1.5F; }));
    EXPECT_FALSE(withEach([](Fog& fog) { fog.density = -1.0F; }));
    EXPECT_FALSE(withEach([](Fog& fog) { fog.density = std::numeric_limits<float>::quiet_NaN(); }));
    EXPECT_FALSE(withEach([](Fog& fog) { fog.start = HammerUnits{-1.0F}; }));
    EXPECT_FALSE(withEach([](Fog& fog) {
        fog.start = HammerUnits{300.0F};
        fog.end = HammerUnits{300.0F};
    })) << "an end not past its start";
    EXPECT_FALSE(withEach([](Fog& fog) { fog.curve = FogCurve::Linear; })) << "a linear fog without end";
    EXPECT_FALSE(withEach([](Fog& fog) { fog.opacity = 1.1F; }));
    EXPECT_FALSE(withEach([](Fog& fog) { fog.curve = FogCurve::Count; }));
}

TEST(FogTest, AFogReachesWhereItIsNearlyAsThickAsItGetsOrItsEndTheNearer) {
    const float exponent = -std::log(1.0F - nx::render::kFogReachThickness);
    EXPECT_NEAR(nx::render::fogReachOf(mapFog()).value(), std::sqrt(exponent) / kDensity, 1.0e-2F);
    Fog exponential = mapFog();
    exponential.curve = FogCurve::Exponential;
    exponential.start = HammerUnits{100.0F};
    EXPECT_NEAR(nx::render::fogReachOf(exponential).value(), 100.0F + (exponent / kDensity), 1.0e-1F);
    Fog ended = mapFog();
    ended.end = HammerUnits{300.0F};
    EXPECT_EQ(nx::render::fogReachOf(ended), HammerUnits{300.0F});
    Fog still = mapFog();
    still.density = 0.0F;
    still.start = HammerUnits{50.0F};
    EXPECT_EQ(nx::render::fogReachOf(still), HammerUnits{50.0F}) << "a fog that never thickens";
}

TEST(FogTest, ItsBlockCarriesItsColorAsTheTargetTakesItAndHowDistancesAreMeasured) {
    const nx::render::Camera camera{.position = {.x = 10.0F, .y = 20.0F, .z = 30.0F},
                                    .angles = {.yaw = nx::Degrees{90.0F}}};
    const nx::render::FogBlock none =
        nx::render::fogBlockOf(std::nullopt, camera, nx::render::ColorEncoding::Display);
    EXPECT_EQ(none.color.w, 0.0F) << "no fog hides nothing";
    const nx::render::FogBlock display =
        nx::render::fogBlockOf(mapFog(), camera, nx::render::ColorEncoding::Display);
    EXPECT_EQ(display.color, (nx::Vec4{.x = 0.25F, .y = 0.5F, .z = 0.75F, .w = 1.0F}));
    EXPECT_EQ(display.shape,
              (nx::Vec4{.x = 2.0F, .y = kDensity, .z = 0.0F, .w = nx::render::kEndlessFog.value()}));
    EXPECT_EQ(display.eye, (nx::Vec4{.x = 10.0F, .y = 20.0F, .z = 30.0F, .w = 0.0F})) << "along the view";
    EXPECT_NEAR(display.forward.y, 1.0F, kTolerance) << "a yaw of 90 looks along +Y";
    const nx::render::FogBlock linear =
        nx::render::fogBlockOf(mapFog(), camera, nx::render::ColorEncoding::Linear);
    EXPECT_NEAR(linear.color.y, std::pow(0.5F, 2.2F), kTolerance) << "as light";
    EXPECT_EQ(linear.eye.w, 1.0F) << "from the eye";
}

TEST(FogTransitionTest, TurnedOnAFogComesInFromAsFarAsItReachesToItsStart) {
    FogTransition transition;
    EXPECT_FALSE(transition.shown().has_value());
    const Fog target = mapFog();
    transition.show(target, Seconds{2.0F});
    const auto first = transition.shown();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->opacity, 0.0F) << "hiding nothing at first";
    EXPECT_EQ(first->start, nx::render::fogReachOf(target)) << "as far as it reaches";
    float lastStart = first->start.value();
    float lastAmount = 0.0F;
    for (int step = 0; step < 8; ++step) {
        transition.advance(Seconds{0.25F});
        const auto shown = transition.shown();
        ASSERT_TRUE(shown.has_value());
        EXPECT_LT(shown->start.value(), lastStart) << "nearer at each step";
        const float amount = nx::render::fogAmountAt(*shown, HammerUnits{2000.0F}); // past where it reaches
        EXPECT_GT(amount, lastAmount) << "thicker at each step";
        lastStart = shown->start.value();
        lastAmount = amount;
    }
    EXPECT_EQ(transition.shown(), target) << "where it goes, in its time";
}

TEST(FogTransitionTest, TurnedOffItGoesBackOutAndIsGone) {
    FogTransition transition;
    transition.show(mapFog(), Seconds{});
    ASSERT_EQ(transition.shown(), mapFog()) << "at once, in no time";
    transition.show(std::nullopt, Seconds{1.0F});
    transition.advance(Seconds{0.5F});
    const auto going = transition.shown();
    ASSERT_TRUE(going.has_value()) << "still going";
    EXPECT_GT(going->start, mapFog().start);
    EXPECT_LT(going->opacity, 1.0F);
    transition.advance(Seconds{0.5F});
    EXPECT_FALSE(transition.shown().has_value());
    EXPECT_FALSE(transition.target().has_value());
}

TEST(FogTransitionTest, ChangedWhileItShowsItsColorGoesToTheOtherFromWhereItIs) {
    FogTransition transition;
    transition.show(mapFog(), Seconds{});
    Fog red = mapFog();
    red.color = {.red = 1.0F};
    transition.show(red, Seconds{1.0F});
    transition.advance(Seconds{0.5F});
    const auto halfway = transition.shown();
    ASSERT_TRUE(halfway.has_value());
    EXPECT_NEAR(halfway->color.red, 0.625F, kTolerance) << "eased: half the way at half the time";
    // Changed again halfway, it goes on from what shows, with no jump.
    Fog green = mapFog();
    green.color = {.green = 1.0F};
    transition.show(green, Seconds{1.0F});
    EXPECT_EQ(transition.shown(), halfway);
    transition.advance(Seconds{1.0F});
    EXPECT_EQ(transition.shown(), green);
}

} // namespace
