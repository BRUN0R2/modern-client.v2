// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The console's navigation tools on the box map of the fuzz corpus: nav_generate builds its meshes by
// the repository's profiles, nav_draw draws one, and each tells what it cannot do.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.console;
import nx.assets;
import nx.game;
import nx.navigation;
import cs.client;

#include "consoleFixtures.hpp"

namespace {

using nx::console::ConsoleError;

[[nodiscard]] auto repositoryText(std::string_view file) -> std::string {
    auto text = nx::platform::readTextFile(std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / file);
    EXPECT_TRUE(text.has_value());
    return text.value_or("");
}

/// The tools of the repository's profiles and brush classes, but that its floor of 32 units by 32, the
/// box's 64 less the player's radius on each side, is not too small an island to keep.
class NavigationToolsTest : public testing::Test {
protected:
    NavigationToolsTest() {
        auto navigation = nx::game::readNavigationData(repositoryText("navigation.toml"), "navigation.toml");
        auto classes = nx::game::readEntityClasses(repositoryText("entities.toml"), "entities.toml");
        EXPECT_TRUE(navigation.has_value() && classes.has_value());
        m_navigation = std::move(navigation).value_or(nx::game::NavigationData{});
        m_navigation.build.smallestIsland = nx::HammerUnits{0.0F};
        m_classes = std::move(classes).value_or(nx::game::EntityClasses{});
        auto tools = cs::NavigationTools::create(m_navigation, m_classes);
        EXPECT_TRUE(tools.has_value());
        if (tools) {
            m_tools.emplace(std::move(*tools));
        }
        auto bytes = nx::platform::readFile(std::filesystem::path{NX_FUZZ_CORPUS_DIRECTORY} / "bsp" /
                                            "boxEmbedded.bsp");
        EXPECT_TRUE(bytes.has_value());
        m_bytes = bytes.value_or(std::vector<std::byte>{});
    }

    nx::game::NavigationData m_navigation;
    nx::game::EntityClasses m_classes;
    std::optional<cs::NavigationTools> m_tools;
    std::vector<std::byte> m_bytes;
    nx::test::RecordingOutput m_output;
    nx::test::MemoryFiles m_files;
    nx::console::Console m_console{m_output, m_files};
};

TEST_F(NavigationToolsTest, NothingIsDrawnOrBuiltWithoutAMapAndAMesh) {
    ASSERT_TRUE(m_tools.has_value());
    const auto generated = m_tools->generate(m_console);
    ASSERT_FALSE(generated.has_value());
    EXPECT_EQ(generated.error().code, ConsoleError::CommandFailed);

    const std::array zombie{std::string{"zombie"}};
    const auto unknown = m_tools->draw(zombie);
    ASSERT_FALSE(unknown.has_value());
    EXPECT_EQ(unknown.error().code, ConsoleError::InvalidArguments);
    EXPECT_NE(nx::describe(unknown.error()).find("UnknownProfile"), std::string::npos);

    const auto noMesh = m_tools->draw({});
    ASSERT_FALSE(noMesh.has_value());
    EXPECT_EQ(noMesh.error().code, ConsoleError::CommandFailed);
    EXPECT_NE(nx::describe(noMesh.error()).find("NoMesh"), std::string::npos);
    EXPECT_TRUE(m_tools->overlay().triangles.empty());
}

TEST_F(NavigationToolsTest, TheMapsMeshIsBuiltAndDrawnUntilAnotherMapIsPlayed) {
    ASSERT_TRUE(m_tools.has_value());
    const auto map = nx::assets::BspView::parse(m_bytes);
    ASSERT_TRUE(map.has_value());
    ASSERT_TRUE(m_tools->registerIn(m_console).has_value());
    m_tools->play(*map);
    m_console.addText("nav_generate\nnav_draw\n");
    std::ignore = m_console.execute(); // what it ran is in the output
    EXPECT_TRUE(m_output.failures.empty());
    ASSERT_EQ(m_output.lines.size(), 1U);
    EXPECT_NE(m_output.lines.front().find("\"player\""), std::string::npos);
    const cs::SceneOverlay drawn = m_tools->overlay();
    EXPECT_FALSE(drawn.triangles.empty());
    EXPECT_EQ(drawn.triangles.size() % 3, 0U);
    EXPECT_FALSE(drawn.lines.empty());
    EXPECT_EQ(drawn.lines.size() % 2, 0U);

    // nav_draw again stops; a map played forgets the meshes.
    ASSERT_TRUE(m_tools->draw({}).has_value());
    EXPECT_TRUE(m_tools->overlay().lines.empty());
    ASSERT_TRUE(m_tools->draw({}).has_value());
    m_tools->play(*map);
    EXPECT_TRUE(m_tools->overlay().lines.empty());
    EXPECT_FALSE(m_tools->draw({}).has_value());
}

} // namespace
