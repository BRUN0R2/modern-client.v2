// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Binds (docs/12-ui-hud-and-console.md, section 6): the commands the player's keys run, the console
/// commands that set them, Counter-Strike 1.6's, and the keys running them as they go down and up,
/// as the original runs them.
export module nx.settings:binds;

import std;
import nx.core;
import nx.platform;
import nx.console;
import :keys;

export namespace nx::settings {

/// The command bound to each of the original's keys, as `bind` sets them.
class KeyBindings {
public:
    /// What `key` runs; empty when nothing is bound to it.
    [[nodiscard]] auto commandOf(console::KeyId key) const noexcept -> std::string_view;

    /// Binds `command` to `key`; an empty command unbinds it. Expects a key with a name (nameOf),
    /// and a command without `"` or a line break, which config.cfg could not read back.
    void bind(console::KeyId key, std::string command);

    /// Unbinds every key.
    void unbindAll() noexcept;

    /// `unbindall`, then a `bind "<key>" "<command>"` line for each bound key, by number: what
    /// config.cfg keeps, so that it gives back these binds and none other.
    [[nodiscard]] auto configText() const -> std::string;

private:
    std::array<std::string, kKeyCount> m_commands;
};

/// Counter-Strike 1.6's binds: those its options restore (w, s, a and d walk, SPACE jumps, e uses,
/// MOUSE1 attacks...).
[[nodiscard]] auto defaultBindings() -> KeyBindings;

/// Registers in `console` the commands that change `bindings`, which must outlive their use:
///
/// - `bind <key> [command]`: binds the rest of the words, joined by spaces; the key alone prints
///   what it runs. A key is named as keyNamed reads it, and a command holds no `"`.
/// - `unbind <key>`, `unbindall`.
/// - `bindlist`: every bound key and its command, then their count.
///
/// Errors: those of Console::registerCommand.
[[nodiscard]] auto registerBindCommands(console::Console& console, KeyBindings& bindings)
    -> Result<void, console::ConsoleError>;

/// The player's keys, mouse buttons and wheel, running in `console` what `bindings` binds to them,
/// as the original runs it. A key going down queues its command, followed by the key's number when
/// it starts with `+`: `+jump 32` from SPACE. Coming up, it queues that command with its `+` turned
/// to `-`, `-jump 32`, and nothing for other commands. The number follows the whole command, as in
/// the original: a key bound to `+jump; wait; -jump` queues `+jump; wait; -jump 32`.
class BoundInput {
public:
    /// Both must outlive the BoundInput.
    inline BoundInput(console::Console& console, const KeyBindings& bindings) noexcept
        : m_console{&console}, m_bindings{&bindings} {}

    /// What `event` does to the keys: a key or a mouse button goes down or up, the wheel turns, or
    /// the window loses the keyboard and every key down comes up. Other events do nothing.
    void handle(const platform::WindowEvent& event);

    /// `key` went down: queues its command, unless it is down already (the system's key repeat).
    void press(console::KeyId key);

    /// `key` came up: queues its `-` command, when it went down here.
    void release(console::KeyId key);

    /// Every key down comes up.
    void releaseAll();

private:
    console::Console* m_console;
    const KeyBindings* m_bindings;
    std::bitset<kKeyCount> m_down;
    float m_wheel = 0.0F; ///< Notches turned and not run yet: a part of one, on precise wheels.
};

} // namespace nx::settings
