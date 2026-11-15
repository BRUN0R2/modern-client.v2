// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The settings next registers: the binds it starts with, which the main menu's options restore.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;
import nx.settings;
import app.client;

namespace {

using nx::console::KeyId;

[[nodiscard]] auto keyNamed(std::string_view name) -> KeyId {
    const std::optional<KeyId> key = nx::settings::keyNamed(name);
    nx::invariant(key.has_value(), "keyNamed: a key of the original");
    return *key;
}

TEST(ClientBindingsTest, AreCounterStrikesWithNoclipOnAKeyTheyLeaveFree) {
    const nx::settings::KeyBindings bindings = app::defaultClientBindings();
    const nx::settings::KeyBindings counterStrike = nx::settings::defaultBindings();
    EXPECT_EQ(counterStrike.commandOf(keyNamed("v")), "");
    EXPECT_EQ(bindings.commandOf(keyNamed("v")), "noclip");
    EXPECT_EQ(bindings.commandOf(keyNamed("w")), "+forward");
    EXPECT_EQ(bindings.commandOf(keyNamed("MOUSE1")), "+attack");

    nx::settings::KeyBindings withoutNoclip = bindings;
    withoutNoclip.bind(keyNamed("v"), "");
    EXPECT_EQ(withoutNoclip.configText(), counterStrike.configText()) << "and nothing else of their own";
}

} // namespace
