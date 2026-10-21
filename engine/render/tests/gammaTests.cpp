// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The expected values are the original engine's tables, as the Xash3D-FWGS project records them for
// its own tests (gamma 2.5, brightness 0, texgamma 2, lightgamma 2.5).

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.render;

namespace {

using nx::render::GammaSettings;

constexpr GammaSettings kRecorded{
    .gamma = 2.5F, .brightness = 0.0F, .textureGamma = 2.0F, .lightGamma = 2.5F};

TEST(GammaTest, TexgammaBrightensThePalettesAsTheOriginalDoes) {
    const auto table = nx::render::textureGammaTable(kRecorded);
    const std::array<std::uint8_t, 12> recorded{0, 3, 5, 7, 9, 10, 12, 14, 15, 17, 19, 20};
    EXPECT_TRUE(std::ranges::equal(std::span{table}.first(recorded.size()), recorded));
    EXPECT_EQ(table[255], 255);
}

TEST(GammaTest, LightgammaAndGammaCancelWithoutBrightness) {
    // The recorded table, 10 bits, starts 0, 0, 1, 2, 3, 4 and ends at 1023; drawn, the upper 8.
    const auto table = nx::render::lightTable(kRecorded);
    EXPECT_EQ(table[0], 0);
    EXPECT_EQ(table[4], 3 >> 2);
    EXPECT_EQ(table[100], 99 >> 2);
    EXPECT_EQ(table[1023], 1023 >> 2);
}

TEST(GammaTest, BrightnessLiftsTheShadows) {
    const auto dark = nx::render::lightTable(kRecorded);
    const auto bright = nx::render::lightTable({.brightness = 1.0F});
    EXPECT_GT(bright[100], dark[100]);
    EXPECT_EQ(bright[1023], dark[1023]) << "full light stays full";
}

TEST(GammaTest, TheLightScaleIsTheOriginalsFixedPoint) {
    EXPECT_EQ(nx::render::lightScale(kRecorded), 338U) << "round(2^(1/2.5) * 256)";
    EXPECT_EQ(nx::render::lightScale({.lightGamma = 1.8F}), 376U) << "round(2^(1/1.8) * 256)";
}

TEST(GammaTest, SettingsAreHeldToTheOriginalsRanges) {
    EXPECT_EQ(nx::render::textureGammaTable({.gamma = 9.0F}), nx::render::textureGammaTable({.gamma = 3.0F}));
    EXPECT_EQ(nx::render::lightTable({.brightness = -1.0F}), nx::render::lightTable({.brightness = 0.0F}));
}

} // namespace
