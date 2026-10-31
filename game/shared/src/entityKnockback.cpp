// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;
import std;
import nx.core;
import nx.math;

namespace cs {

auto isValid(const EntityKnockback& rules) noexcept -> bool {
    const auto noneOrMore = [](float value) {
        return std::isfinite(value) && value >= 0.0F;
    };
    return noneOrMore(rules.pushPerDamage.value()) && noneOrMore(rules.mostPushSpeed.value()) &&
           rules.mostPushSpeed > nx::UnitsPerSecond{} && noneOrMore(rules.staggerTime.count()) &&
           std::isfinite(rules.slowShare) && rules.slowShare > 0.0F && rules.slowShare <= 1.0F &&
           noneOrMore(rules.slowRecovery.count());
}

auto pushedBy(const EntityKnockback& rules, nx::Vec3 velocity, nx::Vec3 direction, nx::Damage dealt) noexcept
    -> nx::Vec3 {
    const nx::Vec3 across{.x = direction.x, .y = direction.y};
    const float length = nx::length(across);
    if (length <= 0.0F || dealt <= nx::Damage{}) {
        return velocity;
    }
    const nx::Vec3 going{.x = velocity.x, .y = velocity.y};
    nx::Vec3 pushed = going + (across * (dealt.value() * rules.pushPerDamage.value() / length));
    // Many hits close together push it no faster than one alone may, and none slows down what goes faster.
    const float most = std::max(nx::length(going), rules.mostPushSpeed.value());
    if (const float speed = nx::length(pushed); speed > most) {
        pushed = pushed * (most / speed);
    }
    return {.x = pushed.x, .y = pushed.y, .z = velocity.z};
}

auto walkShareOf(const EntityKnockback& rules, const KnockedBack& knocked, nx::GameTime now,
                 nx::TickRate rate) noexcept -> float {
    if (!knocked.hitAt) {
        return 1.0F;
    }
    const nx::Seconds since = now.since(*knocked.hitAt, rate);
    if (since < rules.staggerTime) {
        return 0.0F;
    }
    if (rules.slowRecovery <= nx::Seconds{}) {
        return 1.0F;
    }
    const float recovered = std::min((since - rules.staggerTime) / rules.slowRecovery, 1.0F);
    return rules.slowShare + ((1.0F - rules.slowShare) * recovered);
}

auto walkedBy(const EntityKnockback& rules, const KnockedBack& knocked, MoveIntent move, nx::GameTime now,
              nx::TickRate rate) noexcept -> MoveIntent {
    const float share = walkShareOf(rules, knocked, now, rate);
    return {.forward = move.forward * share, .side = move.side * share, .up = move.up * share};
}

} // namespace cs
