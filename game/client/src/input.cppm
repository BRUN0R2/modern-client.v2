// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The player's input (docs/12-ui-hud-and-console.md, section 6): the `+` buttons the binds hold,
/// `+forward` and the others, the user commands they make (docs/07-simulation-and-physics.md,
/// section 2.2) and the view the keys turn, as the original's client makes them.
export module cs.client:input;

import std;
import nx.core;
import nx.math;
import nx.console;
import cs.shared;

export namespace cs {

/// Why the player's input could not be set up.
enum class InputError : std::uint8_t {
    RegistrationFailed, ///< A button or a cvar could not be registered (cause: the console's failure).
};

/// How far the view looks up and down: the original's cl_pitchup and cl_pitchdown.
inline constexpr nx::Degrees kMaxPitch{89.0F};

/// The `+` buttons of the player's input, by the original's names.
struct InputButtons {
    nx::console::Button forward;   ///< `+forward`.
    nx::console::Button back;      ///< `+back`.
    nx::console::Button moveLeft;  ///< `+moveleft`.
    nx::console::Button moveRight; ///< `+moveright`.
    nx::console::Button moveUp;    ///< `+moveup`: up, in water and in noclip.
    nx::console::Button moveDown;  ///< `+movedown`.
    nx::console::Button left;      ///< `+left`: turns the view left; with `+strafe`, moves left.
    nx::console::Button right;     ///< `+right`.
    nx::console::Button lookUp;    ///< `+lookup`.
    nx::console::Button lookDown;  ///< `+lookdown`.
    nx::console::Button strafe;    ///< `+strafe`.
    nx::console::Button speed;     ///< `+speed`: walks, at cl_movespeedkey; turns at cl_anglespeedkey.
    nx::console::Button jump;      ///< `+jump`.
    nx::console::Button duck;      ///< `+duck`.
    nx::console::Button use;       ///< `+use`.
    nx::console::Button attack;    ///< `+attack`.
    nx::console::Button attack2;   ///< `+attack2`.
    nx::console::Button reload;    ///< `+reload`.
};

/// The cvars of the player's input, by the original's names, with Counter-Strike 1.6's values and
/// archived as it archives them.
struct InputCvars {
    nx::console::Cvar<float> forwardSpeed;  ///< `cl_forwardspeed`: what `+forward` asks, a second.
    nx::console::Cvar<float> backSpeed;     ///< `cl_backspeed`.
    nx::console::Cvar<float> sideSpeed;     ///< `cl_sidespeed`.
    nx::console::Cvar<float> upSpeed;       ///< `cl_upspeed`.
    nx::console::Cvar<float> moveSpeedKey;  ///< `cl_movespeedkey`: the share of each speed +speed asks.
    nx::console::Cvar<float> yawSpeed;      ///< `cl_yawspeed`: degrees a second `+left` turns.
    nx::console::Cvar<float> pitchSpeed;    ///< `cl_pitchspeed`: degrees a second `+lookup` turns.
    nx::console::Cvar<float> angleSpeedKey; ///< `cl_anglespeedkey`: the share of each turn with +speed.
};

/// Where a button is at the end of a command.
enum class ButtonPosition : std::uint8_t {
    Up,
    Down,
};

/// The shares of a command the original's CL_KeyState counts a button held for, besides all of it and
/// none: half when it went down during the command, a quarter when it went down and came up again,
/// three quarters when it came up and went down again.
inline constexpr float kPressedShare = 0.5F;
inline constexpr float kTappedShare = 0.25F;
inline constexpr float kPressedAgainShare = 0.75F;

/// How much of a command a button now at `position`, whose `edges` the command saw, was held, as the
/// original's CL_KeyState counts it: all of it when down throughout, none when it came up.
[[nodiscard]] constexpr auto heldShare(ButtonPosition position, nx::console::ButtonEdges edges) noexcept
    -> float {
    const bool isDown = position == ButtonPosition::Down;
    if (edges.pressed && edges.released) {
        return isDown ? kPressedAgainShare : kTappedShare;
    }
    if (edges.pressed) {
        return isDown ? kPressedShare : 0.0F;
    }
    if (edges.released) {
        return 0.0F;
    }
    return isDown ? 1.0F : 0.0F;
}

/// The player's input: its buttons and cvars, registered in a console, and what they make.
class PlayerInput {
public:
    /// Registers the buttons and the cvars in `console`, which must outlive the result. Errors:
    /// RegistrationFailed.
    [[nodiscard]] static auto registerIn(nx::console::Console& console)
        -> nx::Result<PlayerInput, InputError>;

    /// Turns `view` over `elapsed` by the keys that turn it, as the original's CL_AdjustAngles does:
    /// `+left` and `+right` at cl_yawspeed, unless `+strafe` makes them move; `+lookup` and
    /// `+lookdown` at cl_pitchspeed, up to kMaxPitch; both at cl_anglespeedkey of that while `+speed`
    /// is held. Once a frame: it takes the edges of the buttons it reads.
    void turn(nx::EulerAngles& view, nx::Seconds elapsed);

    /// The command for the next `duration`, looking along `view`, as the original's CL_CreateMove
    /// makes it: each movement button asks its speed (cl_forwardspeed...) for the share of the
    /// command it was held (heldShare), all of them cl_movespeedkey of that while `+speed` is held,
    /// together no more than `maxSpeed` (the player's, from the server; zero for no limit). The
    /// movement buttons are set while held; `+jump`, `+duck`, `+use`, `+attack`, `+attack2` and
    /// `+reload` also when pressed during the command, so that a press and a release within one (a
    /// bind on the mouse wheel) still counts.
    /// It selects the weapon selectWeapon chose since the last command. Once a command: it takes the
    /// edges of the buttons it reads, and the weapon chosen.
    [[nodiscard]] auto nextCommand(nx::EulerAngles view, nx::Milliseconds duration,
                                   nx::UnitsPerSecond maxSpeed) -> UserCommand;

    /// The next command selects `weapon`: what the original's `weapon_<name>` commands and `lastinv`
    /// ask.
    inline void selectWeapon(WeaponIndex weapon) noexcept { m_weaponSelect = weapon; }

    /// The `+` buttons the binds and the console hold.
    [[nodiscard]] inline auto buttons() const noexcept -> const InputButtons& { return m_buttons; }
    /// The speeds the buttons ask for, by the original's cvars.
    [[nodiscard]] inline auto cvars() const noexcept -> const InputCvars& { return m_cvars; }

private:
    inline PlayerInput(const InputButtons& inputButtons, const InputCvars& inputCvars) noexcept
        : m_buttons{inputButtons}, m_cvars{inputCvars} {}

    InputButtons m_buttons;
    InputCvars m_cvars;
    std::optional<WeaponIndex> m_weaponSelect;
};

} // namespace cs
