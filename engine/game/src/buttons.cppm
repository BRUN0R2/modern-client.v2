// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Buttons (docs/07-simulation-and-physics.md, section 5), as the original's game makes them work
/// (CBaseButton, func_button; ReGameDLL, GPL, consulted: docs/20-legal-and-licensing.md, section 3):
/// pressed in by a player's use, or touch, or a target's use, firing their targets once in, and back
/// out after their wait, or when used again.
export module nx.game:buttons;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;

export namespace nx::game {

/// Where a button is (the original's toggle states).
enum class ButtonState : std::uint8_t {
    Out,      ///< At rest.
    GoingIn,  ///< Pressed, on its way in.
    In,       ///< In: its targets fired.
    GoingOut, ///< On its way back out.
};

/// A button (CBaseButton): where it rests and where it goes in to, and how.
struct Button {
    Vec3 out{};
    Vec3 in{};
    UnitsPerSecond speed{};
    Seconds wait{}; ///< In, before it comes back out.
    ButtonState state = ButtonState::Out;
    bool staysIn = false;     ///< A "wait" of -1: in for good.
    bool isToggle = false;    ///< Comes back out only when it is used again.
    bool isTouchOnly = false; ///< Pressed by a player's touch, never used.
    bool isUsable = true;     ///< A player's use reaches it: it takes no damage (FCAP_IMPULSE_USE).
    bool isTouchable = false; ///< A touch acts now.
    ecs::Entity activator{};  ///< What pressed it last.
};

} // namespace nx::game

namespace nx::game {

/// `activator` uses button `entity` (CBaseButton::ButtonUse): not moving, an out button goes in; an in
/// one that is a toggle comes back out. A button pressed by touch only ignores it.
void useButton(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
               ecs::Entity activator);

/// `toucher` touched button `entity` (CBaseButton::ButtonTouch): one pressed by touch, not moving, goes
/// in, or, in and a toggle, fires its targets and comes back out.
void touchButton(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
                 ecs::Entity toucher);

/// Button `entity` reached where it was going: in, it fires its targets and waits, or stays
/// (TriggerAndWait); out, it rests, a toggle firing its targets (ButtonBackHome).
void buttonArrived(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity);

/// Button `entity`, in, comes back out (CBaseButton::ButtonReturn).
void returnButton(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity);

} // namespace nx::game
