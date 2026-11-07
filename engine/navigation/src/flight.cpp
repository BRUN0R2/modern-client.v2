// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.navigation;

import std;
import nx.core;
import nx.math;

namespace nx::navigation {

namespace {

/// The share of what a body is shorter ducked by which its feet rise as it ducks in the air: its box shrinks
/// about its middle, as much from below as from above.
constexpr float kDuckedLiftShare = 0.5F;

/// Whether `profile` flies as leastLeapSpeedOf requires: its gravity and jump heights finite and more than
/// none.
[[nodiscard]] auto fliesAsGiven(const AgentProfile& profile) noexcept -> bool {
    const auto isPositive = [](float value) {
        return std::isfinite(value) && value > 0.0F;
    };
    return isPositive(profile.gravity.value()) && isPositive(profile.jumpHeight.value()) &&
           (!profile.crouchedJumpHeight || isPositive(profile.crouchedJumpHeight->value()));
}

/// How fast a body runs at most on the ground `takeoff` is: crouched where only a crouched body stands.
[[nodiscard]] auto runOn(const std::optional<Takeoff>& takeoff, const BodyMotion& motion) noexcept -> float {
    return takeoff && takeoff->isCrouched ? motion.crouchedSpeed.value() : motion.runSpeed.value();
}

} // namespace

auto isValidMotion(const BodyMotion& motion) noexcept -> bool {
    return std::ranges::all_of(std::array{motion.runSpeed.value(), motion.crouchedSpeed.value(),
                                          motion.acceleration, motion.braking},
                               [](float value) { return std::isfinite(value) && value >= 0.0F; });
}

auto leastLeapSpeedOf(const MeshLink& link, const AgentProfile& profile, HammerUnits shortOfStart) noexcept
    -> UnitsPerSecond {
    expects(fliesAsGiven(profile), "leastLeapSpeedOf: a gravity and jump heights finite and more than none");
    const float gap = detail::gapAcross(link.start, link.end, profile.radius);
    if (link.kind == AreaKind::Ladder || link.kind == AreaKind::Door || gap <= 0.0F) {
        return UnitsPerSecond{};
    }
    if (!fliesAsGiven(profile)) {
        return UnitsPerSecond{std::numeric_limits<float>::infinity()}; // under observe: no body takes it
    }
    // From where its feet are once it ducks in the air, as high as its speed up takes it from there.
    const detail::Leap leap = detail::leapOf(link.kind, profile, link.takeoff && link.takeoff->isCrouched);
    const float lift = leap.lift.value();
    const float reach = lift + leap.jump.value();
    const float rise = link.takeoff ? std::min(link.takeoff->rise.value(), reach) : reach;
    const std::optional<Seconds> inTheAir =
        detail::airTimeOf(leap.jump, HammerUnits{std::max(rise - lift, 0.0F)},
                          HammerUnits{link.start.z - link.end.z + lift}, profile.gravity);
    if (!inTheAir) {
        return UnitsPerSecond{std::numeric_limits<float>::infinity()};
    }
    return UnitsPerSecond{(gap + shortOfStart.value()) / inTheAir->count()};
}

auto fastestTakeoffOf(const MeshLink& link, const AgentProfile& profile, const BodyMotion& motion) noexcept
    -> UnitsPerSecond {
    expects(isValidMotion(motion), "fastestTakeoffOf: a motion of speeds and rates finite, of none or more");
    if (!isValidMotion(motion)) {
        return UnitsPerSecond{}; // under observe: it leaves no ledge
    }
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
    return UnitsPerSecond{std::max(ground, profile.airSpeed.value())};
}

auto runUpOf(const MeshLink& link, const AgentProfile& profile, const BodyMotion& motion,
             UnitsPerSecond takeoff) noexcept -> std::optional<HammerUnits> {
    expects(isValidMotion(motion), "runUpOf: a motion of speeds and rates finite, of none or more");
    if (!isValidMotion(motion)) {
        return std::nullopt; // under observe: no run gets it there
    }
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

auto leapOf(AreaKind kind, const AgentProfile& profile, bool isCrouched) noexcept -> Leap {
    if (kind != AreaKind::CrouchedJump || !profile.crouchedJumpHeight) {
        return Leap{.jump = profile.jumpHeight};
    }
    const float lift = (profile.standingHeight.value() - profile.crouchedHeight.value()) * kDuckedLiftShare;
    return Leap{.jump = HammerUnits{std::max(profile.crouchedJumpHeight->value() - lift, 0.0F)},
                .lift = HammerUnits{isCrouched ? 0.0F : lift}};
}

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
