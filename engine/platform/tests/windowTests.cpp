// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// These run on SDL's offscreen video driver (SDL_VIDEODRIVER=offscreen, set by CMakeLists.txt):
// real windows, without a display.

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;

#include "contractRecorder.hpp"

namespace {

using nx::platform::PixelSize;
using nx::platform::Window;
using nx::platform::WindowError;
using nx::platform::WindowEvent;

[[nodiscard]] auto openWindow(PixelSize size = {.width = 320, .height = 240}) -> std::optional<Window> {
    auto window = Window::create({.title = "nxPlatformTests", .size = size, .isResizable = false});
    EXPECT_TRUE(window.has_value()) << nx::describe(window.error());
    return window ? std::optional<Window>{std::move(*window)} : std::nullopt;
}

TEST(WindowTest, OpensWithTheRequestedSize) {
    const auto window = openWindow({.width = 320, .height = 240});
    ASSERT_TRUE(window.has_value());
    EXPECT_EQ(window->pixelSize(), (PixelSize{.width = 320, .height = 240}));
    EXPECT_EQ(nx::platform::WindowDesc{}.size, nx::platform::kDefaultWindowSize);
}

TEST(WindowTest, AWindowOpenedHiddenShowsWhenItIsTold) {
    auto window = Window::create({.title = "nxPlatformTests", .isResizable = false, .isShown = false});
    ASSERT_TRUE(window.has_value()) << nx::describe(window.error());
    EXPECT_FALSE(window->isShown()) << "hidden until it has something to show";
    const auto shown = window->show();
    ASSERT_TRUE(shown.has_value()) << nx::describe(shown.error());
    EXPECT_TRUE(window->isShown());
    const auto again = window->show();
    ASSERT_TRUE(again.has_value()) << nx::describe(again.error());
    EXPECT_TRUE(window->isShown()) << "one shown stays as it is";
}

TEST(WindowTest, AWindowShowsAsItOpensUnlessAskedNot) {
    const auto window = openWindow();
    ASSERT_TRUE(window.has_value());
    EXPECT_TRUE(window->isShown());
    EXPECT_TRUE(nx::platform::WindowDesc{}.isShown);
}

TEST(WindowTest, AnOffscreenWindowHasNothingAGpuCanDrawTo) {
    const auto window = openWindow();
    ASSERT_TRUE(window.has_value());
    const auto native = window->nativeWindow();
    ASSERT_FALSE(native.has_value());
    EXPECT_EQ(native.error().code, WindowError::NoNativeWindow);
    EXPECT_NE(native.error().detail.find("offscreen"), std::string::npos) << native.error().detail;
}

TEST(WindowTest, PollingAppendsWithoutAskingToClose) {
    auto window = openWindow();
    ASSERT_TRUE(window.has_value());
    std::vector<WindowEvent> events{nx::platform::FocusChanged{.isFocused = true}};
    window->pollEvents(events);
    ASSERT_FALSE(events.empty());
    EXPECT_TRUE(std::holds_alternative<nx::platform::FocusChanged>(events.front())) << "what was there stays";
    EXPECT_TRUE(std::ranges::none_of(events, [](const WindowEvent& event) {
        return std::holds_alternative<nx::platform::CloseRequested>(event);
    }));
}

TEST(WindowTest, TextInputTurnsOnAndOff) {
    auto window = openWindow();
    ASSERT_TRUE(window.has_value());
    EXPECT_FALSE(window->isTextInputOn()) << "off until something is typed in";
    const auto on = window->setTextInput(nx::platform::TextInput::On);
    ASSERT_TRUE(on.has_value()) << nx::describe(on.error());
    EXPECT_TRUE(window->isTextInputOn());
    const auto off = window->setTextInput(nx::platform::TextInput::Off);
    ASSERT_TRUE(off.has_value()) << nx::describe(off.error());
    EXPECT_FALSE(window->isTextInputOn());
}

TEST(WindowTest, KeyboardEventsKeepThePositionAndTheLayoutsBaseCharacter) {
    auto window = openWindow();
    ASSERT_TRUE(window.has_value());
    std::vector<WindowEvent> events;
    window->pollEvents(events);
    events.clear();
    SDL_Event typed{};
    typed.key.type = SDL_EVENT_KEY_DOWN;
    typed.key.scancode = SDL_SCANCODE_A;
    typed.key.key = SDLK_Q;
    typed.key.mod = SDL_KMOD_SHIFT | SDL_KMOD_CAPS;
    typed.key.down = true;
    typed.key.repeat = true;
    ASSERT_TRUE(SDL_PushEvent(&typed));
    SDL_Event control = typed;
    control.key.scancode = SDL_SCANCODE_F1;
    control.key.key = SDLK_F1;
    control.key.repeat = false;
    ASSERT_TRUE(SDL_PushEvent(&control));
    window->pollEvents(events);
    std::vector<nx::platform::KeyChanged> keys;
    for (const WindowEvent& event : events) {
        if (const auto* key = std::get_if<nx::platform::KeyChanged>(&event)) {
            keys.push_back(*key);
        }
    }
    ASSERT_EQ(keys.size(), 2);
    EXPECT_EQ(keys[0].key, nx::platform::Key::A);
    EXPECT_EQ(keys[0].character, U'q');
    EXPECT_TRUE(keys[0].isDown);
    EXPECT_TRUE(keys[0].isRepeat);
    EXPECT_EQ(keys[1].key, nx::platform::Key::F1);
    EXPECT_FALSE(keys[1].character.has_value());
}

TEST(WindowTest, AMovedWindowStaysOpenUntilItsLastOwnerCloses) {
    auto window = openWindow();
    ASSERT_TRUE(window.has_value());
    Window moved = std::move(*window);
    window.reset(); // the moved-from window owns nothing to close
    EXPECT_EQ(moved.pixelSize(), (PixelSize{.width = 320, .height = 240}));
}

TEST(WindowTest, AnotherWindowOpensOnceTheFirstIsClosed) {
    {
        const auto first = openWindow();
        ASSERT_TRUE(first.has_value());
    }
    const auto second = openWindow({.width = 640, .height = 480});
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->pixelSize(), (PixelSize{.width = 640, .height = 480}));
}

TEST(WindowTest, TwoWindowsAtOnceBreakTheContract) {
    const nx::test::ContractRecorder recorder;
    const auto first = openWindow();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(recorder.count(), 0);
    const auto second = openWindow(); // observed, not enforced: the second window opens anyway
    EXPECT_EQ(recorder.count(), 1);
}

} // namespace
