// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The main menu (docs/12-ui-hud-and-console.md, section 4), the project's own look over the original's
/// pattern: the items data/cstrike/mainMenu.toml lists, those of a map played only over one, chosen by the
/// arrows and Enter or by the mouse, Escape going back to the map played; and the page of a new game, the
/// maps to play, beside them. Here is its state as the keys and the mouse change it, and what it shows
/// (MainMenuFrame); drawing it is apart (cs.client:mainMenuPanel), and so is what its items do, which
/// whoever runs it does with what the menu asks for.
export module cs.client:mainMenu;

import std;
import nx.core;
import nx.rhi;
import nx.platform;
import cs.shared;
import :mainMenuLayout;
import :optionsPage;

export namespace cs {

/// Whether a map is played under the main menu.
enum class MenuGame : std::uint8_t {
    None,
    Played,
};

/// What the menu of `look` lists while `game`, from the top.
[[nodiscard]] auto mainMenuItemsOf(const MainMenuLook& look, MenuGame game) -> std::vector<MainMenuAction>;

/// What the menu asks of whoever runs it: what an item does, and for a new game the map to play, one of
/// the menu's.
struct MainMenuRequest {
    MainMenuAction action{};
    std::string_view map{};

    [[nodiscard]] inline auto operator==(const MainMenuRequest&) const noexcept -> bool = default;
};

/// The page of a new game as it shows: the maps on the rows its list shows, from the one at `first` of all
/// `count`, and the row of the one selected, when it is among them.
struct NewGamePage {
    std::vector<std::string_view> rows{};
    std::optional<std::size_t> selectedRow{};
    std::size_t first = 0;
    std::size_t count = 0;

    [[nodiscard]] inline auto operator==(const NewGamePage&) const noexcept -> bool = default;
};

/// All the menu shows: whether over a map played, its items from the top, the one selected (none while none
/// of those listed is), and the page of a new game or of the options while it is open.
struct MainMenuFrame {
    MenuGame game{};
    std::vector<MainMenuAction> items{};
    std::optional<MainMenuAction> selected{};
    std::optional<NewGamePage> newGame{};
    std::optional<OptionsFrame> options{};

    [[nodiscard]] inline auto operator==(const MainMenuFrame&) const noexcept -> bool = default;
};

/// The menu's state, as the keys and the mouse change it: whether it is open, the item selected, the page
/// open beside the items, and on that of a new game the map selected and how far its list is scrolled.
class MainMenu {
public:
    /// The menu of what `listed` lists, which must outlive it, a new game played on one of `playable`, the
    /// names of the game's maps in the order its page lists them, and its options those of `settings`.
    MainMenu(const MainMenuLook& listed, std::vector<std::string> playable,
             std::vector<OptionRow> settings) noexcept;

    [[nodiscard]] inline auto isOpen() const noexcept -> bool { return isShown; }
    [[nodiscard]] inline auto selected() const noexcept -> const std::optional<MainMenuAction>& {
        return selection;
    }
    /// Whether the page of a new game is open.
    [[nodiscard]] inline auto showsNewGame() const noexcept -> bool { return page == Page::NewGame; }
    /// Whether the page of the options is open.
    [[nodiscard]] inline auto showsOptions() const noexcept -> bool { return page == Page::Options; }

    /// Opens on its items, the first listed while `game` selected.
    void open(MenuGame game);
    void close() noexcept;

    /// What `event` does to the menu, open, while `game`, on a screen of `size` with the mouse's cursor at
    /// `cursor`. The mouse moved selects the item it is over, and its left button chooses it. On the items,
    /// Up and Down select the item before and the one after, around its ends; Enter chooses the one selected;
    /// Escape chooses to go back to the map played, when the menu lists that. The item of a new game opens
    /// its page, where Up, Down, Page Up, Page Down, Home and End select a map, the wheel scrolls the list,
    /// the mouse selects the map it is pressed on, Enter and the page's button play the map selected, and
    /// Escape and its other button go back to the items. The item of the options opens theirs, which takes
    /// the keys and the mouse off the items (OptionsPage::handle). Returns what the menu asks for, if
    /// anything: the menu stays as it is, whoever runs it doing that.
    [[nodiscard]] auto handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition cursor,
                              nx::rhi::Extent2d size, MenuGame game) -> std::optional<MainMenuRequest>;

    /// What it shows while `game` on a screen of `size`.
    [[nodiscard]] auto frameOf(MenuGame game, nx::rhi::Extent2d size) const -> MainMenuFrame;

private:
    /// Which way the arrows move what is selected.
    enum class Step : std::uint8_t {
        Before,
        After,
    };
    /// What is open beside the items.
    enum class Page : std::uint8_t {
        Items, ///< Nothing: the keys are the items'.
        NewGame,
        Options,
    };

    /// Goes back to the items when the options ask for it.
    void leaveOptionsIf(OptionsOutcome outcome) noexcept;

    /// Selects the item `step` of the one selected among `items`, around their ends; the first or the last
    /// when none of them is.
    void select(std::span<const MainMenuAction> items, Step step);
    /// What choosing `action` asks for: nothing of the item of a new game, which opens its page.
    [[nodiscard]] auto choose(MainMenuAction action) -> std::optional<MainMenuRequest>;
    /// What the left button going down at `cursor` does (handle).
    [[nodiscard]] auto click(nx::platform::MousePosition cursor, const MainMenuLayout& layout,
                             std::span<const MainMenuAction> items) -> std::optional<MainMenuRequest>;
    /// What a key going down does on the items (handle).
    [[nodiscard]] auto pressOnItems(const nx::platform::KeyChanged& key,
                                    std::span<const MainMenuAction> items) -> std::optional<MainMenuRequest>;
    /// What a key going down does on the page of a new game, its list `rows` rows tall (handle).
    [[nodiscard]] auto pressOnNewGame(const nx::platform::KeyChanged& key, std::size_t rows)
        -> std::optional<MainMenuRequest>;
    /// Selects the map at `index`, or the last when there are fewer, and scrolls the list of `rows` rows so
    /// that it shows.
    void selectMap(std::size_t index, std::size_t rows) noexcept;
    /// Scrolls the list of `rows` rows by `by` of them, down when more than none, as far as its ends.
    void scrollMaps(std::ptrdiff_t by, std::size_t rows) noexcept;
    /// The map on the first row of the list of `rows` rows: as far as it was scrolled, or as far as a list
    /// of that many rows goes, when the screen changed size since.
    [[nodiscard]] auto firstMapShownOf(std::size_t rows) const noexcept -> std::size_t;
    /// A new game on the map selected; none without maps.
    [[nodiscard]] auto playSelectedMap() const -> std::optional<MainMenuRequest>;

    const MainMenuLook* look;
    std::vector<std::string> maps;
    bool isShown = false;
    std::optional<MainMenuAction> selection;
    Page page = Page::Items;
    std::size_t selectedMap = 0;
    std::size_t firstMapShown = 0;
    OptionsPage options;
};

} // namespace cs
