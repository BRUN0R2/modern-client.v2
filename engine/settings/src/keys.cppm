// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The original's keys (docs/12-ui-hud-and-console.md, section 6), by its numbers: a key that types
/// an ASCII character has that character's number (`a` is 97, SPACE 32), the others the original's
/// own (UPARROW 128, MWHEELUP 240, MOUSE1 241). console::KeyId is that number, so a `+jump` bound to
/// SPACE runs `+jump 32`, as the original runs it.
export module nx.settings:keys;

import std;
import nx.core;
import nx.platform;
import nx.console;

export namespace nx::settings {

/// The original's key numbers run from 0 to this, excluded.
inline constexpr std::uint32_t kKeyCount = 256;

/// The wheel's keys: each notch presses one and releases it at once.
inline constexpr console::KeyId kWheelDown{239};
inline constexpr console::KeyId kWheelUp{240};

/// The original's key at the position of `key` on a US keyboard: both Ctrl keys are its CTRL, both
/// Shift its SHIFT and both Alt its ALT, and the keypad's keys are its KP_ keys whatever Num Lock
/// says. None for the keys it has no number for: Print Screen, Scroll Lock, Num Lock, the system
/// and menu keys, and the keys without an enumerator.
[[nodiscard]] auto keyOf(platform::Key key) noexcept -> std::optional<console::KeyId>;

/// MOUSE1 to MOUSE5; none past the fifth button, which the original does not have.
[[nodiscard]] auto keyOf(platform::MouseButton button) noexcept -> std::optional<console::KeyId>;

/// The key `name` names, as `bind` reads it: one ASCII character (a letter in either case names its
/// key, which types the small letter), or one of the original's names (SPACE, KP_END, MOUSE1,
/// SEMICOLON...) in any case. None for anything else, and for a character whose key has no name to
/// be written with (nameOf): every key it gives, config.cfg can name.
[[nodiscard]] auto keyNamed(std::string_view name) noexcept -> std::optional<console::KeyId>;

/// How `bind` and config.cfg write `key`: its character when it types a printable one other than `"`
/// and `;`, else its name. None for a number the original names nothing.
[[nodiscard]] auto nameOf(console::KeyId key) noexcept -> std::optional<std::string_view>;

} // namespace nx::settings
