// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.math;
import nx.game;

namespace cs {

namespace {

/// The streams of random numbers the weapons draw from: which sequence they play and which way they
/// kick, and how far a shot strays.
constexpr std::string_view kRandomStream = "weapons";
constexpr std::string_view kSpreadStream = "spread";

/// A gun clicks on an empty clip, and fires no sooner than this after (PlayEmptySound), in seconds.
constexpr float kEmptyClickDelay = 0.2F;
/// A reloaded weapon idles this long after its reload is over (DefaultReload), in seconds.
constexpr float kIdleAfterReload = 0.5F;
/// The lowest the timers count down to (PostThink).
constexpr float kLowestNextShot = -1.0F;
constexpr float kLowestTimer = -0.001F;
constexpr double kSecondsPerMillisecond = 0.001;
/// With its button up, a burst keeps this many shots at most, loses none for a while, then one a
/// step (ItemPostFrame).
constexpr std::uint32_t kBurstShotsKept = 15;
constexpr nx::Seconds kBurstWaneDelay{0.4F};
constexpr nx::Seconds kBurstWaneStep{0.0225F};

/// One of `sequences`, at random.
[[nodiscard]] auto anyOf(std::span<const std::uint32_t> sequences, nx::Random& random) -> std::uint32_t {
    return sequences[nx::narrow<std::size_t>(random.below(sequences.size()))];
}

/// How far a shot of `firing` strays, its weapon's accuracy `accuracy`, as `shooter` moves: in the
/// air, else moving faster than the spread's limit, else ducked, else standing; its inaccuracy the
/// accuracy for a burst's, 1 less it for a paced one's.
[[nodiscard]] auto spreadOf(const WeaponFiring& firing, float accuracy, const Shooter& shooter) -> float {
    const WeaponSpread& spread = firing.spread;
    const SpreadTerm& term = !shooter.isOnGround                             ? spread.air
                             : shooter.horizontalSpeed > spread.movingAbove  ? spread.moving
                             : shooter.isDucked && spread.ducked.has_value() ? *spread.ducked
                                                                             : spread.standing;
    const double inaccuracy = firing.accuracy.burst ? double{accuracy} : 1.0 - double{accuracy};
    return static_cast<float>(double{term.base} + (double{term.scale} * inaccuracy));
}

/// The kick of `kicks` as `shooter` moves: moving at all, else in the air, else ducked, else standing.
[[nodiscard]] auto kickOf(const KickByMovement& kicks, const Shooter& shooter) -> const Kick& {
    if (shooter.horizontalSpeed > nx::UnitsPerSecond{}) {
        return kicks.moving;
    }
    if (!shooter.isOnGround) {
        return kicks.air;
    }
    return shooter.isDucked ? kicks.ducked : kicks.standing;
}

/// Kicks `punch` by `kick`, the `shots`th shot of a burst, to the side `kicksLeft` says, which it
/// turns over at random (KickBack).
void kickBack(const Kick& kick, std::uint32_t shots, bool& kicksLeft, nx::EulerAngles& punch,
              nx::Random& random) {
    const auto fired = static_cast<float>(shots);
    const double up = shots == 1 ? double{kick.upBase} : double{(fired * kick.upModifier) + kick.upBase};
    const float lateral = shots == 1 ? kick.lateralBase : (fired * kick.lateralModifier) + kick.lateralBase;
    punch.pitch = nx::Degrees{std::max(static_cast<float>(double{punch.pitch.value()} - up), -kick.upMax)};
    punch.yaw = kicksLeft ? nx::Degrees{std::min(punch.yaw.value() + lateral, kick.lateralMax)}
                          : nx::Degrees{std::max(punch.yaw.value() - lateral, -kick.lateralMax)};
    if (random.below(std::uint64_t{kick.directionChange} + 1) == 0) {
        kicksLeft = !kicksLeft;
    }
}

} // namespace

auto Inventory::give(WeaponIndex weapon, bool autoSwitch, const nx::game::TickTime& time, nx::Random& random)
    -> bool {
    const Weapon& given = m_catalog->weapon(weapon);
    const std::uint32_t rounds = given.magazine ? given.magazine->clipSize : 0;
    if (const std::optional<std::size_t> owned = find(weapon)) {
        takeRounds(*owned, rounds);
        return false;
    }
    m_weapons.push_back(
        OwnedWeapon{.weapon = weapon, .accuracy = given.firing ? given.firing->accuracy.initial : 0.0F});
    const std::size_t index = m_weapons.size() - 1;
    takeRounds(index, rounds);
    if (!m_held || (autoSwitch && given.weight > m_catalog->weapon(m_weapons[*m_held].weapon).weight)) {
        draw(index, time, random);
    }
    return true;
}

auto Inventory::giveAmmo(std::string_view ammo, std::uint32_t rounds) -> std::uint32_t {
    const AmmoType* const type = m_catalog->findAmmo(ammo);
    if (type == nullptr) {
        return 0;
    }
    std::uint32_t& carried = m_reserves.try_emplace(type->id, 0).first->second;
    const std::uint32_t taken = std::min(rounds, type->maxCarried - std::min(carried, type->maxCarried));
    carried += taken;
    return taken;
}

void Inventory::select(const UserCommand& command, const nx::game::TickTime& time) {
    if (!command.weaponSelect) {
        return;
    }
    nx::Random random{command.number.value, kRandomStream};
    if (const std::optional<std::size_t> selected = find(*command.weaponSelect);
        selected && selected != m_held) {
        draw(*selected, time, random);
    }
}

auto Inventory::frame(const UserCommand& command, Shooter& shooter, const nx::game::TickTime& time)
    -> std::vector<Shot> {
    std::vector<Shot> shots;
    // As the original's PostThink, which counts the timers down once the frame is over, the command's
    // milliseconds made seconds in double, then float.
    const auto frameTime = static_cast<float>(double{command.duration.count()} * kSecondsPerMillisecond);
    const auto over = [&] {
        countDown(frameTime);
        return shots;
    };
    if (!m_held || m_nextAttack > 0.0F) {
        return over();
    }
    OwnedWeapon& owned = m_weapons[*m_held];
    const Weapon& weapon = m_catalog->weapon(owned.weapon);
    nx::Random random{command.number.value, kRandomStream};
    if (m_isReloading) {
        // The reload is over: as many rounds as the clip takes and the player carries go into it.
        std::uint32_t& carried = m_reserves.try_emplace(weapon.magazine->ammo, 0).first->second;
        const std::uint32_t taken = std::min(weapon.magazine->clipSize - owned.clip, carried);
        owned.clip += taken;
        carried -= taken;
        m_isReloading = false;
    }
    const bool attacks = command.buttons.test(Button::Attack);
    if (attacks && owned.nextShot <= 0.0F) {
        if (weapon.firing) {
            if (const std::optional<Shot> shot =
                    fire(owned, weapon, shooter, command.number.value, time, random)) {
                shots.push_back(*shot);
            }
        }
        return over();
    }
    if (command.buttons.test(Button::Reload) && weapon.reload && !m_isReloading && owned.nextShot < 0.0F) {
        reload(owned, weapon, time, random);
        return over();
    }
    if (attacks) {
        return over();
    }
    // No fire button down: the burst wanes, a pistol's at once.
    if (owned.isBursting) {
        owned.isBursting = false;
        owned.shotsFired = std::min(owned.shotsFired, kBurstShotsKept);
        owned.fewerShotsAt = time.now.after(kBurstWaneDelay, time.rate);
    }
    if (weapon.slot == WeaponSlot::Secondary) {
        owned.shotsFired = 0;
    } else if (owned.shotsFired > 0 && owned.fewerShotsAt < time.now) {
        owned.fewerShotsAt = time.now.after(kBurstWaneStep, time.rate);
        --owned.shotsFired;
    }
    // An empty clip is reloaded once the weapon may fire again; else the weapon idles.
    const bool isUsable = !weapon.magazine || owned.clip > 0 || reserve(weapon.magazine->ammo) > 0;
    if (isUsable && weapon.reload && owned.clip == 0 && owned.nextShot < 0.0F) {
        reload(owned, weapon, time, random);
        return over();
    }
    idle(time, random);
    return over();
}

auto Inventory::lastWeapon() const -> std::optional<WeaponIndex> {
    if (m_before && m_before != m_held) {
        return m_weapons[*m_before].weapon;
    }
    for (const WeaponSlot slot : {WeaponSlot::Primary, WeaponSlot::Secondary, WeaponSlot::Knife}) {
        // The original keeps the weapons of a slot newest first, and looks at the first alone.
        std::optional<std::size_t> newest;
        for (std::size_t index = 0; index < m_weapons.size(); ++index) {
            if (m_catalog->weapon(m_weapons[index].weapon).slot == slot) {
                newest = index;
            }
        }
        if (newest && newest != m_held) {
            return m_weapons[*newest].weapon;
        }
    }
    return std::nullopt;
}

auto Inventory::held() const noexcept -> const OwnedWeapon* {
    return m_held ? &m_weapons[*m_held] : nullptr;
}

auto Inventory::reserve(std::string_view ammo) const noexcept -> std::uint32_t {
    const auto found = m_reserves.find(ammo);
    return found == m_reserves.end() ? 0 : found->second;
}

auto Inventory::maxSpeed() const noexcept -> nx::UnitsPerSecond {
    return m_held ? m_catalog->weapon(m_weapons[*m_held].weapon).maxSpeed : kUnarmedSpeed;
}

auto Inventory::find(WeaponIndex weapon) const noexcept -> std::optional<std::size_t> {
    const auto found = std::ranges::find(m_weapons, weapon, &OwnedWeapon::weapon);
    return found == m_weapons.end() ? std::nullopt
                                    : std::optional{nx::narrow<std::size_t>(found - m_weapons.begin())};
}

void Inventory::takeRounds(std::size_t index, std::uint32_t rounds) {
    OwnedWeapon& owned = m_weapons[index];
    const std::optional<WeaponMagazine>& magazine = m_catalog->weapon(owned.weapon).magazine;
    if (!magazine) {
        return;
    }
    std::uint32_t besides = rounds;
    if (owned.clip == 0) {
        owned.clip = std::min(rounds, magazine->clipSize);
        besides -= owned.clip;
    }
    std::ignore = giveAmmo(magazine->ammo, besides);
}

void Inventory::draw(std::size_t index, const nx::game::TickTime& time, nx::Random& random) {
    m_before = m_held;
    m_held = index;
    m_isReloading = false; // the one put away stops its reload
    OwnedWeapon& owned = m_weapons[index];
    const Weapon& weapon = m_catalog->weapon(owned.weapon);
    if (weapon.firing) {
        owned.accuracy = weapon.firing->accuracy.initial;
        owned.shotsFired = 0;
    }
    owned.fewerShotsAt = time.now;
    m_animation = ViewAnimation{
        .weapon = owned.weapon, .sequence = anyOf(weapon.view.drawSequences, random), .start = time.now};
    m_nextIdle = kIdleAfterDraw;
    m_nextAttack = kAttackAfterDraw;
}

auto Inventory::fire(OwnedWeapon& owned, const Weapon& weapon, Shooter& shooter, std::uint32_t seed,
                     const nx::game::TickTime& time, nx::Random& random) -> std::optional<Shot> {
    const WeaponFiring& firing = *weapon.firing;
    // The spread is of the accuracy before this shot changes it.
    const float spread = spreadOf(firing, owned.accuracy, shooter);
    if (firing.isAutomatic) {
        owned.isBursting = true;
        ++owned.shotsFired;
    } else if (++owned.shotsFired > 1) {
        return std::nullopt; // once a press
    }
    if (const std::optional<BurstAccuracy>& burst = firing.accuracy.burst) {
        // The shots cubed over the divisor in whole numbers, as the original divides them.
        const std::uint64_t shots = owned.shotsFired;
        owned.accuracy = std::min(
            static_cast<float>((shots * shots * shots) / burst->divisor) + burst->offset, burst->maximum);
    } else if (const std::optional<PacedAccuracy>& paced = firing.accuracy.paced) {
        if (owned.lastShot) {
            const nx::Seconds since = time.now.since(*owned.lastShot, time.rate);
            owned.accuracy -= (paced->threshold - since).count() * paced->rate;
            owned.accuracy = std::clamp(owned.accuracy, paced->minimum, paced->maximum);
        }
        owned.lastShot = time.now;
    }
    if (owned.clip == 0) {
        owned.nextShot = kEmptyClickDelay;
        return std::nullopt;
    }
    --owned.clip;
    owned.nextShot = firing.cycle.count();
    m_nextIdle = firing.idleAfterShot.count();
    const std::uint32_t sequence = owned.clip == 0 && firing.lastShotSequence
                                       ? *firing.lastShotSequence
                                       : anyOf(firing.shootSequences, random);
    m_animation = ViewAnimation{.weapon = owned.weapon, .sequence = sequence, .start = time.now};

    // Along the view the kick turns, strayed at random by up to twice the spread each way, as the
    // sum of two even draws (FireBullets3); then the kick.
    const nx::AngleVectors axes =
        nx::angleVectors(nx::EulerAngles{.pitch = shooter.view.pitch + shooter.punch.pitch,
                                         .yaw = shooter.view.yaw + shooter.punch.yaw,
                                         .roll = shooter.view.roll + shooter.punch.roll});
    nx::Random stray{seed, kSpreadStream};
    const auto halfUnit = [&stray] {
        return stray.nextUnit() - 0.5;
    };
    const auto right = static_cast<float>(halfUnit() + halfUnit());
    const auto up = static_cast<float>(halfUnit() + halfUnit());
    const Shot shot{.origin = shooter.eye,
                    .direction = axes.forward + (axes.right * (right * spread)) + (axes.up * (up * spread)),
                    .spread = spread};
    if (firing.recoil.kicks) {
        kickBack(kickOf(*firing.recoil.kicks, shooter), owned.shotsFired, owned.kicksLeft, shooter.punch,
                 random);
    } else if (firing.recoil.punch) {
        shooter.punch.pitch = shooter.punch.pitch - *firing.recoil.punch;
    }
    return shot;
}

void Inventory::reload(OwnedWeapon& owned, const Weapon& weapon, const nx::game::TickTime& time,
                       nx::Random& random) {
    const std::uint32_t carried = reserve(weapon.magazine->ammo);
    if (carried == 0 || owned.clip >= weapon.magazine->clipSize) {
        return;
    }
    m_nextAttack = weapon.reload->time.count();
    m_nextIdle = weapon.reload->time.count() + kIdleAfterReload;
    m_isReloading = true;
    m_animation = ViewAnimation{
        .weapon = owned.weapon, .sequence = anyOf(weapon.reload->sequences, random), .start = time.now};
    if (weapon.firing) {
        owned.accuracy = weapon.firing->accuracy.initial;
        if (weapon.firing->accuracy.burst) {
            owned.shotsFired = 0;
            owned.isBursting = false;
        }
    }
}

void Inventory::idle(const nx::game::TickTime& time, nx::Random& random) {
    if (!m_held || m_nextIdle > 0.0F) {
        return;
    }
    const OwnedWeapon& owned = m_weapons[*m_held];
    const Weapon& weapon = m_catalog->weapon(owned.weapon);
    if (weapon.magazine && owned.clip == 0 && !weapon.view.idlesWhenEmpty) {
        return; // it idles once its clip has rounds again
    }
    const double drawn = random.nextUnit();
    double shares = 0.0;
    const WeaponIdle* chosen = &weapon.view.idles.back();
    for (const WeaponIdle& candidate : weapon.view.idles) {
        shares += double{candidate.share};
        if (drawn < shares) {
            chosen = &candidate;
            break;
        }
    }
    m_animation = ViewAnimation{.weapon = owned.weapon, .sequence = chosen->sequence, .start = time.now};
    m_nextIdle = chosen->wait.count();
}

void Inventory::countDown(float frameTime) noexcept {
    for (OwnedWeapon& owned : m_weapons) {
        owned.nextShot = std::max(owned.nextShot - frameTime, kLowestNextShot);
    }
    m_nextIdle = std::max(m_nextIdle - frameTime, kLowestTimer);
    m_nextAttack = std::max(m_nextAttack - frameTime, kLowestTimer);
}

} // namespace cs
