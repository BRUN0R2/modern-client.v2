// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// What the game shows of every map's world over what the map sets: the repository's
// data/cstrike/environment.toml, which sets nothing, a mod's sky, and the sky a map is drawn with.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.assets;
import cs.shared;

#include "boxMap.hpp"

namespace {

[[nodiscard]] auto repositoryText() -> std::string {
    return nx::platform::readTextFile(std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" /
                                      "environment.toml")
        .value_or(std::string{});
}

TEST(EnvironmentTest, CounterStrikesSetsNothingOverItsMaps) {
    const auto environment = cs::readWorldEnvironment(repositoryText(), "environment.toml");
    ASSERT_TRUE(environment.has_value()) << (environment ? "" : nx::describe(environment.error()));
    EXPECT_FALSE(environment->sky.has_value()) << "each map draws its own sky";
}

TEST(EnvironmentTest, AModsSkyIsDrawnInPlaceOfEachMapsAndAMapsOwnElse) {
    const auto night = cs::readWorldEnvironment("sky = \"night\"", "environment.toml");
    ASSERT_TRUE(night.has_value()) << (night ? "" : nx::describe(night.error()));
    const std::vector<std::byte> desert =
        nx::test::boxMapWith(R"({ "classname" "worldspawn" "skyname" "des" })");
    const std::vector<std::byte> skyless = nx::test::boxMapWith(R"({ "classname" "worldspawn" })");
    const auto withSky = nx::assets::BspView::parse(desert);
    const auto withoutSky = nx::assets::BspView::parse(skyless);
    ASSERT_TRUE(withSky.has_value());
    ASSERT_TRUE(withoutSky.has_value());
    EXPECT_EQ(cs::skyNameOf(*withSky, *night), "night") << "the mod's, over the map's";
    EXPECT_EQ(cs::skyNameOf(*withoutSky, *night), "night");
    const cs::WorldEnvironment none;
    EXPECT_EQ(cs::skyNameOf(*withSky, none), "des") << "the map's own";
    EXPECT_EQ(cs::skyNameOf(*withoutSky, none), std::nullopt) << "the client's default";
}

TEST(EnvironmentTest, ASkyOfNoNameOrOfAPathIsRefused) {
    for (const std::string_view text :
         {"sky = \"\"", "sky = \"../night\"", "sky = \"env/night\"", "sky = 1"}) {
        const auto environment = cs::readWorldEnvironment(text, "environment.toml");
        ASSERT_FALSE(environment.has_value()) << text;
        EXPECT_EQ(environment.error().code, cs::EnvironmentError::DataInvalid);
    }
}

} // namespace
