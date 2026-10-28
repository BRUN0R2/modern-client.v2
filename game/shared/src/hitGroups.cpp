// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.data;

namespace cs {

auto HitGroupShares::of(HitGroup group) const noexcept -> DamageShare {
    switch (group) {
    case HitGroup::Generic:
        return generic;
    case HitGroup::Head:
        return head;
    case HitGroup::Chest:
        return chest;
    case HitGroup::Stomach:
        return stomach;
    case HitGroup::LeftArm:
    case HitGroup::RightArm:
        return arm;
    case HitGroup::LeftLeg:
    case HitGroup::RightLeg:
        return leg;
    case HitGroup::Shield:
        return shield;
    default:
        return other;
    }
}

auto readHitGroupRules(std::string_view text, std::string_view sourceName)
    -> nx::Result<HitGroupRules, HitGroupError> {
    auto rules = nx::data::parseToml<HitGroupRules>(text, sourceName);
    if (!rules) {
        return nx::fail(HitGroupError::DataInvalid, std::string{sourceName}, std::move(rules.error()));
    }
    return std::move(*rules);
}

} // namespace cs
