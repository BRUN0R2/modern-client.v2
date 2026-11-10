// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The main menu: what it lists with and without a map played, what its keys and its mouse select and
// choose, the page of a new game and the map it plays, and where its parts go on a screen.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.rhi;
import nx.ui;
import cs.shared;
import cs.client;

namespace {

using cs::MainMenuAction;
using cs::MainMenuListing;
using cs::MenuGame;
using nx::platform::Key;
using nx::platform::KeyChanged;
using nx::platform::MouseButton;
using nx::platform::MouseButtonChanged;
using nx::platform::MouseMoved;
using nx::platform::MousePosition;

constexpr nx::rhi::Extent2d kScreen{.width = 1920, .height = 1080};
/// Where no item is.
constexpr MousePosition kNowhere{.x = 1900.0F, .y = 10.0F};

/// More maps than the page's list shows at once on the screen.
constexpr std::size_t kManyMaps = 40;

/// The original's menu, as far as the project's goes: to the game and out of it over a map, a new game, and
/// out.
[[nodiscard]] auto lookOf() -> cs::MainMenuLook {
    return {.font = {.regular = "Barlow", .strong = "Barlow SemiBold"},
            .background = "resource/BackgroundLayout.txt",
            .items = {{.action = MainMenuAction::ResumeGame, .listed = MainMenuListing::InGame},
                      {.action = MainMenuAction::Disconnect, .listed = MainMenuListing::InGame},
                      {.action = MainMenuAction::NewGame, .listed = MainMenuListing::Always},
                      {.action = MainMenuAction::Quit, .listed = MainMenuListing::Always}}};
}

/// `count` maps, named by their place: map00, map01...
[[nodiscard]] auto mapsOf(std::size_t count) -> std::vector<std::string> {
    return std::views::iota(std::size_t{0}, count) |
           std::views::transform([](std::size_t index) { return std::format("map{:02}", index); }) |
           std::ranges::to<std::vector>();
}

/// The action `menu` asks for of `event`, whatever its map.
[[nodiscard]] auto asked(cs::MainMenu& menu, const nx::platform::WindowEvent& event, MousePosition cursor,
                         nx::rhi::Extent2d size, MenuGame game) -> std::optional<MainMenuAction> {
    return menu.handle(event, cursor, size, game).transform([](const cs::MainMenuRequest& request) {
        return request.action;
    });
}

/// The middle of `rect`.
[[nodiscard]] auto middleOf(const nx::ui::PixelRect& rect) -> MousePosition {
    return {.x = static_cast<float>((rect.x + (rect.wide / 2)).value()),
            .y = static_cast<float>((rect.y + (rect.tall / 2)).value())};
}

/// The middle of the item at `index` of `count` on the screen.
[[nodiscard]] auto middleOf(std::size_t index, std::size_t count) -> MousePosition {
    return middleOf(cs::MainMenuLayout{kScreen, count}.item(index));
}

[[nodiscard]] auto pressed(Key key, bool isRepeat = false) -> nx::platform::WindowEvent {
    return KeyChanged{.key = key, .isDown = true, .isRepeat = isRepeat};
}

TEST(MainMenuTest, AMapPlayedListsItsItemsToo) {
    const cs::MainMenuLook look = lookOf();
    EXPECT_EQ(cs::mainMenuItemsOf(look, MenuGame::None),
              (std::vector{MainMenuAction::NewGame, MainMenuAction::Quit}));
    EXPECT_EQ(cs::mainMenuItemsOf(look, MenuGame::Played),
              (std::vector{MainMenuAction::ResumeGame, MainMenuAction::Disconnect, MainMenuAction::NewGame,
                           MainMenuAction::Quit}));
}

TEST(MainMenuTest, OpensOnItsFirstItem) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu{look, mapsOf(kManyMaps), {}};
    EXPECT_FALSE(menu.isOpen());
    menu.open(MenuGame::Played);
    EXPECT_TRUE(menu.isOpen());
    EXPECT_EQ(menu.frameOf(MenuGame::Played, kScreen).selected, MainMenuAction::ResumeGame);
    menu.close();
    EXPECT_FALSE(menu.isOpen());
    menu.open(MenuGame::None);
    EXPECT_EQ(menu.frameOf(MenuGame::None, kScreen).selected, MainMenuAction::NewGame);
}

