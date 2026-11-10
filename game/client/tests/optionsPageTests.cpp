// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The options of the main menu: their tabs and rows, what the keys and the mouse change, how a slider's
// value is written, and the page on the menu.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.rhi;
import nx.ui;
import cs.shared;
import cs.client;

namespace {

using cs::OptionCategory;
using cs::OptionsOutcome;
using cs::OptionsTexts;
using nx::platform::Key;
using nx::platform::KeyChanged;
using nx::platform::MouseButton;
using nx::platform::MouseButtonChanged;
using nx::platform::MouseMoved;
using nx::platform::MousePosition;

constexpr nx::rhi::Extent2d kScreen{.width = 1920, .height = 1080};
constexpr MousePosition kNowhere{.x = 1900.0F, .y = 10.0F};

/// What the options of the tests set.
struct Settings {
    float sensitivity = 3.0F;
    bool isMouseReversed = false;
    float volume = 0.5F;
};

/// The options over `settings`: a slider and a switch on the mouse's tab, a slider on the audio's.
[[nodiscard]] auto optionsOf(Settings& settings) -> std::vector<cs::OptionRow> {
    return {{.category = OptionCategory::Mouse,
             .name = &OptionsTexts::sensitivity,
             .control = cs::OptionSlider{.value = [&settings] { return settings.sensitivity; },
                                         .set = [&settings](float value) { settings.sensitivity = value; },
                                         .minimum = 1.0F,
                                         .maximum = 5.0F,
                                         .step = 0.5F}},
            {.category = OptionCategory::Mouse,
             .name = &OptionsTexts::reverseMouse,
             .control = cs::OptionSwitch{.isOn = [&settings] { return settings.isMouseReversed; },
                                         .set = [&settings](bool isOn) { settings.isMouseReversed = isOn; }}},
            {.category = OptionCategory::Audio,
             .name = &OptionsTexts::volume,
             .control = cs::OptionSlider{.value = [&settings] { return settings.volume; },
                                         .set = [&settings](float value) { settings.volume = value; },
                                         .minimum = 0.0F,
                                         .maximum = 1.0F,
                                         .step = 0.25F,
                                         .style = cs::OptionValueStyle::Percent}}};
}

[[nodiscard]] auto pressed(Key key, bool isRepeat = false) -> nx::platform::WindowEvent {
    return KeyChanged{.key = key, .isDown = true, .isRepeat = isRepeat};
}

[[nodiscard]] auto middleOf(const nx::ui::PixelRect& rect) -> MousePosition {
    return {.x = static_cast<float>((rect.x + (rect.wide / 2)).value()),
            .y = static_cast<float>((rect.y + (rect.tall / 2)).value())};
}

/// The options over `settings`, open, with what the tests send them.
struct Opened {
    explicit Opened(Settings& settings) : page{optionsOf(settings)} { page.open(); }

    auto press(Key key, bool isRepeat = false) -> OptionsOutcome {
        return page.handle(pressed(key, isRepeat), kNowhere, layout);
    }
    auto mouse(const nx::platform::WindowEvent& event, MousePosition cursor) -> OptionsOutcome {
        return page.handle(event, cursor, layout);
    }

