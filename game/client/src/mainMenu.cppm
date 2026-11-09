// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The main menu (docs/12-ui-hud-and-console.md, section 4), the project's own look over the original's
/// pattern: the items data/cstrike/mainMenu.toml lists, those of a map played only over one, chosen by the
/// arrows and Enter or by the mouse, Escape going back to the map played. Here is its state as the keys and
/// the mouse change it, and what it shows (MainMenuFrame); drawing it is apart (cs.client:mainMenuPanel), and
/// so is what its items do, which whoever runs it does with the action chosen.
export module cs.client:mainMenu;

import std;
import nx.core;
import nx.rhi;
import nx.platform;
import cs.shared;
import :mainMenuLayout;

export namespace cs {

/// Whether a map is played under the main menu.
enum class MenuGame : std::uint8_t {
    None,
    Played,
};

/// What the menu of `look` lists while `game`, from the top.
[[nodiscard]] auto mainMenuItemsOf(const MainMenuLook& look, MenuGame game) -> std::vector<MainMenuAction>;

/// All the menu shows: whether over a map played, its items from the top, and the one selected; none while
/// none of those listed is.
struct MainMenuFrame {
    MenuGame game{};
    std::vector<MainMenuAction> items{};
    std::optional<MainMenuAction> selected{};

    [[nodiscard]] inline auto operator==(const MainMenuFrame&) const noexcept -> bool = default;
};

/// The menu's state, as the keys and the mouse change it: whether it is open, and the item selected.
class MainMenu {
public:
    /// The menu of what `listed` lists, which must outlive it.
    explicit MainMenu(const MainMenuLook& listed) noexcept;

    [[nodiscard]] inline auto isOpen() const noexcept -> bool { return isShown; }
    [[nodiscard]] inline auto selected() const noexcept -> const std::optional<MainMenuAction>& {
        return selection;
    }

    /// Opens, the first item listed while `game` selected.
    void open(MenuGame game);
    void close() noexcept;

    /// What `event` does to the menu, open, while `game`, on a screen of `size` with the mouse's cursor at
    /// `cursor`: the mouse moved selects the item it is over, and its left button chooses it; Up and Down
    /// select the item before and the one after, around its ends; Enter chooses the one selected; Escape
    /// chooses to go back to the map played, when the menu lists that. Returns the item chosen, if any: the
    /// menu stays as it is, whoever runs it doing what the item does.
    [[nodiscard]] auto handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition cursor,
                              nx::rhi::Extent2d size, MenuGame game) -> std::optional<MainMenuAction>;

    /// What it shows while `game`.
    [[nodiscard]] auto frameOf(MenuGame game) const -> MainMenuFrame;

private:
    /// Which way the arrows move what is selected.
    enum class Step : std::uint8_t {
        Before,
        After,
    };

    /// Selects the item `step` of the one selected among `items`, around their ends; the first or the last
    /// when none of them is.
    void select(std::span<const MainMenuAction> items, Step step);
    /// What a key going down does (handle).
    [[nodiscard]] auto press(const nx::platform::KeyChanged& key, std::span<const MainMenuAction> items)
        -> std::optional<MainMenuAction>;

    const MainMenuLook* look;
    bool isShown = false;
    std::optional<MainMenuAction> selection;
};

} // namespace cs