TEST(MainMenuTest, TheArrowsGoAroundTheItems) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu{look, mapsOf(kManyMaps), {}};
    menu.open(MenuGame::Played);
    const auto after = [&](Key key) {
        EXPECT_EQ(asked(menu, pressed(key), kNowhere, kScreen, MenuGame::Played), std::nullopt);
        return menu.selected();
    };
    EXPECT_EQ(after(Key::Down), MainMenuAction::Disconnect);
    EXPECT_EQ(after(Key::Down), MainMenuAction::NewGame);
    EXPECT_EQ(after(Key::Down), MainMenuAction::Quit);
    EXPECT_EQ(after(Key::Down), MainMenuAction::ResumeGame) << "around the foot";
    EXPECT_EQ(after(Key::Up), MainMenuAction::Quit) << "around the top";
    EXPECT_EQ(asked(menu, pressed(Key::Up, true), kNowhere, kScreen, MenuGame::Played), std::nullopt);
    EXPECT_EQ(menu.selected(), MainMenuAction::NewGame) << "a key held goes on";
}

TEST(MainMenuTest, EnterChoosesWhatIsSelected) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu{look, mapsOf(kManyMaps), {}};
    menu.open(MenuGame::Played);
    std::ignore = asked(menu, pressed(Key::Down), kNowhere, kScreen, MenuGame::Played);
    EXPECT_EQ(asked(menu, pressed(Key::Enter, true), kNowhere, kScreen, MenuGame::Played), std::nullopt)
        << "a key held chooses nothing more";
    EXPECT_EQ(asked(menu, pressed(Key::Enter), kNowhere, kScreen, MenuGame::Played),
              MainMenuAction::Disconnect);
    EXPECT_EQ(asked(menu, pressed(Key::KeypadEnter), kNowhere, kScreen, MenuGame::Played),
              MainMenuAction::Disconnect);
    EXPECT_TRUE(menu.isOpen()) << "whoever runs it does what the item does";
}

TEST(MainMenuTest, TheMapLeftItsItemsAreNoLongerChosen) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu{look, mapsOf(kManyMaps), {}};
    menu.open(MenuGame::Played);
    std::ignore = asked(menu, pressed(Key::Down), kNowhere, kScreen, MenuGame::Played);
    ASSERT_EQ(menu.selected(), MainMenuAction::Disconnect);
    EXPECT_EQ(menu.frameOf(MenuGame::None, kScreen).selected, std::nullopt);
    EXPECT_EQ(asked(menu, pressed(Key::Enter), kNowhere, kScreen, MenuGame::None), std::nullopt);
    std::ignore = asked(menu, pressed(Key::Down), kNowhere, kScreen, MenuGame::None);
    EXPECT_EQ(menu.selected(), MainMenuAction::NewGame) << "the first of those listed now";
}

TEST(MainMenuTest, EscapeGoesBackToTheMapPlayed) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu{look, mapsOf(kManyMaps), {}};
    menu.open(MenuGame::Played);
    EXPECT_EQ(asked(menu, pressed(Key::Escape), kNowhere, kScreen, MenuGame::Played),
              MainMenuAction::ResumeGame);
    EXPECT_EQ(asked(menu, pressed(Key::Escape, true), kNowhere, kScreen, MenuGame::Played), std::nullopt);
    EXPECT_EQ(asked(menu, pressed(Key::Escape), kNowhere, kScreen, MenuGame::None), std::nullopt)
        << "without a map there is nowhere to go back to";
    EXPECT_EQ(
        asked(menu, KeyChanged{.key = Key::Enter, .isDown = false}, kNowhere, kScreen, MenuGame::Played),
        std::nullopt)
        << "a key coming up does nothing";
}

