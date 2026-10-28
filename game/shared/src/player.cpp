// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;

namespace cs {

namespace {

/// The classnames of the items of armor, as the original's `give` names them.
constexpr std::string_view kKevlarItem = "item_kevlar";
constexpr std::string_view kAssaultSuitItem = "item_assaultsuit";

} // namespace

auto armorItemNamed(std::string_view classname) noexcept -> std::optional<ArmorKind> {
    if (classname == kKevlarItem) {
        return ArmorKind::Kevlar;
    }
    if (classname == kAssaultSuitItem) {
        return ArmorKind::KevlarAndHelmet;
    }
    return std::nullopt;
}

void wearArmor(PlayerStatus& status, ArmorKind kind) noexcept {
    // A vest leaves a helmet worn on (CItemKevlar::MyTouch sets none but a kind of none).
    if (kind != ArmorKind::Kevlar || status.armorKind == ArmorKind::None) {
        status.armorKind = kind;
    }
    status.armor = kMostArmor;
}

} // namespace cs
