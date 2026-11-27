// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The team and appearance menus (docs/12-ui-hud-and-console.md, section 4): the original's choices and keys
/// (ReGameDLL_CS's HandleMenu_ChooseTeam and HandleMenu_ChooseAppearance, GPL, consulted: docs/20 section 3),
/// 1 the terrorists, 2 the counter-terrorists, 5 auto-select and 0 cancel; then 1 to 4 the team's characters
/// and 5 one of them at random; laid out in the buy menu's look.
export module cs.client:teamMenu;

import std;
import nx.core;
import nx.rhi;
import nx.ui;
import nx.platform;
import cs.shared;
import :menuDesign;

export namespace cs {

/// The keys of the team menu.
inline constexpr std::uint32_t kTerroristsKey = 1;
inline constexpr std::uint32_t kCounterTerroristsKey = 2;
inline constexpr std::uint32_t kAutoSelectKey = 5;
inline constexpr std::uint32_t kCancelKey = 0;

/// Which of the menus shows.
enum class TeamMenuPage : std::uint8_t {
    Team,      ///< The team menu (#Team_Select, #IG_Team_Select).
    Character, ///< The appearance menu of a team (#Terrorist_Select, #CT_Select).
};

/// What the game is asked by the menu.
struct TeamMenuActions {
    /// Joins `team`; the text of why it may not, when it may not (HandleMenu_ChooseTeam).
    std::function<std::optional<std::string>(Team)> chooseTeam;
    /// The team auto-select joins (SelectDefaultTeam).
    std::function<Team()> defaultTeam;
    /// Plays as the team's character `slot` (from 1), one at random for a slot of none.
    std::function<void(std::uint32_t)> chooseCharacter;
};

/// What an event did to the menu that the player hears.
enum class TeamMenuEffect : std::uint8_t {
    None,
    Rollover, ///< Another row is selected.
    Click,    ///< A choice was made.
};

/// A row of the menu: its key, what it says, its picture among the game's files (none: empty), and the team
/// it stands for, whose accent lights it.
struct TeamMenuRow {
    std::uint32_t key = 0;
    std::string label{};
    std::string picture{};
    Team accent{};

    [[nodiscard]] inline auto operator==(const TeamMenuRow&) const noexcept -> bool = default;
};

/// All the menu shows: its title, its rows, the one selected, and why the last team chosen was refused.
struct TeamMenuFrame {
    TeamMenuPage page{};
    Team team{}; ///< The team of the appearance menu, or the player's.
    std::string title{};
    std::vector<TeamMenuRow> rows{};
    std::optional<std::size_t> selected{};
    std::string refusal{};

    [[nodiscard]] inline auto operator==(const TeamMenuFrame&) const noexcept -> bool = default;
};

/// Where the menu's parts go on a screen: what heads it, its rows down its left, and the picture of the row
/// selected on their right, as the buy menu's rail and its detail.
class TeamMenuLayout {
public:
    explicit TeamMenuLayout(nx::rhi::Extent2d size) noexcept;

    [[nodiscard]] inline auto pixels(MenuUnits units) const noexcept -> nx::Pixels {
        return scale.pixels(units);
    }
    [[nodiscard]] inline auto header() const noexcept -> const nx::ui::PixelRect& { return headerRect; }
    /// The line at the foot of what heads the menu.
    [[nodiscard]] auto rule() const noexcept -> nx::ui::PixelRect;
    /// The row at `index`, from the top.
    [[nodiscard]] auto row(std::size_t index) const noexcept -> nx::ui::PixelRect;
    /// The row of `count` the point at `x`, `y` is over; none when it is over none.
    [[nodiscard]] auto rowAt(float x, float y, std::size_t count) const noexcept -> std::optional<std::size_t>;
    /// What shows the row selected.
    [[nodiscard]] inline auto detail() const noexcept -> const nx::ui::PixelRect& { return detailRect; }
    /// Where why a team was refused is told: under the rows.
    [[nodiscard]] auto refusal() const noexcept -> nx::ui::PixelRect;

private:
    MenuScale scale;
    nx::ui::PixelRect headerRect{};
    nx::ui::PixelRect railRect{};
    nx::ui::PixelRect detailRect{};
};

/// The menus' state, as the keys and the mouse change it.
class TeamMenu {
public:
    explicit TeamMenu(TeamMenuActions asked) noexcept;

    [[nodiscard]] inline auto isOpen() const noexcept -> bool { return isShown; }
    [[nodiscard]] inline auto page() const noexcept -> TeamMenuPage { return shownPage; }

    /// Opens on the team menu: with 0 to cancel as `canCancel` (a player who chose a team already).
    void open(bool canCancel);
    void close() noexcept;

    /// What `event` does to the menu, open, on a screen of `size` with the mouse at `cursor`, whose rows are
    /// those of `rows`: a row's key chooses it, Up and Down select one, Enter and the left button choose the
    /// one selected, the mouse moved selects the one it is over, and Escape cancels the team menu when it may,
    /// or auto-selects a character. The menu takes every event while open.
    auto handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition cursor,
                nx::rhi::Extent2d size, std::span<const TeamMenuRow> rows) -> TeamMenuEffect;

    /// What it shows, writing `texts`, the characters of `characters`, for a player of `team`.
    [[nodiscard]] auto frameOf(const TeamMenuTexts& texts, const Characters& characters, Team team) const
        -> TeamMenuFrame;

private:
    /// Chooses the row of `key`, among `rows`; a key of none does nothing.
    auto choose(std::uint32_t key, std::span<const TeamMenuRow> rows) -> TeamMenuEffect;

    TeamMenuActions actions;
    bool isShown = false;
    bool isCancelable = false;
    TeamMenuPage shownPage{};
    Team characterTeam{};
    std::optional<std::size_t> selection{};
    std::string refused{};
};

} // namespace cs