TEST(MainMenuTest, TheMouseSelectsAndChooses) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu{look, mapsOf(kManyMaps), {}};
    menu.open(MenuGame::Played);
    constexpr std::size_t kItems = 4;
    EXPECT_EQ(asked(menu, MouseMoved{}, middleOf(3, kItems), kScreen, MenuGame::Played), std::nullopt);
    EXPECT_EQ(menu.selected(), MainMenuAction::Quit);
    EXPECT_EQ(asked(menu, MouseMoved{}, kNowhere, kScreen, MenuGame::Played), std::nullopt);
    EXPECT_EQ(menu.selected(), MainMenuAction::Quit) << "off the items, what was selected stays";

    const auto clicked = [&](MouseButton button, bool isDown, MousePosition cursor) {
        return asked(menu, MouseButtonChanged{.button = button, .isDown = isDown}, cursor, kScreen,
                     MenuGame::Played);
    };
    EXPECT_EQ(clicked(MouseButton::Left, true, middleOf(1, kItems)), MainMenuAction::Disconnect);
    EXPECT_EQ(menu.selected(), MainMenuAction::Disconnect);
    EXPECT_EQ(clicked(MouseButton::Left, false, middleOf(1, kItems)), std::nullopt) << "coming up";
    EXPECT_EQ(clicked(MouseButton::Right, true, middleOf(1, kItems)), std::nullopt);
    EXPECT_EQ(clicked(MouseButton::Left, true, kNowhere), std::nullopt);
}

/// The menu of `look` open without a map on the page of a new game, on `maps` maps.
[[nodiscard]] auto onNewGame(const cs::MainMenuLook& look, std::size_t maps) -> cs::MainMenu {
    cs::MainMenu menu{look, mapsOf(maps), {}};
    menu.open(MenuGame::None);
    EXPECT_EQ(menu.handle(pressed(Key::Enter), kNowhere, kScreen, MenuGame::None), std::nullopt)
        << "the item opens its page, and asks for nothing yet";
    EXPECT_TRUE(menu.showsNewGame());
    return menu;
}

/// The page `menu` shows without a map.
[[nodiscard]] auto pageOf(const cs::MainMenu& menu) -> cs::NewGamePage {
    return menu.frameOf(MenuGame::None, kScreen).newGame.value_or(cs::NewGamePage{});
}

TEST(MainMenuNewGameTest, ItsItemOpensThePageOnTheFirstMaps) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu{look, mapsOf(kManyMaps), {}};
    menu.open(MenuGame::None);
    EXPECT_EQ(menu.frameOf(MenuGame::None, kScreen).newGame, std::nullopt);
    menu = onNewGame(look, kManyMaps);
    const cs::NewGamePage page = pageOf(menu);
    const std::size_t rows = cs::MainMenuLayout{kScreen, 2}.listRows();
    ASSERT_LT(rows, kManyMaps);
    EXPECT_EQ(page.rows.size(), rows);
    EXPECT_EQ(page.rows.front(), "map00");
    EXPECT_EQ(page.selectedRow, 0U);
    EXPECT_EQ(page.first, 0U);
    EXPECT_EQ(page.count, kManyMaps);
    EXPECT_EQ(menu.frameOf(MenuGame::None, kScreen).selected, MainMenuAction::NewGame);
}

TEST(MainMenuNewGameTest, EnterPlaysTheMapSelected) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu = onNewGame(look, kManyMaps);
    const auto press = [&](Key key, bool isRepeat = false) {
        return menu.handle(pressed(key, isRepeat), kNowhere, kScreen, MenuGame::None);
    };
    EXPECT_EQ(press(Key::Down), std::nullopt);
    EXPECT_EQ(press(Key::Down), std::nullopt);
    EXPECT_EQ(press(Key::Enter, true), std::nullopt) << "a key held plays nothing";
    EXPECT_EQ(press(Key::Enter), (cs::MainMenuRequest{.action = MainMenuAction::NewGame, .map = "map02"}));
    EXPECT_TRUE(menu.isOpen()) << "whoever runs it plays the map";
}

TEST(MainMenuNewGameTest, TheKeysWalkTheListAndItFollows) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu = onNewGame(look, kManyMaps);
    const std::size_t rows = cs::MainMenuLayout{kScreen, 2}.listRows();
    const auto after = [&](Key key) {
        EXPECT_EQ(menu.handle(pressed(key), kNowhere, kScreen, MenuGame::None), std::nullopt);
        return pageOf(menu);
    };
    EXPECT_EQ(after(Key::Up).selectedRow, 0U) << "the first stays the first";
    cs::NewGamePage page = after(Key::PageDown);
    EXPECT_EQ(page.first, 1U) << "scrolled as far as shows the map selected";
    EXPECT_EQ(page.selectedRow, rows - 1);
    EXPECT_EQ(page.rows.back(), std::format("map{:02}", rows));
    page = after(Key::End);
    EXPECT_EQ(page.first, kManyMaps - rows);
    EXPECT_EQ(page.rows.back(), std::format("map{:02}", kManyMaps - 1));
    EXPECT_EQ(page.selectedRow, rows - 1);
    EXPECT_EQ(after(Key::Down).selectedRow, rows - 1) << "the last stays the last";
    page = after(Key::PageUp);
    EXPECT_EQ(page.first, kManyMaps - rows - 1) << "scrolled up as far as shows it";
    EXPECT_EQ(page.selectedRow, 0U);
    page = after(Key::Home);
    EXPECT_EQ(page.first, 0U);
    EXPECT_EQ(page.selectedRow, 0U);
}

