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
    const std::vector<cs::ScoreTeam> teams = cs::scoreTeamsOf(rows, {}, cs::Team::Terrorists);
    ASSERT_EQ(teams.size(), 2U);
    EXPECT_EQ(teams[0].team, cs::Team::Terrorists);
    EXPECT_EQ(namesOf(teams[0]), (std::vector<std::string>{"Most", "Few", "Same", "Dying"}));
    EXPECT_EQ(teams[1].team, cs::Team::CounterTerrorists);
    EXPECT_TRUE(teams[1].rows.empty());
}

TEST(ScoreTeamsTest, EachViewerSeesTheirTeamLeftAndTheirOpponentRightRegardlessOfWins) {
    const std::array rows{rowOf(0, "Ct", cs::Team::CounterTerrorists, 0, 0),
                          rowOf(1, "T", cs::Team::Terrorists, 0, 0)};
    nx::EnumArray<cs::Team, std::uint32_t> wins{};
    wins[cs::Team::CounterTerrorists] = 2;
    wins[cs::Team::Terrorists] = 9;

    const std::vector<cs::ScoreTeam> terroristView = cs::scoreTeamsOf(rows, wins, cs::Team::Terrorists);
    ASSERT_EQ(terroristView.size(), 2U);
    EXPECT_EQ(terroristView[0].team, cs::Team::Terrorists);
    EXPECT_EQ(terroristView[0].wins, 9U);
    EXPECT_EQ(terroristView[1].team, cs::Team::CounterTerrorists);
    EXPECT_EQ(terroristView[1].wins, 2U);

    const std::vector<cs::ScoreTeam> counterTerroristView =
        cs::scoreTeamsOf(rows, wins, cs::Team::CounterTerrorists);
    ASSERT_EQ(counterTerroristView.size(), 2U);
    EXPECT_EQ(counterTerroristView[0].team, cs::Team::CounterTerrorists);
    EXPECT_EQ(counterTerroristView[1].team, cs::Team::Terrorists);
}

TEST(ScoreboardTextTest, ATeamRowCountsItsPlayers) {
    EXPECT_EQ(cs::teamRowText(cs::Team::Terrorists, 1), "Terrorists   -   1 player");
    EXPECT_EQ(cs::teamRowText(cs::Team::CounterTerrorists, 2), "Counter-Terrorists   -   2 players");
}

/// The two columns' placement on the screen: the panel, the columns and the ends of their data fields.
struct BoardLayout {
    nx::rhi::Extent2d size{};
    std::int32_t panelX = 0;
    std::int32_t panelY = 0;
    std::int32_t panelWide = 0;
    std::int32_t panelTall = 0;
    std::int32_t teamWide = 0;
    std::int32_t leftTeamX = 0;
    std::int32_t rightTeamX = 0;
    std::int32_t nameX = 0;
    std::int32_t statusX = 0;
    std::int32_t healthEnd = 0;
    std::int32_t moneyEnd = 0;
    std::int32_t scoreEnd = 0;
    std::int32_t deathsEnd = 0;
    std::int32_t latencyEnd = 0;
};

