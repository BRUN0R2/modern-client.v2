// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Buttons: the state behind a `+name`/`-name` command pair (+jump, +attack). A bind runs `+name`
/// with its key when the key goes down and `-name` with the same key when it comes up.
export module nx.console:button;

import std;

export namespace nx::console {

/// Key that holds a button, as the bind layer passes it to the commands: `+jump 32`.
enum class KeyId : std::uint32_t {};

/// What happened to a button since its edges were last taken.
struct ButtonEdges {
    bool pressed = false;  ///< Went down, even if it came up again before the edges were taken.
    bool released = false; ///< Came up.
};

/// Down while any key holds it, or since `+name` was typed without a key. A key that already holds
/// the button pressing again (auto-repeat) changes nothing.
class ButtonState {
public:
    /// `+name`: `key` starts holding the button; without a key, the console holds it.
    inline void press(std::optional<KeyId> key) {
        const bool wasDown = isDown();
        if (key) {
            if (std::ranges::contains(m_keys, *key)) {
                return;
            }
            m_keys.push_back(*key);
        } else {
            m_heldByConsole = true;
        }
        m_edges.pressed = m_edges.pressed || !wasDown;
    }

    /// `-name`: `key` stops holding the button; without a key, every holder lets go, which unsticks
    /// a button from the console.
    inline void release(std::optional<KeyId> key) {
        const bool wasDown = isDown();
        if (key) {
            std::erase(m_keys, *key);
        } else {
            m_keys.clear();
            m_heldByConsole = false;
        }
        m_edges.released = m_edges.released || (wasDown && !isDown());
    }

    [[nodiscard]] inline auto isDown() const noexcept -> bool { return m_heldByConsole || !m_keys.empty(); }

    /// The edges since the previous call, which clears them. Taken once per user command, they
    /// keep a press and release within one frame (a bind on the mouse wheel) from being lost.
    [[nodiscard]] inline auto takeEdges() noexcept -> ButtonEdges {
        return std::exchange(m_edges, ButtonEdges{});
    }

private:
    std::vector<KeyId> m_keys;
    bool m_heldByConsole = false;
    ButtonEdges m_edges;
};

/// Handle to a button registered with Console::registerButton; valid as long as that Console.
class Button {
public:
    inline explicit Button(ButtonState& state) noexcept : m_state{&state} {}

    [[nodiscard]] inline auto isDown() const noexcept -> bool { return m_state->isDown(); }
    [[nodiscard]] inline auto takeEdges() const noexcept -> ButtonEdges { return m_state->takeEdges(); }

private:
    ButtonState* m_state;
};

} // namespace nx::console
