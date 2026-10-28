// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.bots;

import std;
import nx.core;
import nx.math;
import cs.server;

namespace cs {

namespace {

/// The ground's way from `from` to `to`, of length 1; none when `to` is straight above or below.
[[nodiscard]] auto groundWayOf(nx::Vec3 from, nx::Vec3 to) noexcept -> std::optional<nx::Vec3> {
    const nx::Vec3 across{.x = to.x - from.x, .y = to.y - from.y};
    const float length = nx::length(across);
    if (length == 0.0F) {
        return std::nullopt;
    }
    return across / length;
}

} // namespace

auto isInViewCone(nx::Vec3 origin, nx::Degrees yaw, nx::Degrees fieldOfView, nx::Vec3 point) noexcept
    -> bool {
    const std::optional<nx::Vec3> way = groundWayOf(origin, point);
    if (!way) {
        return true;
    }
    const nx::Vec3 facing = nx::angleVectors(nx::EulerAngles{.yaw = yaw}).forward;
    constexpr float kHalf = 0.5F;
    return nx::dot(*way, facing) > std::cos(nx::toRadians(fieldOfView * kHalf).value());
}

auto sightSpotsOf(const PerceptionTarget& target, nx::Vec3 origin, const BotSightSpots& spots) noexcept
    -> std::array<nx::Vec3, kSightSpotCount> {
    const nx::Vec3 chest = target.origin;
    const nx::HammerUnits feet = target.isDucked ? spots.duckedFeet : spots.standingFeet;
    const std::optional<nx::Vec3> way = groundWayOf(origin, chest);
    const nx::Vec3 left = way ? nx::Vec3{.x = -way->y, .y = way->x} * spots.side.value() : nx::Vec3{};
    return {chest, chest + nx::Vec3{.z = spots.head.value()}, chest - nx::Vec3{.z = feet.value()},
            chest + left, chest - left};
}

} // namespace cs
