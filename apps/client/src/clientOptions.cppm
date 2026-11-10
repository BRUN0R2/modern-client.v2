// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The player's settings the main menu's options change (docs/12-ui-hud-and-console.md, section 4).
export module app.client:options;

import std;
import nx.settings;
import cs.client;
import :settings;

namespace app {

/// The settings the options list, by their tabs: each reads and sets a cvar of `settings`, which takes
/// effect as it is set and which config.cfg keeps, or the keys `bindings` binds to an action of the game's,
/// which it keeps too. The cvars are the console's, which outlive what is returned, as `bindings` must.
[[nodiscard]] auto clientOptionsOf(const ClientSettings& settings, nx::settings::KeyBindings& bindings)
    -> std::vector<cs::OptionRow>;

} // namespace app