    cs::MainMenuLayout layout{kScreen, 2};
    cs::OptionsPage page;
};

constexpr MouseButtonChanged kLeftDown{.button = MouseButton::Left, .isDown = true};
constexpr MouseButtonChanged kLeftUp{.button = MouseButton::Left, .isDown = false};

TEST(OptionsPageTest, OpensOnItsFirstTabWithTheTabsSelected) {
    Settings settings;
    Opened options{settings};
    const cs::OptionsFrame frame = options.page.frame();
    EXPECT_EQ(frame.category, OptionCategory::Mouse);
    EXPECT_EQ(frame.selectedRow, std::nullopt);
    ASSERT_EQ(frame.rows.size(), 2U);
    EXPECT_EQ(frame.rows[0].name, &OptionsTexts::sensitivity);
    const auto* const slider = std::get_if<cs::ShownSlider>(&frame.rows[0].control);
    ASSERT_NE(slider, nullptr);
    EXPECT_FLOAT_EQ(slider->value, 3.0F);
    EXPECT_FLOAT_EQ(slider->share, 0.5F) << "halfway from 1 to 5";
    EXPECT_EQ(frame.rows[1].control, (std::variant<cs::ShownSlider, cs::ShownSwitch>{cs::ShownSwitch{}}));
}

TEST(OptionsPageTest, LeftAndRightChangeTheTabWhileTheTabsAreSelected) {
    Settings settings;
    Opened options{settings};
    EXPECT_EQ(options.press(Key::Left), OptionsOutcome::Stays);
    EXPECT_EQ(options.page.frame().category, OptionCategory::Mouse) << "the first stays the first";
    std::ignore = options.press(Key::Right);
    const cs::OptionsFrame audio = options.page.frame();
    EXPECT_EQ(audio.category, OptionCategory::Audio);
    ASSERT_EQ(audio.rows.size(), 1U);
    EXPECT_EQ(audio.rows[0].name, &OptionsTexts::volume);
    for (int press = 0; press < 5; ++press) {
        std::ignore = options.press(Key::Right);
    }
    EXPECT_EQ(options.page.frame().category, OptionCategory::Game) << "the last stays the last";
    EXPECT_TRUE(options.page.frame().rows.empty());
    std::ignore = options.press(Key::Down);
    EXPECT_EQ(options.page.frame().selectedRow, std::nullopt) << "a tab of no rows selects none";
    EXPECT_FLOAT_EQ(settings.sensitivity, 3.0F);
}

TEST(OptionsPageTest, TheArrowsSelectARowAndChangeIt) {
    Settings settings;
    Opened options{settings};
    std::ignore = options.press(Key::Down);
    EXPECT_EQ(options.page.frame().selectedRow, 0U);
    std::ignore = options.press(Key::Right);
    EXPECT_FLOAT_EQ(settings.sensitivity, 3.5F) << "a step up";
    std::ignore = options.press(Key::Left);
    std::ignore = options.press(Key::Left, true);
    EXPECT_FLOAT_EQ(settings.sensitivity, 2.5F) << "a key held goes on";
    for (int press = 0; press < 10; ++press) {
        std::ignore = options.press(Key::Left);
    }
    EXPECT_FLOAT_EQ(settings.sensitivity, 1.0F) << "no lower than its least";
    EXPECT_EQ(options.page.frame().category, OptionCategory::Mouse) << "the tab stays, a row selected";

    std::ignore = options.press(Key::Down);
    std::ignore = options.press(Key::Down);
    EXPECT_EQ(options.page.frame().selectedRow, 1U) << "the last stays the last";
    std::ignore = options.press(Key::Right);
    EXPECT_TRUE(settings.isMouseReversed);
    std::ignore = options.press(Key::Left);
    EXPECT_FALSE(settings.isMouseReversed) << "either way switches it over";
    std::ignore = options.press(Key::Enter);
    EXPECT_TRUE(settings.isMouseReversed);
    std::ignore = options.press(Key::Enter, true);
    EXPECT_TRUE(settings.isMouseReversed) << "a key held switches nothing more";

    std::ignore = options.press(Key::Up);
    std::ignore = options.press(Key::Enter);
    EXPECT_FLOAT_EQ(settings.sensitivity, 1.0F) << "Enter leaves a slider as it is";
    std::ignore = options.press(Key::Up);
    EXPECT_EQ(options.page.frame().selectedRow, std::nullopt) << "the tabs are above the first row";
}

TEST(OptionsPageTest, TheMouseShowsATabSwitchesASwitchAndDragsASlider) {
    Settings settings;
    Opened options{settings};
    const cs::MainMenuLayout& layout = options.layout;
    EXPECT_EQ(options.mouse(MouseMoved{}, middleOf(layout.listRow(1))), OptionsOutcome::Stays);
    EXPECT_EQ(options.page.frame().selectedRow, 1U);
    std::ignore = options.mouse(kLeftDown, middleOf(layout.listRow(1)));
    EXPECT_TRUE(settings.isMouseReversed);
    std::ignore = options.mouse(kLeftUp, middleOf(layout.listRow(1)));
    EXPECT_TRUE(settings.isMouseReversed) << "coming up switches nothing";

    const nx::ui::PixelRect track = layout.optionTrack(0);
    const float trackY = middleOf(track).y;
    const auto at = [&](float share) {
        return MousePosition{.x = static_cast<float>(track.x.value()) +
                                  (share * static_cast<float>(track.wide.value())),
                             .y = trackY};
    };
    std::ignore = options.mouse(kLeftDown, at(1.0F));
    EXPECT_FLOAT_EQ(settings.sensitivity, 5.0F) << "set to where it is pressed";
    std::ignore = options.mouse(MouseMoved{}, at(0.26F));
    EXPECT_FLOAT_EQ(settings.sensitivity, 2.0F) << "and on as it moves, to the nearest step";
    std::ignore = options.mouse(MouseMoved{}, MousePosition{.x = 0.0F, .y = 0.0F});
    EXPECT_FLOAT_EQ(settings.sensitivity, 1.0F) << "held past its end, it stays at its least";
    std::ignore = options.mouse(kLeftUp, at(0.26F));
    std::ignore = options.mouse(MouseMoved{}, at(1.0F));
    EXPECT_FLOAT_EQ(settings.sensitivity, 1.0F) << "let go, it follows no more";
    std::ignore = options.mouse(kLeftDown, middleOf(layout.listRow(0)));
    EXPECT_FLOAT_EQ(settings.sensitivity, 1.0F) << "pressed on its name, it does not change";

    std::ignore = options.mouse(kLeftDown, middleOf(layout.optionTab(1)));
    EXPECT_EQ(options.page.frame().category, OptionCategory::Audio);
    EXPECT_EQ(options.page.frame().selectedRow, std::nullopt);
    std::ignore = options.mouse(MouseMoved{}, middleOf(layout.listRow(1)));
    EXPECT_EQ(options.page.frame().selectedRow, std::nullopt) << "a row the tab does not have";
}

TEST(OptionsPageTest, EscapeAndItsButtonGoBack) {
    Settings settings;
    Opened options{settings};
    EXPECT_EQ(options.press(Key::Escape, true), OptionsOutcome::Stays);
    EXPECT_EQ(options.press(Key::Escape), OptionsOutcome::GoesBack);
    EXPECT_EQ(options.mouse(kLeftDown, middleOf(options.layout.backButton())), OptionsOutcome::GoesBack);
    EXPECT_EQ(options.mouse(MouseButtonChanged{.button = MouseButton::Right, .isDown = true},
                            middleOf(options.layout.backButton())),
              OptionsOutcome::Stays);
}

TEST(OptionsPageTest, ASlidersValueIsWrittenAsItsSettingReads) {
    EXPECT_EQ(cs::sliderText({.value = 3.0F}), "3.0");
    EXPECT_EQ(cs::sliderText({.value = 2.55F}), "2.5");
    EXPECT_EQ(cs::sliderText({.value = 100.0F, .style = cs::OptionValueStyle::Whole}), "100");
    EXPECT_EQ(cs::sliderText({.value = 0.7F, .style = cs::OptionValueStyle::Percent}), "70%");
    EXPECT_EQ(cs::sliderText({.value = 0.0F, .style = cs::OptionValueStyle::Percent}), "0%");
}

TEST(OptionsPageTest, ItsItemOpensItOnTheMenuAndEscapeGoesBackToTheItems) {
    Settings settings;
    const cs::MainMenuLook look{
        .font = {.regular = "Barlow", .strong = "Barlow SemiBold"},
        .background = "resource/BackgroundLayout.txt",
        .items = {{.action = cs::MainMenuAction::Options, .listed = cs::MainMenuListing::Always},
                  {.action = cs::MainMenuAction::Quit, .listed = cs::MainMenuListing::Always}}};
    cs::MainMenu menu{look, {}, optionsOf(settings)};
    menu.open(cs::MenuGame::None);
    const auto send = [&](const nx::platform::WindowEvent& event, MousePosition cursor = kNowhere) {
        return menu.handle(event, cursor, kScreen, cs::MenuGame::None);
    };
    EXPECT_EQ(menu.frameOf(cs::MenuGame::None, kScreen).options, std::nullopt);
    EXPECT_EQ(send(pressed(Key::Enter)), std::nullopt) << "the item opens its page, and asks for nothing";
    ASSERT_TRUE(menu.showsOptions());
    ASSERT_TRUE(menu.frameOf(cs::MenuGame::None, kScreen).options.has_value());

    EXPECT_EQ(send(pressed(Key::Down)), std::nullopt);
    EXPECT_EQ(send(pressed(Key::Right)), std::nullopt);
    EXPECT_FLOAT_EQ(settings.sensitivity, 3.5F) << "the keys are the page's";
    EXPECT_EQ(menu.selected(), cs::MainMenuAction::Options) << "the item whose page is open stays selected";

    const cs::MainMenuLayout layout{kScreen, 2};
    EXPECT_EQ(send(kLeftDown, middleOf(layout.optionTrack(0))), std::nullopt);
    EXPECT_EQ(send(kLeftUp, middleOf(layout.optionTrack(0))), std::nullopt);
    EXPECT_FLOAT_EQ(settings.sensitivity, 3.0F) << "and so is the mouse off the items";

    EXPECT_EQ(send(pressed(Key::Escape)), std::nullopt);
    EXPECT_FALSE(menu.showsOptions());
    EXPECT_TRUE(menu.isOpen());

    std::ignore = send(pressed(Key::Enter));
    ASSERT_TRUE(menu.showsOptions());
    const auto quit = send(kLeftDown, middleOf(layout.item(1)));
    ASSERT_TRUE(quit.has_value());
    EXPECT_EQ(quit->action, cs::MainMenuAction::Quit) << "another item chosen leaves the page";
    EXPECT_FALSE(menu.showsOptions());
}

TEST(MainMenuLayoutTest, TheOptionsTabsAndControlsStandInThePage) {
    for (const nx::rhi::Extent2d size : {nx::rhi::Extent2d{.width = 640, .height = 480}, kScreen,
                                         nx::rhi::Extent2d{.width = 3440, .height = 1440}}) {
        const cs::MainMenuLayout layout{size, 4};
        const nx::ui::PixelRect& page = layout.page();
        const nx::ui::PixelRect first = layout.optionTab(0);
        const nx::ui::PixelRect last = layout.optionTab(cs::kPageTabs - 1);
        EXPECT_GT(first.x, page.x) << size.width;
        EXPECT_LT(last.x + last.wide, page.x + page.wide + nx::Pixels{1});
        EXPECT_GT(layout.optionTab(1).x, first.x + first.wide - nx::Pixels{1}) << "none over another";
        EXPECT_LE(first.y + first.tall, layout.listRow(0).y) << "the tabs over the rows";
        const nx::ui::PixelRect row = layout.listRow(0);
        const nx::ui::PixelRect control = layout.optionControl(0);
        const nx::ui::PixelRect track = layout.optionTrack(0);
        EXPECT_GT(control.x, row.x);
        EXPECT_LE(control.x + control.wide, row.x + row.wide);
        EXPECT_EQ(track.x, control.x);
        EXPECT_LT(track.x + track.wide, control.x + control.wide) << "room for the value after the track";
        EXPECT_GT(track.wide, nx::Pixels{0});
    }
}

} // namespace
