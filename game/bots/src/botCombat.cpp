// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.bots;

import std;
import nx.core;
import nx.math;
import cs.shared;

namespace cs {

namespace {

/// The ways a dodge draws among: but the jump, the last, which only a first dodge may draw.
constexpr std::uint64_t kWaysButJump = std::to_underlying(BotDodge::Jump);
constexpr std::uint64_t kEveryWay = std::to_underlying(BotDodge::Count);

/// Whether a draw of `random` falls within `chance`, from 0 to 1.
[[nodiscard]] auto drawsWithin(nx::Random& random, float chance) noexcept -> bool {
    return random.between(0.0F, 1.0F) < chance;
}

} // namespace

auto spawnedCombat(const BotCombatRules& rules, nx::Random& random) noexcept -> BotCombat {
    return BotCombat{.range = nx::HammerUnits{random.between(rules.combatRange.nearest.value(),
                                                             rules.combatRange.farthest.value())}};
}

auto isFacing(nx::Vec3 enemyOrigin, nx::EulerAngles enemyView, nx::Vec3 origin) noexcept -> bool {
    const nx::Vec3 ahead = nx::angleVectors(enemyView).forward;
    const nx::Vec3 away = enemyOrigin - origin;
    return (ahead.x * away.x) + (ahead.y * away.y) < 0.0F;
}

auto fightMove(BotCombat& combat, const BotFight& fight, const BotCombatRules& rules,
               nx::Random& random) noexcept -> BotFightMove {
    const nx::Vec3 toEnemy = fight.enemyOrigin - fight.origin;
    const float range = std::hypot(toEnemy.x, toEnemy.y);
    if (!combat.isFighting) {
        // OnEnter: whether it crouches and holds, then whether it dodges this fight.
        const float crouchChance = range > rules.crouchFarFrom.value()
                                       ? rules.farCrouchChance
                                       : rules.nearCrouchChance * (1.0F - fight.aggression);
        const bool crouches = !fight.holdsKnife && drawsWithin(random, crouchChance) && fight.seesCrouched;
        const float dodgeChance =
            rules.dodgeChance * fight.skill.value() * (fight.holdsKnife ? rules.knifeDodgeFactor : 1.0F);
        combat = BotCombat{.isFighting = true,
                           .crouches = crouches,
                           .dodges = drawsWithin(random, dodgeChance),
                           .range = combat.range};
    }
    BotFightMove move{.crouches = combat.crouches};
    if (!combat.dodges || combat.crouches) {
        return move;
    }
    // Its combat range, within its slack, below the skill that holds its ground.
    if (fight.skill < rules.rangeSkill) {
        if (range > combat.range.value() + rules.rangeSlack.value()) {
            move.forward = fight.maxSpeed;
        } else if (range < combat.range.value() - rules.rangeSlack.value()) {
            move.forward = -fight.maxSpeed;
        }
    }
    if (range > rules.dodgeRange.value() || !isFacing(fight.enemyOrigin, fight.enemyView, fight.origin)) {
        combat.dodge = BotDodge::SteadyOn;
        combat.nextDodgeAt = nx::GameTime{};
    } else if (fight.now >= combat.nextDodgeAt) {
        const bool isMoving = nx::length(fight.velocity) >= rules.movingSpeed.value();
        const bool mayJump = !combat.hasDodged && fight.skill > rules.jumpSkill &&
                             drawsWithin(random, rules.jumpChance) && isMoving;
        const std::uint64_t ways = mayJump ? kEveryWay : kWaysButJump;
        BotDodge next = combat.dodge;
        do {
            next = static_cast<BotDodge>(random.below(ways));
        } while (combat.hasDodged && next == combat.dodge);
        combat.dodge = next;
        combat.nextDodgeAt = fight.now.after(
            nx::Seconds{random.between(rules.dodgeTime.shortest.count(), rules.dodgeTime.longest.count())},
            fight.rate);
        combat.hasDodged = true;
    }
    switch (combat.dodge) {
    case BotDodge::SlideLeft:
        move.side = -fight.maxSpeed;
        break;
    case BotDodge::SlideRight:
        move.side = fight.maxSpeed;
        break;
    case BotDodge::Jump:
        move.jumps = true;
        break;
    case BotDodge::SteadyOn:
    case BotDodge::Count:
        break;
    }
    return move;
}

} // namespace cs
