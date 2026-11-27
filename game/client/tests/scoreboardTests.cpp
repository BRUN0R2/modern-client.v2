// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The scoreboard: how it lists the teams side by side and their players, how many of a team live, and
// +showscores.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;
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
    ASSERT_EQ(teams.size(), 2U);
    EXPECT_EQ(namesOf(teams[1]), (std::vector<std::string>{"Most", "Few", "Same", "Dying"}));
}

TEST(ScoreTeamsTest, TheCounterTerroristsGoLeftTheTerroristsRightWhateverTheirWins) {
    const std::array rows{rowOf(0, "T", cs::Team::Terrorists, 0, 0)};
    nx::EnumArray<cs::Team, std::uint32_t> wins{};
    wins[cs::Team::Terrorists] = 7;
    wins[cs::Team::CounterTerrorists] = 2;
    const std::vector<cs::ScoreTeam> teams = cs::scoreTeamsOf(rows, wins);
    ASSERT_EQ(teams.size(), 2U) << "a team of no player is listed too";
    EXPECT_EQ(teams[0].team, cs::Team::CounterTerrorists);
    EXPECT_EQ(teams[0].wins, 2U);
    EXPECT_TRUE(teams[0].rows.empty());
    EXPECT_EQ(teams[1].team, cs::Team::Terrorists);
    EXPECT_EQ(teams[1].wins, 7U);
}

TEST(ScoreTeamsTest, ATeamCountsThoseOfItThatLive) {
    std::array rows{rowOf(0, "A", cs::Team::CounterTerrorists, 0, 0),
                    rowOf(1, "B", cs::Team::CounterTerrorists, 0, 0),
                    rowOf(2, "C", cs::Team::CounterTerrorists, 0, 0)};
    rows[1].isDead = true;
    const std::vector<cs::ScoreTeam> teams = cs::scoreTeamsOf(rows, {});
    EXPECT_EQ(teams[0].aliveCount(), 2U);
    EXPECT_EQ(teams[1].aliveCount(), 0U);
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
