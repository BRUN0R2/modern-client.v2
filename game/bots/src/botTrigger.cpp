// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.bots;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import cs.shared;

namespace cs {

namespace {

/// The range across the ground from `eye` to `spot`.
[[nodiscard]] auto groundRange(nx::Vec3 eye, nx::Vec3 spot) noexcept -> nx::HammerUnits {
    return nx::HammerUnits{std::hypot(spot.x - eye.x, spot.y - eye.y)};
}

/// The whole ticks nearest to `interval` at `rate`, one at least: a float tenth of a second is ten ticks
/// and a hair more, which GameTime::after would carry into an eleventh.
[[nodiscard]] auto wholeTicksOf(nx::Seconds interval, nx::TickRate rate) noexcept -> std::int64_t {
    const auto ticks = static_cast<std::int64_t>(
        std::llround(static_cast<double>(interval.count()) * static_cast<double>(rate.value())));
    return std::max<std::int64_t>(1, ticks);
}

[[nodiscard]] auto drawPause(const BotPause& pause, nx::Random& random) noexcept -> nx::Seconds {
    return nx::Seconds{random.between(pause.shortest.count(), pause.longest.count())};
}

/// The pause after a press of `shot` at `aimSpot`, which fires as fast as it may from then on with a
/// pistol above BotFireRules::rapidPistolSkill (StartRapidFire); with a gun, none to spray it.
[[nodiscard]] auto pauseAfterPress(BotTrigger& trigger, const BotShot& shot, nx::Vec3 aimSpot,
                                   const BotFireRules& rules, nx::Random& random) noexcept -> nx::Seconds {
    if (shot.heldSlot == WeaponSlot::Secondary) {
        if (shot.skill > rules.rapidPistolSkill) {
            trigger.isRapid = true;
            return {};
        }
        return drawPause(rules.pistolPause, random);
    }
    const nx::HammerUnits range = groundRange(shot.eye, aimSpot);
    if (shot.skill < rules.spraySkill || range < rules.sprayRange) {
        return {};
    }
    return drawPause(range > rules.distantRange ? rules.distantPause : rules.burstPause, random);
}

} // namespace

auto isAimedAt(nx::Vec3 eye, nx::Degrees viewYaw, nx::Vec3 aimSpot, nx::HammerUnits halfWidth) noexcept
    -> bool {
    const nx::HammerUnits range = groundRange(eye, aimSpot);
    if (range == nx::HammerUnits{}) {
        return true;
    }
    const nx::Vec3 toSpot = nx::Vec3{.x = aimSpot.x - eye.x, .y = aimSpot.y - eye.y} / range.value();
    const nx::Vec3 facing = nx::angleVectors(nx::EulerAngles{.yaw = viewYaw}).forward;
    // The cosine of the angle the half width spans at the range: cos(atan(halfSize / range)).
    const float tolerance = range.value() / std::hypot(range.value(), halfWidth.value());
    return nx::dot(toSpot, facing) > tolerance;
}

auto pullTrigger(BotTrigger& trigger, const BotShot& shot, const BotFireRules& rules,
                 nx::Random& random) noexcept -> bool {
    if (trigger.enemy != shot.pursued) {
        trigger.enemy = shot.pursued;
        trigger.enemySince = shot.now;
    }
    if (!shot.aimSpot) {
        trigger.isPressed = false;
        trigger.isRapid = false;
        trigger.isDown = false;
        return false;
    }
    if (shot.now < trigger.nextThinkAt) {
        trigger.isDown = trigger.isRapid ? !trigger.isDown : trigger.isPressed;
        return trigger.isDown;
    }
    // A full think: its buttons set anew (ResetCommand), pressed when it may (FireWeaponAtEnemy).
    trigger.nextThinkAt = shot.now.ticksLater(wholeTicksOf(rules.thinkInterval, shot.rate));
    trigger.isPressed = shot.now > trigger.nextPressAt &&
                        shot.now.since(trigger.enemySince, shot.rate) >= shot.attackDelay &&
                        isAimedAt(shot.eye, shot.viewYaw, *shot.aimSpot, rules.aimHalfWidth);
    if (trigger.isPressed) {
        const nx::Seconds pause = pauseAfterPress(trigger, shot, *shot.aimSpot, rules, random);
        trigger.nextPressAt = shot.now.after(pause - rules.thinkInterval, shot.rate);
    }
    trigger.isDown = trigger.isPressed;
    return trigger.isDown;
}

} // namespace cs
