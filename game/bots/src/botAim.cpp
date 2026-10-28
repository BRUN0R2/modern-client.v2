// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.bots;

import std;
import nx.core;
import nx.math;

namespace cs {

namespace {

/// How a turn speeds up, pulled by `stiffness` times how far it is off and held back by `spring`'s
/// damping times how fast it turns, within the spring's acceleration.
[[nodiscard]] auto accelerationOf(const BotSpring& spring, nx::PerSecondSquared stiffness, nx::Degrees off,
                                  nx::DegreesPerSecond speed) noexcept -> nx::DegreesPerSecondSquared {
    const nx::DegreesPerSecondSquared pull{(stiffness.value() * off.value()) -
                                           (spring.damping.value() * speed.value())};
    return std::clamp(pull, -spring.maxAcceleration, spring.maxAcceleration);
}

/// `speed` sped up by `acceleration` for `duration`.
[[nodiscard]] auto spedUp(nx::DegreesPerSecond speed, nx::DegreesPerSecondSquared acceleration,
                          nx::Seconds duration) noexcept -> nx::DegreesPerSecond {
    return speed + nx::DegreesPerSecond{acceleration.value() * duration.count()};
}

/// How far `speed` turns in `duration`.
[[nodiscard]] auto turnedBy(nx::DegreesPerSecond speed, nx::Seconds duration) noexcept -> nx::Degrees {
    return nx::Degrees{speed.value() * duration.count()};
}

} // namespace

auto turnView(nx::EulerAngles view, nx::EulerAngles desired, BotViewTurn& turn, const BotSpring& spring,
              const BotLookRules& rules, nx::Seconds duration) noexcept -> nx::EulerAngles {
    nx::EulerAngles turned{};
    const nx::Degrees yawOff = nx::normalizeAngle(desired.yaw - view.yaw);
    if (yawOff < rules.snapTolerance && yawOff > -rules.snapTolerance) {
        turn.yaw = {};
        turned.yaw = desired.yaw;
    } else {
        turn.yaw = spedUp(turn.yaw, accelerationOf(spring, spring.stiffness, yawOff, turn.yaw), duration);
        turned.yaw = nx::normalizeAngle(view.yaw + turnedBy(turn.yaw, duration));
    }
    const nx::PerSecondSquared pitchStiffness = spring.stiffness * rules.pitchStiffness.value();
    const nx::Degrees pitchOff = nx::normalizeAngle(desired.pitch - view.pitch);
    turn.pitch = spedUp(turn.pitch, accelerationOf(spring, pitchStiffness, pitchOff, turn.pitch), duration);
    turned.pitch =
        std::clamp(view.pitch + turnedBy(turn.pitch, duration), -rules.pitchLimit, rules.pitchLimit);
    return turned;
}

auto isTurningFaster(const BotViewTurn& turn, nx::DegreesPerSecond speed) noexcept -> bool {
    const auto isFaster = [speed](nx::DegreesPerSecond about) {
        return about >= speed || about <= -speed;
    };
    return isFaster(turn.pitch) || isFaster(turn.yaw);
}

void settleAimError(BotAimError& error, const BotAimMoment& moment, const BotAimRules& rules,
                    nx::Random& random) noexcept {
    if (moment.now >= error.nextGoalAt) {
        float accuracy = moment.skill.value();
        if (accuracy < 1.0F) {
            // Its aim improves as it focuses on where it aims, its view held steady.
            if (moment.isTurning) {
                error.steadySince = moment.now;
            }
            const float focusTime =
                std::max(rules.slowestFocus.count() * (1.0F - accuracy), rules.quickestFocus.count());
            const float focus = std::min(moment.now.since(error.steadySince, moment.rate).count() / focusTime,
                                         rules.bestFocus.value());
            accuracy = std::max(accuracy, focus);
        }
        const float bound = moment.range.value() * rules.errorPerRange.value() * (1.0F - accuracy);
        error.goal = nx::Vec3{.x = random.between(-bound, bound),
                              .y = random.between(-bound, bound),
                              .z = random.between(-bound, bound)};
        error.nextGoalAt = moment.now.after(
            nx::Seconds{random.between(rules.soonestNewError.count(), rules.latestNewError.count())},
            moment.rate);
    }
    // settleShare of the way each settleRate-th of a second, compounded over the command.
    const float kept =
        std::pow(1.0F - rules.settleShare.value(), moment.duration.count() * rules.settleRate.value());
    error.offset += (error.goal - error.offset) * (1.0F - kept);
}

auto leadSpot(nx::Vec3 spot, nx::Vec3 enemyVelocity, nx::Vec3 velocity, BotSkill skill,
              const BotAimRules& rules) noexcept -> nx::Vec3 {
    if (skill <= rules.leadingSkill) {
        return spot;
    }
    return spot + ((enemyVelocity - velocity) * rules.leadTime.count());
}

} // namespace cs
