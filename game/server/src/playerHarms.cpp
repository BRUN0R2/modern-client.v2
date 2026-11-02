// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.game;
import cs.shared;

namespace cs {

namespace {

/// The stream of random numbers an attack on a player draws from, of the attacker's command and the victim.
constexpr std::string_view kAttackStream = "player attack";
/// The stream of random numbers the blood a player's attack sheds draws from, of its command and the
/// victim.
constexpr std::string_view kBloodStream = "player blood";
/// The stream of random numbers the blood a player's attack makes an entity's body shed draws from, of
/// its command and the entity.
constexpr std::string_view kEntityBloodStream = "entity blood";

/// The way of `vector`, of length 1, as the original's Vector::Normalize makes it: its parts times the
/// inverse of its length; straight up for none.
[[nodiscard]] auto wayOf(nx::Vec3 vector) noexcept -> nx::Vec3 {
    const float length = nx::length(vector);
    if (length == 0.0F) {
        return nx::Vec3{.z = 1.0F};
    }
    return vector * (1.0F / length);
}

/// The seed of the random numbers `attacker`'s attack on what it met, `victim`, draws from: of its
/// command and the victim.
[[nodiscard]] auto attackSeedOf(const Player& attacker, std::uint64_t victim) noexcept -> std::uint64_t {
    constexpr unsigned kVictimBits = 32;
    return (static_cast<std::uint64_t>(attacker.randomSeed.value) << kVictimBits) ^ victim;
}

} // namespace

auto stampTargetOf(const nx::ecs::World& world, std::optional<nx::ecs::Entity> entity) -> StampTarget {
    if (!entity) {
        return {};
    }
    // What a mark lies on is a solid brush entity: it has its model and its place.
    const nx::game::BrushModel* const model = world.get<nx::game::BrushModel>(*entity);
    const nx::game::Transform* const transform = world.get<nx::game::Transform>(*entity);
    nx::expects(model != nullptr && transform != nullptr, "stampTargetOf: a brush entity");
    if (model == nullptr || transform == nullptr) {
        return {};
    }
    return StampTarget{.model = model->index, .origin = transform->origin, .angles = transform->angles};
}

void feelHurt(Player& victim, const nx::game::Attack& attack, std::optional<Team> team) {
    const nx::Vec3 back{.x = -attack.direction.x, .y = -attack.direction.y};
    const float across = nx::length(back);
    if (across > 0.0F && victim.status.isAlive()) {
        victim.hurtFrom = HurtFrom{.toward = back / across, .team = team};
    }
}

void bleed(GameState& game, const nx::game::Attack& attack, const Bleeding& bleeding, nx::Random& random) {
    // TraceBleed: a hit that dealt nothing sheds nothing.
    if (bleeding.dealt <= nx::Damage{}) {
        return;
    }
    const BloodRules& rules = game.content.blood;
    const nx::Vec3 from = attack.trace.end;
    if (const std::optional<std::uint32_t> size = spraySizeOf(rules.spray, bleeding.dealt)) {
        game.events.bloodSprays.push_back(BloodSpray{.position = from,
                                                     .direction = attack.direction,
                                                     .color = bleeding.color,
                                                     .size = *size,
                                                     .hitGroup = bleeding.hitGroup});
    }
    for (const nx::Vec3 way : bleedingWaysOf(rules, bleeding.dealt, attack.direction, random)) {
        const nx::game::SolidHit hit = nx::game::traceSolidsHit(game.world, game.map.hulls(),
                                                                {.start = from,
                                                                 .end = from + (way * rules.reach.value()),
                                                                 .type = nx::game::TraceType::NoMonsters,
                                                                 .ignored = bleeding.body});
        if (hit.trace.fraction >= 1.0F) {
            continue;
        }
        const std::optional<std::uint32_t> decal = bloodDecalOf(rules, random);
        // A decal lies on the world or on a brush entity alone (UTIL_DecalTrace).
        const bool isBrush = !hit.entity || game.world.has<nx::game::BrushModel>(*hit.entity);
        if (decal && isBrush) {
            game.events.bloodMarks.push_back(BloodMark{
                .position = hit.trace.end, .on = stampTargetOf(game.world, hit.entity), .decal = *decal});
        }
    }
}

void die(GameState& game, Player& player, Player* killer) {
    nx::log::info(kGameLog, "{} dies", player.name);
    makeSound(game, player, game.content.playerHarm.death);
    const bool isKilledByAnother = killer != nullptr && killer != &player;
    // DeathNotice, as PlayerKilled starts, while its killer holds the weapon that killed it.
    const OwnedWeapon* const held = isKilledByAnother ? killer->inventory.held() : nullptr;
    game.events.killNotices.push_back(KillNotice{
        .killer = isKilledByAnother ? std::optional{NoticedPlayer{.name = killer->name, .team = killer->team}}
                                    : std::nullopt,
        .victim = {.name = player.name, .team = player.team},
        .weapon = held != nullptr ? std::optional{held->weapon} : std::nullopt,
        .isHead = player.lastHitGroup == HitGroup::Head});
    ++player.score.deaths;
    if (killer == &player) {
        --player.score.frags;
    } else if (isKilledByAnother) {
        killer->score.frags += killer->team == player.team ? -1 : 1; // IPointsForKill
    }
    // PlayerKilled pays its killer, or makes it pay for a teammate.
    if (isKilledByAnother) {
        const KillMoney& kills = game.content.economy.kills;
        killer->status.money = addMoney(
            killer->status.money, killer->team == player.team ? nx::Money{} - kills.teammate : kills.enemy);
    }
    player.movement.isDead = true;
    animate(game, player, PlayerAction::Die);
    placeBody(game, player);
    // Its weapons go with it (PackDeadPlayerItems; the box it drops comes with dropping, TODO.md).
    player.inventory.removeAll();
    checkWinConditions(game);
}

void hurt(GameState& game, Player& player, const PlayerHarm& harm) {
    if (!player.status.isAlive()) {
        return;
    }
    const PlayerHarmRules& rules = game.content.playerHarm;
    const PlayerHarmed harmed =
        harmPlayer(player.status, player.timedHarms, harm, player.lastHitGroup, rules.armor);
    makeSound(game, player, painOf(rules, player.status, player.lastHitGroup, harmed.isArmored));
    if (harmed.isKilled) {
        die(game, player, nullptr);
    }
}

void land(GameState& game, Player& player, nx::Damage damage) {
    // Weighed before the damage is cut to its whole points.
    if (damage.value() > player.status.health.value()) {
        makeSound(game, player, game.content.playerHarm.splat);
    }
    player.lastHitGroup = HitGroup::Generic; // PostThink: a fall meets it in no group.
    hurt(game, player, PlayerHarm{.damage = damage, .kind = HarmKind::Fall});
    player.movement.punch.pitch = nx::Degrees{};
}

void touchHurtVolumes(GameState& game, Player& player) {
    for (const nx::game::VolumeHarm& harm :
         nx::game::touchHurtVolumes(game.world, game.map.hulls(), player.body)) {
        if (harm.damage < nx::Damage{}) {
            healPlayer(player.status, nx::HitPoints{-harm.damage.value()}); // TakeHealth
        } else {
            hurt(game, player, PlayerHarm{.damage = harm.damage, .kind = harmKindOf(harm.kind)});
        }
    }
}

auto headTopOf(GameState& game, nx::ecs::Entity entity) -> nx::Vec3 {
    std::optional<nx::Vec3> head;
    if (game.world.has<PlayerBody>(entity)) {
        head = playerHeadTopOf(game.world, entity);
    } else if (game.world.has<Hostage>(entity)) {
        head = hostageHeadTopOf(game.world, entity);
    } else if (game.world.has<EntityBody>(entity)) {
        head = entityHeadTopOf(game.world, entity);
    }
    if (head) {
        return *head;
    }
    const nx::game::BoxSolidity* const solidity = game.world.get<nx::game::BoxSolidity>(entity);
    nx::expects(solidity != nullptr, "Game::headTopOf: a body of no hitbox is solid as its box");
    const nx::Vec3 origin = game.world.get<nx::game::Transform>(entity)->origin;
    const nx::Vec3 middle = solidity->box.center();
    return {.x = origin.x + middle.x, .y = origin.y + middle.y, .z = origin.z + solidity->box.max.z};
}

void attackPlayer(GameState& game, Player& attacker, Player& victim, const nx::game::Attack& attack) {
    if (!victim.status.isAlive()) {
        return;
    }
    const PlayerHarmRules& rules = game.content.playerHarm;
    // TraceAttack: a teammate's attack sheds no blood, nor hurts, while friendly fire is off.
    const bool isTeammate = attacker.team == victim.team;
    const bool isSpared = isTeammate && !game.cvars.isFriendlyFireOn();
    victim.lastHitGroup = hitGroupOf(attack.trace);
    nx::Damage damage = scaled(attack.damage, game.content.hitGroups.player.of(victim.lastHitGroup));
    // A helmet sparks, whoever's attack met it.
    if (sparksOffHelmet(victim.lastHitGroup, victim.status.armorKind)) {
        const HelmetSparks& sparks = rules.helmetSparks;
        game.events.streakSplashes.push_back(StreakSplash{.position = attack.trace.end,
                                                          .direction = attack.trace.plane.normal,
                                                          .color = sparks.color,
                                                          .count = sparks.count,
                                                          .speed = sparks.speed,
                                                          .spread = sparks.spread});
    }
    if (isSpared) {
        return;
    }
    feelHurt(victim, attack, attacker.team);
    const std::uint64_t seed = attackSeedOf(attacker, game.world.get<PlayerBody>(victim.body)->player.value);
    nx::Random random{seed, kAttackStream};
    punchByHit(victim.movement.punch, victim.lastHitGroup, damage, victim.status.armorKind, random);
    if (shedsBlood(victim.lastHitGroup, victim.status.armorKind)) {
        nx::Random blood{seed, kBloodStream};
        // BloodSplat: a head streams its blood the way the hit went, before it bleeds.
        if (victim.lastHitGroup == HitGroup::Head) {
            const BloodStreamRules& stream = game.content.blood.stream;
            game.events.bloodStreams.push_back(BloodStream{.position = attack.trace.end,
                                                           .direction = attack.direction,
                                                           .color = stream.color,
                                                           .speed = streamSpeedOf(stream, damage, blood)});
        }
        bleed(game, attack,
              {.body = victim.body,
               .dealt = damage,
               .hitGroup = victim.lastHitGroup,
               .color = game.content.blood.spray.color},
              blood);
    }
    // TakeDamage.
    if (isTeammate) {
        damage = scaled(damage, rules.attacks.teammateShare);
    }
    ArmorShares armor = rules.armor;
    const OwnedWeapon* const held = attacker.inventory.held();
    const WeaponHarm* const harm =
        held != nullptr ? &game.content.weapons.weapon(held->weapon).harm : nullptr;
    if (harm != nullptr) {
        armor.ratio = static_cast<float>(static_cast<double>(armor.ratio) * harm->armorRatio);
    }
    const bool isLargeFlinch =
        harm != nullptr && harm->largeFlinch && !victim.movement.isDucked && !isLeg(victim.lastHitGroup);
    if (!isLargeFlinch) {
        victim.speedShare = rules.attacks.flinchSpeedShare;
        animate(game, victim, PlayerAction::Flinch);
    } else {
        if (nx::length(victim.movement.velocity) < rules.attacks.knockbackBelowSpeed.value()) {
            const nx::Vec3 away = wayOf(victim.movement.origin - attacker.movement.origin);
            victim.movement.velocity = victim.movement.velocity + (away * rules.attacks.knockback.value());
            victim.speedShare = rules.attacks.largeFlinchSpeedShare;
        }
        animate(game, victim, PlayerAction::LargeFlinch);
    }
    const PlayerHarmed harmed =
        harmPlayer(victim.status, victim.timedHarms, PlayerHarm{.damage = damage, .kind = HarmKind::Bullet},
                   victim.lastHitGroup, armor);
    if (harmed.damage > nx::Damage{}) {
        attacker.damageDealt.push_back({.headTop = headTopOf(game, victim.body),
                                        .damage = harmed.damage,
                                        .isHead = victim.lastHitGroup == HitGroup::Head});
    }
    makeSound(game, victim, painOf(rules, victim.status, victim.lastHitGroup, harmed.isArmored));
    if (harmed.isKilled) {
        die(game, victim, &attacker);
    }
}

void attackEntity(GameState& game, Player& player, nx::ecs::Entity entity, const nx::game::Attack& attack) {
    if (const PlayerBody* const body = game.world.get<PlayerBody>(entity)) {
        attackPlayer(game, player, playerAt(game, body->player), attack);
        return;
    }
    // Over its head as the attack met it, before a death poses it anew and takes its solidity.
    const bool showsDamage = game.world.has<Hostage>(entity) || game.world.has<EntityBody>(entity);
    const std::optional<nx::Vec3> headTop =
        showsDamage ? std::optional{headTopOf(game, entity)} : std::nullopt;
    const nx::game::Harm harm = nx::game::attackEntity(game.world, entity, attack);
    if (harm.dealt > nx::Damage{} && headTop) {
        player.damageDealt.push_back({.headTop = *headTop,
                                      .damage = harm.dealt,
                                      .isHead = hitGroupOf(attack.trace) == HitGroup::Head});
    }
    if (game.world.has<Hostage>(entity)) {
        // The hostage's TakeDamage takes it from its attacker's account.
        player.status.money = addMoney(player.status.money,
                                       nx::Money{} - hostageHarmCost(harm, game.content.economy.hostageHarm));
    }
    // CBaseMonster::TraceAttack: a body of a kind that bleeds sprays its blood and traces it behind it.
    const EntityBody* const body = game.world.get<EntityBody>(entity);
    if (body == nullptr) {
        return;
    }
    if (const std::optional<std::uint8_t> blood =
            game.content.entities.type(body->type).definition.bloodColor) {
        nx::Random random{attackSeedOf(player, std::to_underlying(entity.index())), kEntityBloodStream};
        bleed(game, attack,
              {.body = entity, .dealt = harm.dealt, .hitGroup = hitGroupOf(attack.trace), .color = *blood},
              random);
    }
}

} // namespace cs
