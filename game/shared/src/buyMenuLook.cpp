// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.data;

namespace cs {

auto readBuyMenuLook(std::string_view text, std::string_view sourceName)
    -> nx::Result<BuyMenuLook, BuyMenuLookError> {
    auto look = nx::data::parseToml<BuyMenuLook>(text, sourceName);
    if (!look) {
        return nx::fail(BuyMenuLookError::DataInvalid, std::string{sourceName}, std::move(look.error()));
    }
    const std::span<const BuyMenuArmor> armor = look->armor;
    const auto isShared = [armor](const BuyMenuArmor& item) {
        return std::ranges::count(armor, item.kind, &BuyMenuArmor::kind) > 1 ||
               std::ranges::count(armor, item.key, &BuyMenuArmor::key) > 1;
    };
    const auto isValid = [&isShared](const BuyMenuArmor& item) {
        return item.kind != ArmorKind::None && !item.name.empty() && !item.picture.empty() && !isShared(item);
    };
    if (look->font.regular.empty() || look->font.strong.empty() || !std::ranges::all_of(armor, isValid)) {
        return nx::fail(
            BuyMenuLookError::DataInvalid,
            std::format("{}: both font families, and armor of some kind, named and with a picture, "
                        "no two of one kind or at one key, are required",
                        sourceName));
    }
    return std::move(*look);
}

} // namespace cs
