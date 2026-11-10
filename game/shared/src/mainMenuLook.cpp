// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.data;

namespace cs {

auto readMainMenuLook(std::string_view text, std::string_view sourceName)
    -> nx::Result<MainMenuLook, MainMenuLookError> {
    auto look = nx::data::parseToml<MainMenuLook>(text, sourceName);
    if (!look) {
        return nx::fail(MainMenuLookError::DataInvalid, std::string{sourceName}, std::move(look.error()));
    }
    const std::span<const MainMenuItem> items = look->items;
    const bool isEachActionOnce = std::ranges::all_of(items, [items](const MainMenuItem& item) {
        return std::ranges::count(items, item.action, &MainMenuItem::action) == 1;
    });
    const bool isNeverEmpty = std::ranges::contains(items, MainMenuListing::Always, &MainMenuItem::listed);
    if (!look->font.isComplete() || look->background.empty() || !isEachActionOnce || !isNeverEmpty) {
        return nx::fail(
            MainMenuLookError::DataInvalid,
            std::format(
                "{}: both font families, a background, and items of which none does what another does "
                "and one at least is always listed, are required",
                sourceName));
    }
    return std::move(*look);
}

} // namespace cs
