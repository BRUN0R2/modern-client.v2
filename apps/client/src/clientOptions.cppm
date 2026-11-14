// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The player's settings the main menu's options change (docs/12-ui-hud-and-console.md, section 4).
export module app.client:options;

import std;
import nx.settings;
import cs.shared;
import cs.client;
import :settings;

namespace app {

/// The settings the options list, by their tabs: each reads and sets a cvar of `settings`, which config.cfg
/// keeps and which takes effect at once or as the row tells, or the keys `bindings` binds to an action of
/// the game's, which it keeps too. A setting of a few values writes them as `texts` does, and the language
/// is one of `languages`, the tags the game has its texts in. The cvars are the console's, which outlive
/// what is returned, as `bindings` must.
[[nodiscard]] auto clientOptionsOf(const ClientSettings& settings, nx::settings::KeyBindings& bindings,
                                   const cs::OptionsTexts& texts, const std::vector<std::string>& languages)
    -> std::vector<cs::OptionRow>;

} // namespace app