TEST(MainMenuNewGameTest, TheWheelScrollsTheListToItsEnds) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu = onNewGame(look, kManyMaps);
    const std::size_t rows = cs::MainMenuLayout{kScreen, 2}.listRows();
    const auto after = [&](float notches) {
        EXPECT_EQ(
            menu.handle(nx::platform::MouseWheelMoved{.dy = notches}, kNowhere, kScreen, MenuGame::None),
            std::nullopt);
        return pageOf(menu);
    };
    EXPECT_EQ(after(1.0F).first, 0U) << "no further up than its top";
    cs::NewGamePage page = after(-1.0F);
    EXPECT_EQ(page.first, 3U);
    EXPECT_EQ(page.selectedRow, std::nullopt) << "the map selected scrolled out of sight";
    EXPECT_EQ(after(-100.0F).first, kManyMaps - rows) << "no further down than its foot";
}

TEST(MainMenuNewGameTest, TheMouseSelectsAMapAndItsButtonsPlayItOrGoBack) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu = onNewGame(look, kManyMaps);
    const cs::MainMenuLayout layout{kScreen, 2};
    const auto clickAt = [&](const nx::ui::PixelRect& rect) {
        return menu.handle(MouseButtonChanged{.button = MouseButton::Left, .isDown = true}, middleOf(rect),
                           kScreen, MenuGame::None);
    };
    EXPECT_EQ(clickAt(layout.listRow(4)), std::nullopt);
    EXPECT_EQ(pageOf(menu).selectedRow, 4U);
    EXPECT_EQ(clickAt(layout.confirmButton()),
              (cs::MainMenuRequest{.action = MainMenuAction::NewGame, .map = "map04"}));
    EXPECT_EQ(clickAt(layout.backButton()), std::nullopt);
    EXPECT_FALSE(menu.showsNewGame());
    EXPECT_TRUE(menu.isOpen());
}

TEST(MainMenuNewGameTest, EscapeGoesBackToTheItemsAndAnotherItemLeavesThePage) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu = onNewGame(look, kManyMaps);
    EXPECT_EQ(menu.handle(pressed(Key::Escape), kNowhere, kScreen, MenuGame::None), std::nullopt);
    EXPECT_FALSE(menu.showsNewGame());
    EXPECT_EQ(menu.selected(), MainMenuAction::NewGame);

    menu = onNewGame(look, kManyMaps);
    EXPECT_EQ(menu.handle(MouseMoved{}, middleOf(1, 2), kScreen, MenuGame::None), std::nullopt);
    EXPECT_EQ(menu.selected(), MainMenuAction::NewGame) << "the item whose page is open stays selected";
    EXPECT_EQ(asked(menu, MouseButtonChanged{.button = MouseButton::Left, .isDown = true}, middleOf(1, 2),
                    kScreen, MenuGame::None),
              MainMenuAction::Quit);
    EXPECT_FALSE(menu.showsNewGame());

    menu = onNewGame(look, kManyMaps);
    menu.open(MenuGame::None);
    EXPECT_FALSE(menu.showsNewGame()) << "it opens on its items";
}

TEST(MainMenuNewGameTest, WithoutMapsThereIsNothingToPlay) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu = onNewGame(look, 0);
    EXPECT_EQ(menu.handle(pressed(Key::Down), kNowhere, kScreen, MenuGame::None), std::nullopt);
    EXPECT_EQ(menu.handle(pressed(Key::Enter), kNowhere, kScreen, MenuGame::None), std::nullopt);
    const cs::NewGamePage page = pageOf(menu);
    EXPECT_TRUE(page.rows.empty());
    EXPECT_EQ(page.selectedRow, std::nullopt);
    EXPECT_EQ((cs::MainMenuLayout{kScreen, 2}.listThumb(0, 0)), std::nullopt);
}

