// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The original's keys: the numbers of the keyboard's keys and the mouse's buttons, and the names bind
// reads and config.cfg writes.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.console;
import nx.settings;

namespace {

using nx::console::KeyId;
using nx::platform::Key;
using nx::platform::MouseButton;
using nx::settings::keyNamed;
using nx::settings::keyOf;
using nx::settings::nameOf;

[[nodiscard]] constexpr auto key(std::uint32_t number) noexcept -> std::optional<KeyId> {
    return KeyId{number};
}

TEST(KeysTest, TheKeyboardsKeysHaveTheOriginalsNumbers) {
    EXPECT_EQ(keyOf(Key::A), key('a')) << "the small letter";
    EXPECT_EQ(keyOf(Key::Z), key('z'));
    EXPECT_EQ(keyOf(Key::Digit1), key('1'));
    EXPECT_EQ(keyOf(Key::Digit0), key('0'));
    EXPECT_EQ(keyOf(Key::Grave), key('`'));
    EXPECT_EQ(keyOf(Key::Semicolon), key(';'));
    EXPECT_EQ(keyOf(Key::Space), key(32));
    EXPECT_EQ(keyOf(Key::Enter), key(13));
    EXPECT_EQ(keyOf(Key::Backspace), key(127));
    EXPECT_EQ(keyOf(Key::Up), key(128));
    EXPECT_EQ(keyOf(Key::F1), key(135));
    EXPECT_EQ(keyOf(Key::F12), key(146));
    EXPECT_EQ(keyOf(Key::Keypad1), key(166)) << "KP_END";
    EXPECT_EQ(keyOf(Key::KeypadMultiply), key('*'));
    EXPECT_EQ(keyOf(Key::Pause), key(255));

    EXPECT_EQ(keyOf(Key::LeftControl), key(133)) << "both Ctrl keys are CTRL";
    EXPECT_EQ(keyOf(Key::RightControl), key(133));
    EXPECT_EQ(keyOf(Key::RightShift), keyOf(Key::LeftShift));
    EXPECT_EQ(keyOf(Key::RightAlt), keyOf(Key::LeftAlt));

    for (const Key unnumbered :
         {Key::PrintScreen, Key::ScrollLock, Key::NumLock, Key::LeftSystem, Key::Menu, Key{100}}) {
        EXPECT_EQ(keyOf(unnumbered), std::nullopt) << std::to_underlying(unnumbered);
    }
}

TEST(KeysTest, KeyboardMappingSelectsThePositionOrTheLayoutsBaseCharacter) {
    using nx::platform::KeyChanged;
    using nx::settings::KeyMapping;
    const KeyChanged frenchW{.key = Key::W, .character = U'z'};
    EXPECT_EQ(keyOf(frenchW, KeyMapping::Scancode), key('w'));
    EXPECT_EQ(keyOf(frenchW, KeyMapping::Keycode), key('z'));
    EXPECT_EQ(keyOf(KeyChanged{.key = Key::Y, .character = U'Z'}, KeyMapping::Keycode), key('z'));
    EXPECT_EQ(keyOf(KeyChanged{.key = Key::Digit1, .character = U'&'}, KeyMapping::Keycode), key('&'));
    EXPECT_EQ(keyOf(KeyChanged{.key = Key::Semicolon, .character = U';'}, KeyMapping::Keycode), key(';'));
    EXPECT_EQ(keyOf(KeyChanged{.key = Key::A, .character = U'\u00e9'}, KeyMapping::Keycode), std::nullopt);
    EXPECT_EQ(keyOf(KeyChanged{.key = Key::A}, KeyMapping::Keycode), std::nullopt)
        << "an unknown layout character is not silently mapped by position";
    EXPECT_EQ(keyOf(KeyChanged{.key = Key::LeftControl}, KeyMapping::Keycode), key(133));
    EXPECT_EQ(keyOf(KeyChanged{.key = Key::Space}, KeyMapping::Keycode), key(32));
    EXPECT_EQ(keyOf(KeyChanged{.key = Key::Keypad4, .character = U'4'}, KeyMapping::Keycode),
              keyOf(Key::Keypad4))
        << "the keypad retains its KP_ keys even when SDL hides the numpad distinction";
}

TEST(KeysTest, TheKeyBelowEscapeIsTheConsolesOnAnyLayout) {
    using nx::platform::KeyChanged;
    using nx::settings::KeyMapping;
    // What a Brazilian ABNT, a French and a German layout type below Escape, and none at all.
    for (const std::optional<char32_t> character :
         {std::optional{U'\''}, std::optional{U'\u00b2'}, std::optional{U'^'}, std::optional<char32_t>{}}) {
        for (const KeyMapping mapping : {KeyMapping::Keycode, KeyMapping::Scancode}) {
            EXPECT_EQ(keyOf(KeyChanged{.key = Key::Grave, .character = character}, mapping), key('`'))
                << std::to_underlying(mapping);
        }
    }
    EXPECT_EQ(keyOf(KeyChanged{.key = Key::Apostrophe, .character = U'~'}, KeyMapping::Keycode), key('~'))
        << "a layout's ~ elsewhere is still its own";
}

TEST(KeysTest, TheMousesButtonsAreMouse1ToMouse5) {
    EXPECT_EQ(keyOf(MouseButton::Left), keyNamed("MOUSE1"));
    EXPECT_EQ(keyOf(MouseButton::Right), keyNamed("MOUSE2"));
    EXPECT_EQ(keyOf(MouseButton::Middle), keyNamed("MOUSE3"));
    EXPECT_EQ(keyOf(MouseButton::X1), keyNamed("MOUSE4"));
    EXPECT_EQ(keyOf(MouseButton::X2), keyNamed("MOUSE5"));
    EXPECT_EQ(keyOf(MouseButton{6}), std::nullopt);
    EXPECT_EQ(keyNamed("MWHEELUP"), nx::settings::kWheelUp);
    EXPECT_EQ(keyNamed("MWHEELDOWN"), nx::settings::kWheelDown);
}

TEST(KeysTest, BindReadsCharactersAndNamesInAnyCase) {
    EXPECT_EQ(keyNamed("w"), key('w'));
    EXPECT_EQ(keyNamed("W"), key('w')) << "the key that types it";
    EXPECT_EQ(keyNamed("SPACE"), key(32));
    EXPECT_EQ(keyNamed("space"), key(32));
    EXPECT_EQ(keyNamed(" "), key(32));
    EXPECT_EQ(keyNamed("kp_end"), key(166));
    EXPECT_EQ(keyNamed("SEMICOLON"), key(';'));
    EXPECT_EQ(keyNamed("~"), key('~')) << "a key no position types, as in the original";
    EXPECT_EQ(keyNamed("AUX32"), key(238));

    for (const std::string_view invalid : {"", "SPACEBAR", "F13", "\"", "\x01", "\xC3", "ww"}) {
        EXPECT_EQ(keyNamed(invalid), std::nullopt) << invalid;
    }
}

TEST(KeysTest, EveryNameReadsBackAsItsKey) {
    EXPECT_EQ(nameOf(KeyId{'a'}), "a");
    EXPECT_EQ(nameOf(KeyId{32}), "SPACE");
    EXPECT_EQ(nameOf(KeyId{';'}), "SEMICOLON");
    EXPECT_EQ(nameOf(KeyId{'\\'}), "\\");
    EXPECT_EQ(nameOf(KeyId{241}), "MOUSE1");
    EXPECT_EQ(nameOf(KeyId{'"'}), std::nullopt);
    EXPECT_EQ(nameOf(KeyId{200}), std::nullopt);
    EXPECT_EQ(nameOf(KeyId{256}), std::nullopt);

    std::size_t named = 0;
    for (std::uint32_t number = 0; number < nx::settings::kKeyCount; ++number) {
        const std::optional<std::string_view> name = nameOf(KeyId{number});
        if (!name) {
            continue;
        }
        ++named;
        const bool isCapital = number >= std::uint32_t{'A'} && number <= std::uint32_t{'Z'};
        EXPECT_EQ(keyNamed(*name), key(isCapital ? number + std::uint32_t{'a' - 'A'} : number)) << *name;
    }
    EXPECT_EQ(named, std::size_t{92 + 91}) << "the printable characters but \" and ;, and the 91 names";
}

} // namespace