TEST(ScoreboardLayoutTest, TheTwoColumnsStayWithinThePanelAtDifferentAspectRatios) {
    const std::array layouts{
        BoardLayout{{.width = 800, .height = 600}, 75, 87, 650, 425, 313, 6, 329, 15, 131, 169, 212, 247,
                    284, 313},
        BoardLayout{{.width = 1024, .height = 768}, 96, 112, 832, 544, 403, 6, 421, 20, 169, 217, 274,
                    318, 366, 403},
        BoardLayout{{.width = 1280, .height = 720}, 250, 105, 780, 510, 377, 6, 395, 18, 158, 203, 256,
                    297, 343, 377},
        BoardLayout{{.width = 1600, .height = 900}, 312, 131, 975, 637, 473, 6, 494, 23, 198, 255, 321,
                    373, 430, 473},
        BoardLayout{{.width = 320, .height = 240}, 30, 35, 260, 170, 121, 6, 131, 6, 50, 65, 82, 95, 110,
                    121},
    };
    for (const BoardLayout& layout : layouts) {
        const cs::ScoreboardMetrics metrics = cs::scoreboardMetricsOf(layout.size, nx::Pixels{16}, 0);
        EXPECT_EQ(metrics.panel.x, nx::Pixels{layout.panelX})
            << layout.size.width << "x" << layout.size.height;
        EXPECT_EQ(metrics.panel.y, nx::Pixels{layout.panelY})
            << layout.size.width << "x" << layout.size.height;
        EXPECT_EQ(metrics.panel.wide, nx::Pixels{layout.panelWide})
            << layout.size.width << "x" << layout.size.height;
        EXPECT_EQ(metrics.panel.tall, nx::Pixels{layout.panelTall})
            << layout.size.width << "x" << layout.size.height;
        EXPECT_EQ(metrics.teamWide, nx::Pixels{layout.teamWide})
            << layout.size.width << "x" << layout.size.height;
        EXPECT_EQ(metrics.leftTeamX, nx::Pixels{layout.leftTeamX})
            << layout.size.width << "x" << layout.size.height;
        EXPECT_EQ(metrics.rightTeamX, nx::Pixels{layout.rightTeamX})
            << layout.size.width << "x" << layout.size.height;
        EXPECT_EQ(metrics.nameX, nx::Pixels{layout.nameX})
            << layout.size.width << "x" << layout.size.height;
        EXPECT_EQ(metrics.statusX, nx::Pixels{layout.statusX})
            << layout.size.width << "x" << layout.size.height;
        EXPECT_EQ(metrics.healthEnd, nx::Pixels{layout.healthEnd})
            << layout.size.width << "x" << layout.size.height;
        EXPECT_EQ(metrics.moneyEnd, nx::Pixels{layout.moneyEnd})
            << layout.size.width << "x" << layout.size.height;
        EXPECT_EQ(metrics.scoreEnd, nx::Pixels{layout.scoreEnd})
            << layout.size.width << "x" << layout.size.height;
        EXPECT_EQ(metrics.deathsEnd, nx::Pixels{layout.deathsEnd})
            << layout.size.width << "x" << layout.size.height;
        EXPECT_EQ(metrics.latencyEnd, nx::Pixels{layout.latencyEnd})
            << layout.size.width << "x" << layout.size.height;
        EXPECT_LT(metrics.nameX, metrics.statusX);
        EXPECT_LT(metrics.statusX, metrics.healthEnd);
        EXPECT_LT(metrics.healthEnd, metrics.moneyEnd);
        EXPECT_LT(metrics.moneyEnd, metrics.scoreEnd);
        EXPECT_LT(metrics.scoreEnd, metrics.deathsEnd);
        EXPECT_LT(metrics.deathsEnd, metrics.latencyEnd);
        EXPECT_LE(metrics.latencyEnd, metrics.teamWide);
    }
}

TEST(ScoreboardLayoutTest, TheRowsExpandThePanelAndCompressToFitTheScreen) {
    const cs::ScoreboardMetrics defaultRows =
        cs::scoreboardMetricsOf({.width = 1280, .height = 720}, nx::Pixels{16}, 0);
    const cs::ScoreboardMetrics manyRows =
        cs::scoreboardMetricsOf({.width = 1280, .height = 720}, nx::Pixels{16}, 32);
    EXPECT_GT(manyRows.panel.tall, defaultRows.panel.tall);
    EXPECT_LT(manyRows.rowPitch, defaultRows.rowPitch);
    EXPECT_LE(manyRows.rowTextUnderLine + nx::Pixels{16}, manyRows.rowPitch);
    EXPECT_LE(manyRows.panel.y + manyRows.panel.tall, nx::Pixels{720});

    const cs::ScoreboardMetrics sixteenRows =
        cs::scoreboardMetricsOf({.width = 1280, .height = 720}, nx::Pixels{16}, 16);
    EXPECT_EQ(sixteenRows.rowPitch, defaultRows.rowPitch);
    EXPECT_GT(sixteenRows.panel.tall, defaultRows.panel.tall);
}

TEST(ScoreboardLayoutTest, ARowsTextIsCenteredUnderItsTeamsLine) {
    // A font of lines 16 tall sits just above each row's center, with its current pitch.
    EXPECT_EQ(cs::scoreboardMetricsOf({.width = 1280, .height = 720}, nx::Pixels{16}, 0).rowTextUnderLine,
              nx::Pixels{6});
    EXPECT_EQ(cs::scoreboardMetricsOf({.width = 1280, .height = 960}, nx::Pixels{16}, 0).rowTextUnderLine,
              nx::Pixels{12});
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
