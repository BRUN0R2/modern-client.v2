// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.game;
import nx.collision;
import cs.shared;

namespace cs {

namespace {

constexpr nx::log::Category kGameLog{"game"};

/// The world's gravity (sv_gravity), which a grenade falls at its share of.
constexpr float kWorldGravity = 800.0F;
/// A grenade stops on what it lies on slower than this (SV_Physics_Toss's 30, squared).
constexpr float kRestingSpeedSquared = 900.0F;
/// What is steeper than this is not ground (a plane's normal up).
constexpr float kGround = 0.7F;
/// A grenade rolling on the ground keeps this of its speed at each touch (BounceTouch).
constexpr float kRolling = 0.8F;
/// It sounds as it bounces the first 5 times, and lies still after the 10th (BounceTouch).
constexpr std::uint32_t kSoundingBounces = 5;
constexpr std::uint32_t kMostBounces = 10;
/// The players' sounds are heard where they are; a grenade's, where it was: makers past any player's.
constexpr std::uint32_t kGrenadeMakers = 32;
constexpr std::uint32_t kFirstGrenadeMaker = 4096;
/// RadiusDamage and RadiusFlash weigh what is 1 unit over the grenade.
constexpr float kBlastLift = 1.0F;

/// A grenade's sound `sound` at `position`, heard by every player there.
void soundAt(GameState& game, const LiveGrenade& grenade, const std::optional<GameSound>& sound,
             nx::Vec3 position) {
    if (!sound) {
        return;
    }
    const nx::game::TickTime& time = nx::game::tickTimeOf(game.world);
    game.events.grenadeSounds.push_back(
        PlayerSound{.player = PlayerIndex{kFirstGrenadeMaker + (grenade.serial % kGrenadeMakers)},
                    .made = SoundMade{.sound = &*sound, .at = time.now, .volume = sound->volume},
                    .position = position});
}

/// The thrower of `grenade`, when it is still in the game.
[[nodiscard]] auto throwerOf(GameState& game, const LiveGrenade& grenade) -> Player* {
    if (grenade.thrower.value >= game.players.size() || !game.players[grenade.thrower.value]) {
        return nullptr;
    }
    return &*game.players[grenade.thrower.value];
}

/// Explode3's RadiusDamage: each living player within the radius dealt the damage less as much as it is far,
/// through what is between, as the original's (without CZero's sight of it).
void explode(GameState& game, const LiveGrenade& grenade, const WeaponThrow& throwing) {
    const nx::Vec3 source = grenade.origin + nx::Vec3{.z = kBlastLift};
    const float damage = throwing.damage.value();
    const float radius = throwing.radius.value();
    Player* const thrower = throwerOf(game, grenade);
    for (Player& victim : playersIn(game.players)) {
        if (!victim.status.isAlive()) {
            continue;
        }
        const nx::Vec3 toward = victim.movement.origin - source;
        const float distance = nx::length(toward);
        const float dealt = damage - (distance * damage / radius);
        if (distance > radius || dealt < 1.0F) {
            continue;
        }
        const nx::Vec3 direction = distance > 0.0F ? toward / distance : nx::Vec3{.z = 1.0F};
        if (thrower != nullptr) {
            attackEntity(game, *thrower, victim.body,
                         nx::game::Attack{.attacker = thrower->body,
                                          .damage = nx::Damage{dealt},
                                          .direction = direction,
                                          .trace = {.fraction = 0.0F, .end = victim.movement.origin}});
        } else {
            hurt(game, victim, {.damage = nx::Damage{dealt}, .kind = HarmKind::Volume});
        }
    }
}

/// Explode's RadiusFlash: each living player who sees the flash blinded.
void flash(GameState& game, const LiveGrenade& grenade, const WeaponThrow& throwing) {
    const nx::Vec3 source = grenade.origin + nx::Vec3{.z = kBlastLift};
    for (Player& victim : playersIn(game.players)) {
        if (!victim.status.isAlive()) {
            continue;
        }
        const nx::Vec3 eye = eyeOf(victim.movement);
        // Seen: nothing but the victim between the flash and its eye.
        nx::game::ServerTrace way = nx::game::hullTrace(nx::collision::Hull::Point, source, eye);
        const nx::game::SolidHit seen =
            nx::game::traceSolidsHit(game.world, game.map.hulls(), solidsNow(game), way);
        if (seen.trace.fraction < 1.0F && seen.entity != victim.body) {
            continue;
        }
        const nx::Vec3 forward = nx::angleVectors(victim.view).forward;
        if (const std::optional<Blinding> blinded =
                blindingOf(source, eye, forward, throwing.damage.value(), throwing.radius.value())) {
            // A flash on one blinded already: the longer and the whiter of them.
            if (victim.blinding) {
                victim.blinding->fade = std::max(victim.blinding->fade, blinded->fade);
                victim.blinding->hold = std::max(victim.blinding->hold, blinded->hold);
                victim.blinding->alpha = std::max(victim.blinding->alpha, blinded->alpha);
            } else {
                victim.blinding = blinded;
            }
        }
    }
}

/// The grenade goes off: as its kind does, heard, told to every client.
void goOff(GameState& game, const LiveGrenade& grenade) {
    const WeaponThrow& throwing = *game.content.weapons.weapon(grenade.weapon).throwing;
    nx::log::info(kGameLog, "a grenade goes off");
    switch (throwing.kind) {
    case GrenadeKind::Explosive:
        explode(game, grenade, throwing);
        break;
    case GrenadeKind::Flash:
        flash(game, grenade, throwing);
        break;
    case GrenadeKind::Smoke:
    case GrenadeKind::Count:
        break;
    }
    soundAt(game, grenade, throwing.sound, grenade.origin);
    game.events.grenadeBlasts.push_back(GrenadeBlast{.kind = throwing.kind, .position = grenade.origin});
}

/// One tick of `grenade` flying, of `throwing`, `delta` long (SV_Physics_Toss for MOVETYPE_BOUNCE,
/// BounceTouch).
void fly(GameState& game, LiveGrenade& grenade, const WeaponThrow& throwing, float delta) {
    if (grenade.isResting) {
        return;
    }
    grenade.velocity.z -= throwing.gravity * kWorldGravity * delta;
    const nx::Vec3 end = grenade.origin + (grenade.velocity * delta);
    nx::game::ServerTrace way = nx::game::hullTrace(nx::collision::Hull::Point, grenade.origin, end);
    if (const Player* const thrower = throwerOf(game, grenade)) {
        way.ignored = thrower->body; // its thrower it does not touch
    }
    const nx::game::SolidHit hit =
        nx::game::traceSolidsHit(game.world, game.map.hulls(), solidsNow(game), way);
    grenade.origin = hit.trace.end;
    if (hit.trace.fraction >= 1.0F) {
        return;
    }
    // ClipVelocity, its overbounce 2 less the friction.
    const nx::Vec3 normal = hit.trace.plane.normal;
    const float backoff = 2.0F - throwing.friction;
    grenade.velocity = grenade.velocity - (normal * (nx::dot(grenade.velocity, normal) * backoff));
    if (normal.z > kGround) {
        if (nx::dot(grenade.velocity, grenade.velocity) < kRestingSpeedSquared) {
            grenade.isResting = true;
            grenade.velocity = nx::Vec3{};
            return;
        }
        grenade.velocity = grenade.velocity * kRolling;
    }
    if (grenade.bounces < kSoundingBounces) {
        soundAt(game, grenade, throwing.bounceSound, grenade.origin);
    }
    if (++grenade.bounces >= kMostBounces) {
        grenade.isResting = true;
        grenade.velocity = nx::Vec3{};
    }
}

} // namespace

auto blindingOf(nx::Vec3 source, nx::Vec3 eye, nx::Vec3 forward, float damage, float radius)
    -> std::optional<Blinding> {
    const float distance = nx::length(source - eye);
    const float adjusted = damage - (distance * damage / radius);
    if (distance > radius || adjusted <= 0.0F) {
        return std::nullopt;
    }
    // In front of the view or behind it: the sign of the dot alone.
    constexpr std::uint8_t kInFront = 255;
    constexpr std::uint8_t kBehind = 200;
    // Behind: held the strength over 3.5 seconds, faded 1.75 times it; in front, over 1.5 and 3 times it.
    constexpr float kHoldBehind = 3.5F;
    constexpr float kFadeBehind = 1.75F;
    constexpr float kHoldInFront = 1.5F;
    constexpr float kFadeInFront = 3.0F;
    if (nx::dot(source - eye, forward) < 0.0F) {
        return Blinding{.hold = nx::Seconds{adjusted / kHoldBehind},
                        .fade = nx::Seconds{adjusted * kFadeBehind},
                        .alpha = kBehind};
    }
    return Blinding{.hold = nx::Seconds{adjusted / kHoldInFront},
                    .fade = nx::Seconds{adjusted * kFadeInFront},
                    .alpha = kInFront};
}

void throwGrenade(GameState& game, const Player& thrower, const Throw& thrown) {
    const WeaponThrow& throwing = *game.content.weapons.weapon(thrown.weapon).throwing;
    const nx::game::TickTime& time = nx::game::tickTimeOf(game.world);
    const PlayerIndex index = game.world.get<PlayerBody>(thrower.body)->player;
    game.grenades.push_back(LiveGrenade{.weapon = thrown.weapon,
                                        .thrower = index,
                                        .origin = thrown.origin,
                                        .velocity = thrown.velocity,
                                        .goesOffAt = time.now.after(throwing.fuse, time.rate),
                                        .serial = game.grenadeSerial++});
}

void thinkGrenades(GameState& game) {
    const nx::game::TickTime& time = nx::game::tickTimeOf(game.world);
    const float delta = time.delta().count();
    std::erase_if(game.grenades, [&](LiveGrenade& grenade) {
        const WeaponThrow& throwing = *game.content.weapons.weapon(grenade.weapon).throwing;
        fly(game, grenade, throwing, delta);
        // A smoke grenade goes off once it lies still too (SG_TumbleThink).
        const bool isDue =
            time.now >= grenade.goesOffAt && (throwing.kind != GrenadeKind::Smoke || grenade.isResting);
        if (isDue) {
            goOff(game, grenade);
        }
        return isDue;
    });
}

} // namespace cs