TEST(MainMenuLayoutTest, ThePageStandsBesideTheItemsOnAnyScreen) {
    for (const nx::rhi::Extent2d size : {nx::rhi::Extent2d{.width = 640, .height = 480}, kScreen,
                                         nx::rhi::Extent2d{.width = 3440, .height = 1440}}) {
        constexpr std::size_t kItems = 4;
        const cs::MainMenuLayout layout{size, kItems};
        const nx::ui::PixelRect& page = layout.page();
        const nx::ui::PixelRect item = layout.item(kItems - 1);
        EXPECT_GT(page.x, item.x + item.wide) << size.width;
        EXPECT_LT(page.x + page.wide, nx::Pixels{nx::narrow<std::int32_t>(size.width)});
        EXPECT_EQ(page.y + page.tall, item.y + item.tall) << "its foot the last item's";
        const std::size_t rows = layout.listRows();
        ASSERT_GE(rows, 1U);
        const nx::ui::PixelRect lastRow = layout.listRow(rows - 1);
        EXPECT_LE(lastRow.y + lastRow.tall, layout.confirmButton().y) << "the list over the buttons";
        EXPECT_LT(layout.backButton().x + layout.backButton().wide, layout.confirmButton().x + nx::Pixels{1});
        EXPECT_GE(layout.backButton().x, page.x);
        const MousePosition middle = middleOf(lastRow);
        EXPECT_EQ(layout.listRowAt(middle.x, middle.y), rows - 1);
    }
}

TEST(MainMenuLayoutTest, TheThumbStandsForTheRowsShown) {
    const cs::MainMenuLayout layout{kScreen, 2};
    const std::size_t rows = layout.listRows();
    EXPECT_EQ(layout.listThumb(0, rows), std::nullopt) << "all of them show";
    const nx::ui::PixelRect top = layout.listRow(0);
    const nx::ui::PixelRect foot = layout.listRow(rows - 1);
    const auto first = layout.listThumb(0, rows * 2);
    const auto last = layout.listThumb(rows, rows * 2);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(first->y, top.y);
    EXPECT_EQ(first->tall, (foot.y + foot.tall - top.y) / 2) << "half the rows, half the track";
    EXPECT_EQ(last->y + last->tall, foot.y + foot.tall);
}

TEST(MainMenuLayoutTest, TheItemsStandOverTheFootOfAnyScreen) {
    for (const nx::rhi::Extent2d size : {nx::rhi::Extent2d{.width = 640, .height = 480}, kScreen,
                                         nx::rhi::Extent2d{.width = 3440, .height = 1440}}) {
        constexpr std::size_t kItems = 3;
        const cs::MainMenuLayout layout{size, kItems};
        const nx::Pixels tall{nx::narrow<std::int32_t>(size.height)};
        const nx::ui::PixelRect last = layout.item(kItems - 1);
        EXPECT_LT(last.y + last.tall, tall) << size.height;
        EXPECT_EQ(layout.item(0).x, layout.title().x) << "under the game's name";
        EXPECT_GT(layout.item(0).y, layout.title().y);
        EXPECT_GT(layout.hints().y, last.y + last.tall);
        for (std::size_t index = 1; index < kItems; ++index) {
            const nx::ui::PixelRect before = layout.item(index - 1);
            EXPECT_GT(layout.item(index).y, before.y + before.tall - nx::Pixels{1}) << "none over another";
        }
        EXPECT_EQ(
            layout.screen(),
            (nx::ui::PixelRect{.wide = nx::Pixels{nx::narrow<std::int32_t>(size.width)}, .tall = tall}));
    }
}

TEST(MainMenuLayoutTest, ThePointOverAnItemFindsIt) {
    constexpr std::size_t kItems = 3;
    const cs::MainMenuLayout layout{kScreen, kItems};
    for (std::size_t index = 0; index < kItems; ++index) {
        const MousePosition middle = middleOf(index, kItems);
        EXPECT_EQ(layout.itemAt(middle.x, middle.y), index);
    }
    EXPECT_EQ(layout.itemAt(kNowhere.x, kNowhere.y), std::nullopt);
    const nx::ui::PixelRect first = layout.item(0);
    EXPECT_EQ(layout.itemAt(static_cast<float>(first.x.value()),
                            static_cast<float>((first.y + first.tall).value())),
              std::nullopt)
        << "between two items, neither";
}

} // namespace
