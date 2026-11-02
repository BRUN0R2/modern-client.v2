// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The scoreboard: how it lists the teams and their players, where its parts go on the screens it was measured
// on, what it says of a team's players, and +showscores.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;
import nx.rhi;
import cs.shared;
import cs.client;

#include "consoleFixtures.hpp"

namespace {

/// A row of `name`, of `team`, in slot `slot`, of those frags and deaths.
[[nodiscard]] auto rowOf(std::uint32_t slot, std::string_view name, cs::Team team, std::int32_t frags,
                         std::uint32_t deaths) -> cs::ScoreRow {
    return {.player = cs::PlayerIndex{slot},
            .name = std::string{name},
            .team = team,
            .score = {.frags = frags, .deaths = deaths}};
}

/// The names of `team`'s players, as the board lists them.
[[nodiscard]] auto namesOf(const cs::ScoreTeam& team) -> std::vector<std::string> {
    return team.rows | std::views::transform(&cs::ScoreRow::name) | std::ranges::to<std::vector>();
}

TEST(ScoreTeamsTest, ATeamsPlayersGoFromTheMostFragsThenTheFewestDeathsThenTheirSlots) {
    const std::array rows{
        rowOf(0, "Few", cs::Team::Terrorists, 1, 0), rowOf(1, "Most", cs::Team::Terrorists, 5, 3),
        rowOf(2, "Dying", cs::Team::Terrorists, 1, 4), rowOf(3, "Same", cs::Team::Terrorists, 1, 0)};
    const std::vector<cs::ScoreTeam> teams = cs::scoreTeamsOf(rows, {});
    ASSERT_EQ(teams.size(), 1U) << "a team of no player is not listed";
    EXPECT_EQ(namesOf(teams.front()), (std::vector<std::string>{"Most", "Few", "Same", "Dying"}));
}

TEST(ScoreTeamsTest, TheTeamOfMoreWinsGoesFirstTheTerroristsOfAsMany) {
    const std::array rows{rowOf(0, "Ct", cs::Team::CounterTerrorists, 0, 0),
                          rowOf(1, "T", cs::Team::Terrorists, 0, 0)};
    nx::EnumArray<cs::Team, std::uint32_t> wins{};
    EXPECT_EQ(cs::scoreTeamsOf(rows, wins).front().team, cs::Team::Terrorists);
    wins[cs::Team::CounterTerrorists] = 2;
    const std::vector<cs::ScoreTeam> teams = cs::scoreTeamsOf(rows, wins);
    EXPECT_EQ(teams.front().team, cs::Team::CounterTerrorists);
    EXPECT_EQ(teams.front().wins, 2U);
}

TEST(ScoreboardTextTest, ATeamRowCountsItsPlayers) {
    EXPECT_EQ(cs::teamRowText(cs::Team::Terrorists, 1), "Terrorists   -   1 player");
    EXPECT_EQ(cs::teamRowText(cs::Team::CounterTerrorists, 2), "Counter-Terrorists   -   2 players");
}

/// The board measured on the original at a screen, against its panel's top left where inside it: the panel,
/// the line under the heads and the team's line, the name's pen and where the score's column ends. All whole
/// pixels; the score's end as the units give it, within 2 of the measure at 800x600 and 1600x900.
struct MeasuredBoard {
    nx::rhi::Extent2d size{};
    std::int32_t panelX = 0;
    std::int32_t panelY = 0;
    std::int32_t panelWide = 0;
    std::int32_t panelTall = 0;
    std::int32_t separatorY = 0;
    std::int32_t teamLineY = 0;
    std::int32_t nameX = 0;
    std::int32_t scoreEnd = 0;
};

TEST(ScoreboardLayoutTest, ThePanelAndItsRowsAreWhereTheOriginalDrawsThem) {
    const std::array measured{
        MeasuredBoard{{.width = 800, .height = 600}, 75, 87, 650, 425, 23, 51, 45, 515},
        MeasuredBoard{{.width = 1024, .height = 768}, 96, 112, 832, 544, 24, 53, 61, 659},
        MeasuredBoard{{.width = 1280, .height = 720}, 250, 105, 780, 510, 23, 51, 45, 618},
        MeasuredBoard{{.width = 1280, .height = 960}, 120, 140, 1040, 680, 24, 53, 61, 824},
        MeasuredBoard{{.width = 1600, .height = 900}, 312, 131, 975, 637, 24, 53, 61, 772},
    };
    for (const MeasuredBoard& board : measured) {
        const cs::ScoreboardMetrics metrics = cs::scoreboardMetricsOf(board.size, 16);
        EXPECT_EQ(metrics.panelX, board.panelX) << board.size.width << "x" << board.size.height;
        EXPECT_EQ(metrics.panelY, board.panelY) << board.size.width << "x" << board.size.height;
        EXPECT_EQ(metrics.panelWide, board.panelWide) << board.size.width << "x" << board.size.height;
        EXPECT_EQ(metrics.panelTall, board.panelTall) << board.size.width << "x" << board.size.height;
        EXPECT_EQ(metrics.separatorY, board.separatorY) << board.size.width << "x" << board.size.height;
        EXPECT_EQ(metrics.teamLineY, board.teamLineY) << board.size.width << "x" << board.size.height;
        EXPECT_EQ(metrics.nameX, board.nameX) << board.size.width << "x" << board.size.height;
        EXPECT_EQ(metrics.scoreEnd, board.scoreEnd) << board.size.width << "x" << board.size.height;
    }
}

TEST(ScoreboardLayoutTest, ARowsTextIsCenteredUnderItsTeamsLineAsTheOriginalPutsIt) {
    // A font of lines 16 tall, 12 units down at 1280x720: 18 less 8 and a pixel.
    EXPECT_EQ(cs::scoreboardMetricsOf({.width = 1280, .height = 720}, 16).rowTextUnderLine, 9);
    EXPECT_EQ(cs::scoreboardMetricsOf({.width = 1280, .height = 960}, 16).rowTextUnderLine, 15);
}

TEST(ScoreboardControlsTest, ShowscoresShowsTheBoardWhileHeld) {
    nx::test::RecordingOutput output;
    nx::test::MemoryFiles files;
    nx::console::Console console{output, files};
    const auto controls = cs::ScoreboardControls::registerIn(console);
    ASSERT_TRUE(controls.has_value());
    EXPECT_FALSE(controls->shown.isDown());
    console.addText("+showscores");
    std::ignore = console.execute();
    EXPECT_TRUE(controls->shown.isDown());
    console.addText("-showscores");
    std::ignore = console.execute();
    EXPECT_FALSE(controls->shown.isDown());
    EXPECT_EQ(controls->hostname.value(), cs::kDefaultHostname);
}

} // namespace
