// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the engine reads of a map entity's keys before its game class does: its angles, as ReHLDS's
/// ED_ParseEdict reads them (GPL, consulted: docs/20 section 3), the same for every entity the game and
/// its clients make of a map.
export module nx.game:entityKeys;

import std;
import nx.math;
import nx.assets;

export namespace nx::game {

/// `angles` after the key `key` of value `value`, when the engine reads it as an entity's angles:
/// "angles" whole, or "angle", a yaw, or -1 for straight up and any other negative value straight
/// down; none for another key.
[[nodiscard]] auto anglesAfter(Vec3 angles, std::string_view key, std::string_view value)
    -> std::optional<Vec3>;

/// The angles of `entity`, its angle keys read in the order the map gives them.
[[nodiscard]] auto anglesOf(const assets::BspEntity& entity) -> Vec3;

} // namespace nx::game
