// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.navigation;

import std;
import nx.core;
import nx.math;

namespace nx::navigation {

namespace {

/// How high a body of `profile` jumps over a link of the kind `kind`: ducking in the air over one it jumps
/// crouched, standing over the others.
[[nodiscard]] auto jumpOf(AreaKind kind, const AgentProfile& profile) noexcept -> float {
    return kind == AreaKind::CrouchedJump && profile.crouchedJumpHeight ? profile.crouchedJumpHeight->value()
                                                                        : profile.jumpHeight.value();
}

/// How fast a body runs at most on the ground `takeoff` is: crouched where only a crouched body stands.
[[nodiscard]] auto runOn(const std::optional<Takeoff>& takeoff, const BodyMotion& motion) noexcept -> float {
    return takeoff && takeoff->isCrouched ? motion.crouchedSpeed.value() : motion.runSpeed.value();
}

} // namespace

auto isValidMotion(const BodyMotion& motion) noexcept -> bool {
    return std::ranges::all_of(std::array{motion.runSpeed.value(), motion.crouchedSpeed.value(),
                                          motion.airSpeed.value(), motion.acceleration, motion.braking},
                               [](float value) { return std::isfinite(value) && value >= 0.0F; });
}

auto leastLeapSpeedOf(const MeshLink& link, const AgentProfile& profile, HammerUnits shortOfStart) noexcept
    -> UnitsPerSecond {
    const float gap = detail::gapAcross(link.start, link.end, profile.radius);
    if (link.kind == AreaKind::Ladder || link.kind == AreaKind::Door || gap <= 0.0F) {
        return UnitsPerSecond{};
    }
    const float jump = jumpOf(link.kind, profile);
    const float rise = link.takeoff ? std::min(link.takeoff->rise.value(), jump) : jump;
    const std::optional<Seconds> inTheAir = detail::airTimeOf(
        HammerUnits{jump}, HammerUnits{rise}, HammerUnits{link.start.z - link.end.z}, profile.gravity);
    if (!inTheAir) {
        return UnitsPerSecond{std::numeric_limits<float>::infinity()};
    }
    return UnitsPerSecond{(gap + shortOfStart.value()) / inTheAir->count()};
}

auto fastestTakeoffOf(const MeshLink& link, const AgentProfile& profile, const BodyMotion& motion) noexcept
    -> UnitsPerSecond {
    const float run = runOn(link.takeoff, motion);
    float ground = run;
    if (link.takeoff && motion.acceleration > 0.0F) {
        // The speed whose run up (runUpOf) is the floor behind the start: the root of v² + 2·braking·room·v =
        // 2·acceleration·run·room.
        const float room = std::max(link.takeoff->room.value() - profile.radius.value(), 0.0F);
        const float slowing = motion.braking * room;
        const float gaining = 2.0F * motion.acceleration * run * room;
        ground = std::min(std::sqrt((slowing * slowing) + gaining) - slowing, run);
    }
    return UnitsPerSecond{std::max(ground, motion.airSpeed.value())};
}

auto runUpOf(const MeshLink& link, const AgentProfile& profile, const BodyMotion& motion,
             UnitsPerSecond takeoff) noexcept -> std::optional<HammerUnits> {
    const float run = runOn(link.takeoff, motion);
    const float speed = takeoff.value();
    if (speed > run) {
        return std::nullopt;
    }
    if (motion.acceleration <= 0.0F) {
        return profile.radius;
    }
    const float gaining = (motion.acceleration * run) - (motion.braking * speed);
    if (gaining <= 0.0F) {
        return std::nullopt;
    }
    return HammerUnits{profile.radius.value() + (speed * speed / (2.0F * gaining))};
}

} // namespace nx::navigation

namespace nx::navigation::detail {

auto airTimeOf(HammerUnits jump, HammerUnits rise, HammerUnits drop, UnitsPerSecondSquared gravity) noexcept
    -> std::optional<Seconds> {
    const float pull = gravity.value();
    const float fall = rise.value() + drop.value();
    if (fall < 0.0F) {
        return std::nullopt;
    }
    // Up to as high as it rises, where it stops rising at the top of its jump or with its head on what is
    // above it, then down from there.
    const float launch = std::sqrt(2.0F * pull * jump.value());
    const float rising =
        (launch - std::sqrt(std::max((launch * launch) - (2.0F * pull * rise.value()), 0.0F))) / pull;
    return Seconds{rising + std::sqrt(2.0F * fall / pull)};
}

} // namespace nx::navigation::detail
