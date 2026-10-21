// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// These run on SDL's offscreen video driver (SDL_VIDEODRIVER=offscreen, set by CMakeLists.txt):
// real windows, without a display.

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
