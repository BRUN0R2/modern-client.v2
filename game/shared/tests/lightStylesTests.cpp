// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.assets;
import cs.shared;

#include "boxMap.hpp"
#include "temporaryDirectory.hpp"

namespace {

TEST(WorldLightStylesTest, TheProjectDataSetsTheWorldsPatterns) {
    const std::filesystem::path file =
        std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "lightStyles.toml";
    const auto text = nx::platform::readTextFile(file);
    ASSERT_TRUE(text.has_value());
    const auto world = cs::readWorldLightStyles(*text, "lightStyles.toml");
    ASSERT_TRUE(world.has_value()) << nx::describe(world.error());
    const std::vector<std::byte> bytes = nx::test::boxMapWith(R"({ "classname" "worldspawn" })");
    const auto map = nx::assets::BspView::parse(bytes);
    ASSERT_TRUE(map.has_value());
    const cs::MapLightStyles styles = cs::mapLightStyles(*map, *world);
    EXPECT_EQ(styles.patterns[0], "m");
    EXPECT_EQ(styles.patterns[4], "mamamamamama");
    EXPECT_EQ(styles.patterns[9], "aaaaaaaazzzzzzzz");
    EXPECT_TRUE(styles.patterns[12].empty()) << "the underwater mutation stays unset";
    EXPECT_EQ(styles.patterns[63], "a");
}

TEST(WorldLightStylesTest, AStyleIsGivenOnceInLettersFromAToZ) {
    const auto errorOf = [](std::string_view text) {
        const auto read = cs::readWorldLightStyles(text, "test");
        return read ? std::nullopt : std::optional{read.error().code};
    };
    EXPECT_EQ(errorOf("[[styles]]\nstyle = 1\npattern = \"mM\""), cs::LightStyleError::BadPattern);
    EXPECT_EQ(errorOf("[[styles]]\nstyle = 1\npattern = \"\""), cs::LightStyleError::BadPattern);
    EXPECT_EQ(errorOf("[[styles]]\nstyle = 1\npattern = \"m\"\n[[styles]]\nstyle = 1\npattern = \"a\""),
              cs::LightStyleError::StyleTwice);
    EXPECT_EQ(errorOf("[[styles]]\nstyle = 64\npattern = \"m\""), cs::LightStyleError::DataInvalid);
    EXPECT_EQ(errorOf("[[styles]]\nstyle = -1\npattern = \"m\""), cs::LightStyleError::DataInvalid);
}

TEST(WorldLightStylesTest, SwitchableLightsStartAsTheirEntitiesSay) {
    const std::vector<std::byte> bytes = nx::test::boxMapWith(R"({ "classname" "worldspawn" }
{ "classname" "light" "targetname" "lamp" "style" "32" "spawnflags" "1" }
{ "classname" "light_spot" "targetname" "sign" "style" "33" "pattern" "za" }
{ "classname" "light" "style" "34" "pattern" "z" }
{ "classname" "light" "targetname" "normal" "style" "35" }
{ "classname" "light" "targetname" "invalid" "style" "64" })");
    const auto map = nx::assets::BspView::parse(bytes);
    ASSERT_TRUE(map.has_value());
    const cs::MapLightStyles styles = cs::mapLightStyles(*map, {});
    EXPECT_EQ(styles.patterns[32], "a");
    EXPECT_EQ(styles.patterns[33], "za");
    EXPECT_TRUE(styles.patterns[34].empty()) << "a light without a name cannot be switched";
    EXPECT_EQ(styles.patterns[35], "m");
    EXPECT_TRUE(styles.patterns[0].empty()) << "the invalid style does not overwrite another";
}

TEST(WorldLightStylesTest, InvalidLightStyleDataPreventsTheGameContentFromOpening) {
    nx::test::TemporaryDirectory data;
    std::error_code error;
    std::filesystem::copy(std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike", data.path() / "cstrike",
                          std::filesystem::copy_options::recursive, error);
    ASSERT_FALSE(error) << error.message();
    data.write("cstrike/lightStyles.toml", "[[styles]]\nstyle = 0\npattern = \"invalid!\"");
    auto vfs = nx::assets::Vfs::create(std::array{data.path()});
    ASSERT_TRUE(vfs.has_value());
    const auto content = cs::readGameContent(data.path(), std::move(*vfs));
    ASSERT_FALSE(content.has_value());
    EXPECT_EQ(content.error().code, cs::GameContentError::Unavailable);
    EXPECT_NE(nx::describe(content.error()).find("lightStyles.toml"), std::string::npos);
    EXPECT_NE(nx::describe(content.error()).find("BadPattern"), std::string::npos);
}

} // namespace
