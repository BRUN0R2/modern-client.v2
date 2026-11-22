// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The player's settings the main menu's options change (docs/12-ui-hud-and-console.md, section 4).
export module app.client:options;

import std;
import nx.settings;
import nx.platform;
import cs.client;
import :settings;

namespace app {

/// Builds the rows for the client's settings. The crosshair rows edit the draft displayed by the menu.
/// Other rows update their cvars or key bindings directly. Available languages come from the game data.
[[nodiscard]] auto clientOptionsOf(const ClientSettings& settings, nx::settings::KeyBindings& bindings,
                                   std::span<const std::string> languages, const nx::platform::Window& window,
                                   const std::shared_ptr<cs::CrosshairStyle>& crosshairDraft)
    -> std::vector<cs::OptionRow>;

} // namespace app
