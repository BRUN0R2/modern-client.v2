// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The player's settings the main menu's options change (docs/12-ui-hud-and-console.md, section 4).
export module app.client:options;

import std;
import cs.client;
import :settings;

namespace app {

/// The settings the options list, by their tabs: each reads and sets a cvar of `settings`, which takes
/// effect as it is set and which config.cfg keeps. The cvars are the console's: they outlive what is
/// returned.
[[nodiscard]] auto clientOptionsOf(const ClientSettings& settings) -> std::vector<cs::OptionRow>;

} // namespace app
