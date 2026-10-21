// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module tool.nxview;

import std;
import nx.math;
import nx.assets;

namespace nxview {

namespace {

/// The `angle` that aims up, and the pitch of an aim straight up or down.
constexpr int kAngleUp = -1;
constexpr float kStraightPitch = 90.0F;

} // namespace

auto anglesAfter(nx::Vec3 angles, std::string_view key, std::string_view value) -> std::optional<nx::Vec3> {
    if (key == "angles") {
        return nx::assets::vectorOfValue(value).value_or(angles);
    }
    if (key != "angle") {
        return std::nullopt;
    }
    const float yaw = nx::assets::numberOfValue(value).value_or(0.0F);
    if (yaw >= 0.0F) {
        return nx::Vec3{.x = angles.x, .y = yaw, .z = angles.z};
    }
    return static_cast<int>(yaw) == kAngleUp ? nx::Vec3{.x = -kStraightPitch} : nx::Vec3{.x = kStraightPitch};
}

auto anglesOf(const nx::assets::BspEntity& entity) -> nx::Vec3 {
    nx::Vec3 angles{};
    for (const auto& [key, value] : entity.keyValues) {
        angles = anglesAfter(angles, key, value).value_or(angles);
    }
    return angles;
}

} // namespace nxview
