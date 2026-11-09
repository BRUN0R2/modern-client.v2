// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The main menu: what it lists with and without a map played, what its keys and its mouse select and
// choose, and where its parts go on a screen.

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

/// The original's menu, as far as the project's goes: to the game and out of it over a map, and out.
[[nodiscard]] auto lookOf() -> cs::MainMenuLook {
    return {.font = {.regular = "Barlow", .strong = "Barlow SemiBold"},
            .items = {{.action = MainMenuAction::ResumeGame, .listed = MainMenuListing::InGame},
                      {.action = MainMenuAction::Disconnect, .listed = MainMenuListing::InGame},
                      {.action = MainMenuAction::Quit, .listed = MainMenuListing::Always}}};
}

/// The middle of the item at `index` of `count` on the screen.
[[nodiscard]] auto middleOf(std::size_t index, std::size_t count) -> MousePosition {
    const nx::ui::PixelRect rect = cs::MainMenuLayout{kScreen, count}.item(index);
    return {.x = static_cast<float>((rect.x + (rect.wide / 2)).value()),
            .y = static_cast<float>((rect.y + (rect.tall / 2)).value())};
}

[[nodiscard]] auto pressed(Key key, bool isRepeat = false) -> nx::platform::WindowEvent {
    return KeyChanged{.key = key, .isDown = true, .isRepeat = isRepeat};
}

TEST(MainMenuTest, AMapPlayedListsItsItemsToo) {
    const cs::MainMenuLook look = lookOf();
    EXPECT_EQ(cs::mainMenuItemsOf(look, MenuGame::None), std::vector{MainMenuAction::Quit});
    EXPECT_EQ(cs::mainMenuItemsOf(look, MenuGame::Played),
              (std::vector{MainMenuAction::ResumeGame, MainMenuAction::Disconnect, MainMenuAction::Quit}));
}

TEST(MainMenuTest, OpensOnItsFirstItem) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu{look};
    EXPECT_FALSE(menu.isOpen());
    menu.open(MenuGame::Played);
    EXPECT_TRUE(menu.isOpen());
    EXPECT_EQ(menu.frameOf(MenuGame::Played).selected, MainMenuAction::ResumeGame);
    menu.close();
    EXPECT_FALSE(menu.isOpen());
    menu.open(MenuGame::None);
    EXPECT_EQ(menu.frameOf(MenuGame::None).selected, MainMenuAction::Quit);
}

TEST(MainMenuTest, TheArrowsGoAroundTheItems) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu{look};
    menu.open(MenuGame::Played);
    const auto after = [&](Key key) {
        EXPECT_EQ(menu.handle(pressed(key), kNowhere, kScreen, MenuGame::Played), std::nullopt);
        return menu.selected();
    };
    EXPECT_EQ(after(Key::Down), MainMenuAction::Disconnect);
    EXPECT_EQ(after(Key::Down), MainMenuAction::Quit);
    EXPECT_EQ(after(Key::Down), MainMenuAction::ResumeGame) << "around the foot";
    EXPECT_EQ(after(Key::Up), MainMenuAction::Quit) << "around the top";
    EXPECT_EQ(menu.handle(pressed(Key::Up, true), kNowhere, kScreen, MenuGame::Played), std::nullopt);
    EXPECT_EQ(menu.selected(), MainMenuAction::Disconnect) << "a key held goes on";
}

TEST(MainMenuTest, EnterChoosesWhatIsSelected) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu{look};
    menu.open(MenuGame::Played);
    std::ignore = menu.handle(pressed(Key::Down), kNowhere, kScreen, MenuGame::Played);
    EXPECT_EQ(menu.handle(pressed(Key::Enter, true), kNowhere, kScreen, MenuGame::Played), std::nullopt)
        << "a key held chooses nothing more";
    EXPECT_EQ(menu.handle(pressed(Key::Enter), kNowhere, kScreen, MenuGame::Played),
              MainMenuAction::Disconnect);
    EXPECT_EQ(menu.handle(pressed(Key::KeypadEnter), kNowhere, kScreen, MenuGame::Played),
              MainMenuAction::Disconnect);
    EXPECT_TRUE(menu.isOpen()) << "whoever runs it does what the item does";
}

TEST(MainMenuTest, TheMapLeftItsItemsAreNoLongerChosen) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu{look};
    menu.open(MenuGame::Played);
    std::ignore = menu.handle(pressed(Key::Down), kNowhere, kScreen, MenuGame::Played);
    ASSERT_EQ(menu.selected(), MainMenuAction::Disconnect);
    EXPECT_EQ(menu.frameOf(MenuGame::None).selected, std::nullopt);
    EXPECT_EQ(menu.handle(pressed(Key::Enter), kNowhere, kScreen, MenuGame::None), std::nullopt);
    std::ignore = menu.handle(pressed(Key::Down), kNowhere, kScreen, MenuGame::None);
    EXPECT_EQ(menu.selected(), MainMenuAction::Quit) << "the first of those listed now";
}

TEST(MainMenuTest, EscapeGoesBackToTheMapPlayed) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu{look};
    menu.open(MenuGame::Played);
    EXPECT_EQ(menu.handle(pressed(Key::Escape), kNowhere, kScreen, MenuGame::Played),
              MainMenuAction::ResumeGame);
    EXPECT_EQ(menu.handle(pressed(Key::Escape, true), kNowhere, kScreen, MenuGame::Played), std::nullopt);
    EXPECT_EQ(menu.handle(pressed(Key::Escape), kNowhere, kScreen, MenuGame::None), std::nullopt)
        << "without a map there is nowhere to go back to";
    EXPECT_EQ(
        menu.handle(KeyChanged{.key = Key::Enter, .isDown = false}, kNowhere, kScreen, MenuGame::Played),
        std::nullopt)
        << "a key coming up does nothing";
}

TEST(MainMenuTest, TheMouseSelectsAndChooses) {
    const cs::MainMenuLook look = lookOf();
    cs::MainMenu menu{look};
    menu.open(MenuGame::Played);
    constexpr std::size_t kItems = 3;
    EXPECT_EQ(menu.handle(MouseMoved{}, middleOf(2, kItems), kScreen, MenuGame::Played), std::nullopt);
    EXPECT_EQ(menu.selected(), MainMenuAction::Quit);
    EXPECT_EQ(menu.handle(MouseMoved{}, kNowhere, kScreen, MenuGame::Played), std::nullopt);
    EXPECT_EQ(menu.selected(), MainMenuAction::Quit) << "off the items, what was selected stays";

    const auto clicked = [&](MouseButton button, bool isDown, MousePosition cursor) {
        return menu.handle(MouseButtonChanged{.button = button, .isDown = isDown}, cursor, kScreen,
                           MenuGame::Played);
    };
    EXPECT_EQ(clicked(MouseButton::Left, true, middleOf(1, kItems)), MainMenuAction::Disconnect);
    EXPECT_EQ(menu.selected(), MainMenuAction::Disconnect);
    EXPECT_EQ(clicked(MouseButton::Left, false, middleOf(1, kItems)), std::nullopt) << "coming up";
    EXPECT_EQ(clicked(MouseButton::Right, true, middleOf(1, kItems)), std::nullopt);
    EXPECT_EQ(clicked(MouseButton::Left, true, kNowhere), std::nullopt);
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
