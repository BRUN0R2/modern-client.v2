// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The team and appearance menus: the original's keys, auto-select, a refusal kept on the team menu, cancel
// only for a player who chose a team, and the mouse choosing the row it is pressed on.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.rhi;
import nx.ui;
import cs.shared;
import cs.client;

namespace {

using cs::Team;
using cs::TeamMenuEffect;
using cs::TeamMenuPage;
using nx::platform::Key;
using nx::platform::KeyChanged;

constexpr nx::rhi::Extent2d kScreen{.width = 1920, .height = 1080};

/// What the menu asked of the game.
struct Asked {
    std::vector<Team> teams{};
    std::vector<std::uint32_t> characters{};
    std::optional<std::string> refusal{};
};

[[nodiscard]] auto charactersOf() -> cs::Characters {
    cs::Characters characters;
    for (const std::string_view name : {"terror", "leet", "arctic", "guerilla"}) {
        characters.characters.push_back({.name = std::string{name}, .team = Team::Terrorists});
    }
    for (const std::string_view name : {"urban", "gsg9", "sas", "gign"}) {
        characters.characters.push_back({.name = std::string{name}, .team = Team::CounterTerrorists});
    }
    return characters;
}

[[nodiscard]] auto menuAsking(Asked& asked) -> cs::TeamMenu {
    return cs::TeamMenu{cs::TeamMenuActions{
        .chooseTeam = [&asked](Team team) -> std::optional<std::string> {
            asked.teams.push_back(team);
            return asked.refusal;
        },
        .defaultTeam = [] { return Team::CounterTerrorists; },
        .chooseCharacter = [&asked](std::uint32_t slot) { asked.characters.push_back(slot); }}};
}

/// Presses `key` on `menu`, its rows those it shows.
auto press(cs::TeamMenu& menu, Key key) -> TeamMenuEffect {
    const cs::TeamMenuFrame frame = menu.frameOf(cs::TeamMenuTexts{}, charactersOf(), Team::Terrorists);
    return menu.handle(KeyChanged{.key = key, .isDown = true}, {}, kScreen, frame.rows);
}

TEST(TeamMenuTest, ATeamThenACharacterAreChosenByTheOriginalsKeys) {
    Asked asked;
    cs::TeamMenu menu = menuAsking(asked);
    menu.open(false);
    EXPECT_EQ(menu.frameOf({}, charactersOf(), Team::Terrorists).rows.size(), 3U) << "1, 2 and 5: no cancel";
    EXPECT_EQ(press(menu, Key::Digit0), TeamMenuEffect::None) << "a player joining may not cancel";
    EXPECT_EQ(press(menu, Key::Digit2), TeamMenuEffect::Click);
    EXPECT_EQ(asked.teams, (std::vector<Team>{Team::CounterTerrorists}));
    ASSERT_EQ(menu.page(), TeamMenuPage::Character);
    const cs::TeamMenuFrame characters = menu.frameOf({}, charactersOf(), Team::Terrorists);
    EXPECT_EQ(characters.team, Team::CounterTerrorists);
    ASSERT_EQ(characters.rows.size(), 5U) << "four characters and auto-select";
    EXPECT_EQ(characters.rows[2].label, "sas");
    EXPECT_EQ(characters.rows[2].picture, "gfx/vgui/sas.tga");
    EXPECT_EQ(characters.rows[4].picture, "gfx/vgui/ct_random.tga");
    EXPECT_EQ(press(menu, Key::Digit3), TeamMenuEffect::Click);
    EXPECT_EQ(asked.characters, (std::vector<std::uint32_t>{3}));
    EXPECT_FALSE(menu.isOpen());
}

TEST(TeamMenuTest, AutoSelectJoinsTheDefaultTeamAndARefusalKeepsTheTeamMenu) {
    Asked asked;
    cs::TeamMenu menu = menuAsking(asked);
    menu.open(true);
    asked.refusal = "Too many CTs!";
    EXPECT_EQ(press(menu, Key::Digit5), TeamMenuEffect::Click);
    EXPECT_EQ(asked.teams, (std::vector<Team>{Team::CounterTerrorists})) << "SelectDefaultTeam's";
    EXPECT_EQ(menu.page(), TeamMenuPage::Team);
    EXPECT_EQ(menu.frameOf({}, charactersOf(), Team::Terrorists).refusal, "Too many CTs!");
    EXPECT_EQ(press(menu, Key::Escape), TeamMenuEffect::Click) << "a player who chose a team may cancel";
    EXPECT_FALSE(menu.isOpen());
}

TEST(TeamMenuTest, TheMouseSelectsAndChoosesTheRowItIsOver) {
    Asked asked;
    cs::TeamMenu menu = menuAsking(asked);
    menu.open(false);
    const cs::TeamMenuFrame frame = menu.frameOf({}, charactersOf(), Team::Terrorists);
    const nx::ui::PixelRect first = cs::TeamMenuLayout{kScreen}.row(0);
    const nx::platform::MousePosition over{.x = static_cast<float>(first.x.value() + 4),
                                           .y = static_cast<float>(first.y.value() + 4)};
    EXPECT_EQ(menu.handle(nx::platform::MouseButtonChanged{.button = nx::platform::MouseButton::Left,
                                                           .isDown = true},
                          over, kScreen, frame.rows),
              TeamMenuEffect::Click);
    EXPECT_EQ(asked.teams, (std::vector<Team>{Team::Terrorists}));
    EXPECT_EQ(press(menu, Key::Escape), TeamMenuEffect::Click) << "Escape auto-selects a character";
    EXPECT_EQ(asked.characters, (std::vector<std::uint32_t>{cs::kAutoSelectKey}));
}

} // namespace
