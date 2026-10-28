// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.game;
import nx.navigation;
import cs.shared;

namespace cs {

auto standingPlayerProfile(const nx::game::NavigationData& navigation)
    -> nx::Result<nx::navigation::AgentProfile, nx::navigation::MeshBuildError> {
    const auto profile =
        std::ranges::find(navigation.profiles, kPlayerProfileName, &nx::navigation::AgentProfile::name);
    if (profile == navigation.profiles.end()) {
        return nx::fail(nx::navigation::MeshBuildError::InvalidProfile,
                        std::format("no navigation profile named {:?}", kPlayerProfileName));
    }
    if (profile->radius.value() != kStandingBox.max.x ||
        profile->standingHeight.value() != kStandingBox.max.z - kStandingBox.min.z ||
        profile->stepHeight != MovementSettings{}.stepSize ||
        profile->minGroundNormalZ != kMinGroundNormalZ) {
        return nx::fail(
            nx::navigation::MeshBuildError::InvalidProfile,
            std::format("the navigation profile {:?} must match a standing player", profile->name));
    }
    nx::navigation::AgentProfile standing = *profile;
    standing.crouchedHeight = standing.standingHeight;
    return standing;
}

} // namespace cs
