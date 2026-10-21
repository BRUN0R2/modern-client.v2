// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The trigger volumes of a map (docs/07-simulation-and-physics.md, section 5.2): brush entities a
/// player fires by walking into them, as the original's game and engine fire them (CBaseTrigger,
/// SV_TouchLinks; ReGameDLL and ReHLDS, GPL, consulted: docs/20-legal-and-licensing.md, section 3).
export module nx.game:triggers;

import std;
import nx.core;
import nx.ecs;
import nx.collision;

export namespace nx::game {

/// A volume that fires its Targets when a player touches it (trigger_multiple, trigger_once): again
/// `wait` after, or never again for a negative `wait`.
struct Trigger {
    Seconds wait{};
    bool isTouchedByPlayers = true; ///< Not flagged "no clients".
    bool isSpent = false;           ///< Fired for good: it goes at its NextThink.
};

/// `toucher`, a Body, touches the triggers of `world` that its linked box meets and whose model's
/// hull for its size it is inside, in the world's order (SV_TouchLinks): each that is ready fires its
/// targets for it (CBaseTrigger::ActivateMultiTrigger), through the world of `hulls`; then it waits,
/// or is spent and goes a tenth of a second later.
void touchTriggers(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity toucher);

} // namespace nx::game
