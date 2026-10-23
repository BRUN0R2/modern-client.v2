// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.math;
import nx.collision;
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

/// What `owned`, of `weapon`, shows, fires as and is reloaded as now: its alternate's while it is on,
/// where its alternate gives them.
[[nodiscard]] auto viewOf(const Weapon& weapon, const OwnedWeapon& owned) -> const WeaponView& {
    return owned.isAlternate && weapon.alternate->view ? *weapon.alternate->view : weapon.view;
}

[[nodiscard]] auto firingOf(const Weapon& weapon, const OwnedWeapon& owned)
    -> const std::optional<WeaponFiring>& {
    return owned.isAlternate && weapon.alternate->firing ? weapon.alternate->firing : weapon.firing;
}

[[nodiscard]] auto reloadOf(const Weapon& weapon, const OwnedWeapon& owned)
    -> const std::optional<WeaponReload>& {
    return owned.isAlternate && weapon.alternate->reload ? weapon.alternate->reload : weapon.reload;
}

/// Where a shot from `shooter` goes, strayed by `spread` at random from the stream of `seed`, along
/// the view the punch turns: each way the sum of two even draws (FireBullets3).
[[nodiscard]] auto shotFrom(const Shooter& shooter, float spread, std::uint32_t seed) -> Shot {
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
    return Shot{.origin = shooter.eye,
                .direction = axes.forward + (axes.right * (right * spread)) + (axes.up * (up * spread)),
                .spread = spread};
}

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

auto Inventory::give(WeaponIndex asked, bool autoSwitch, const nx::game::TickTime& time, nx::Random& random)
    -> bool {
    const WeaponIndex weapon = m_catalog->madeFor(asked);
    nx::expects(m_catalog->isComplete(weapon), "Inventory::give: a complete weapon");
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
    // A weapon given before it was replaced is still selected by its own name.
    std::optional<std::size_t> selected = find(*command.weaponSelect);
    if (!selected) {
        selected = find(m_catalog->madeFor(*command.weaponSelect));
    }
    if (selected && selected != m_held) {
        draw(*selected, time, random);
    }
}

auto Inventory::frame(const UserCommand& command, Shooter& shooter, const WeaponTracer& tracer,
                      const nx::game::TickTime& time) -> std::vector<Shot> {
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
    if (owned.followUpsLeft > 0) {
        // The rounds a shot fires after it, one a frame, whatever the buttons (FireRemaining).
        if (const std::optional<Shot> shot = fireFollowUp(owned, weapon, shooter, command.number.value)) {
            shots.push_back(*shot);
        }
    }
    const bool attacks = command.buttons.test(Button::Attack);
    // `Attack2` counts for a weapon with an alternate or melee attacks alone (HasSecondaryAttack).
    const bool switches =
        (weapon.alternate.has_value() || weapon.melee.has_value()) && command.buttons.test(Button::Attack2);
    if (switches && owned.nextSecondary <= 0.0F) {
        if (weapon.melee) {
            strike(owned, weapon.melee->stab, owned.stabs, shooter, tracer, time);
        } else {
            switchAlternate(owned, weapon, time);
        }
        return over();
    }
    if (attacks && owned.nextShot <= 0.0F) {
        if (weapon.melee) {
            strike(owned, weapon.melee->slash, owned.slashes, shooter, tracer, time);
        } else if (firingOf(weapon, owned)) {
            if (const std::optional<Shot> shot =
                    fire(owned, weapon, shooter, command.number.value, time, random)) {
                shots.push_back(*shot);
            }
        }
        return over();
    }
    const bool canReload = reloadOf(weapon, owned).has_value() && owned.followUpsLeft == 0;
    if (command.buttons.test(Button::Reload) && canReload && !m_isReloading && owned.nextShot < 0.0F) {
        reload(owned, weapon, time, random);
        return over();
    }
    if (attacks || switches) {
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
    if (isUsable && canReload && owned.clip == 0 && owned.nextShot < 0.0F) {
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

auto Inventory::takeSounds() -> std::vector<const GameSound*> {
    return std::exchange(m_sounds, {});
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

void Inventory::makeSound(const std::optional<GameSound>& sound) {
    if (sound) {
        m_sounds.push_back(&*sound);
    }
}

void Inventory::draw(std::size_t index, const nx::game::TickTime& time, nx::Random& random) {
    m_before = m_held;
    m_held = index;
    m_isReloading = false; // the one put away stops its reload
    OwnedWeapon& owned = m_weapons[index];
    const Weapon& weapon = m_catalog->weapon(owned.weapon);
    if (const std::optional<WeaponFiring>& firing = firingOf(weapon, owned)) {
        owned.accuracy = firing->accuracy.initial;
        owned.shotsFired = 0;
    }
    owned.fewerShotsAt = time.now;
    owned.followUpsLeft = 0; // its alternate stays as it was
    const WeaponView& view = viewOf(weapon, owned);
    m_animation = ViewAnimation{
        .weapon = owned.weapon, .sequence = anyOf(view.drawSequences, random), .start = time.now};
    makeSound(view.drawSound);
    m_nextIdle = kIdleAfterDraw;
    m_nextAttack = kAttackAfterDraw;
}

auto Inventory::fire(OwnedWeapon& owned, const Weapon& weapon, Shooter& shooter, std::uint32_t seed,
                     const nx::game::TickTime& time, nx::Random& random) -> std::optional<Shot> {
    const WeaponFiring& firing = *firingOf(weapon, owned);
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
        makeSound(firing.emptySound);
        return std::nullopt;
    }
    --owned.clip;
    makeSound(firing.sound);
    owned.nextShot = firing.cycle.count();
    owned.nextSecondary = firing.cycle.count();
    m_nextIdle = firing.idleAfterShot.count();
    owned.followUpsLeft = firing.followUps ? firing.followUps->rounds : 0;
    const std::uint32_t sequence = owned.clip == 0 && firing.lastShotSequence
                                       ? *firing.lastShotSequence
                                       : anyOf(firing.shootSequences, random);
    m_animation = ViewAnimation{.weapon = owned.weapon, .sequence = sequence, .start = time.now};

    // The shot goes along the view the kick turned so far; then the kick.
    const Shot shot = shotFrom(shooter, spread, seed);
    if (firing.recoil.kicks) {
        kickBack(kickOf(*firing.recoil.kicks, shooter), owned.shotsFired, owned.kicksLeft, shooter.punch,
                 random);
    } else if (firing.recoil.punch) {
        shooter.punch.pitch = shooter.punch.pitch - *firing.recoil.punch;
    }
    return shot;
}

auto Inventory::fireFollowUp(OwnedWeapon& owned, const Weapon& weapon, const Shooter& shooter,
                             std::uint32_t seed) -> std::optional<Shot> {
    const std::optional<WeaponFiring>& firing = firingOf(weapon, owned);
    if (owned.clip == 0 || !firing || !firing->followUps) {
        owned.followUpsLeft = 0;
        return std::nullopt;
    }
    // The burst's sequence plays on: the view model's own events show each round, which sounds as a
    // shot does.
    --owned.clip;
    --owned.followUpsLeft;
    makeSound(firing->sound);
    return shotFrom(shooter, firing->followUps->spread, seed);
}

void Inventory::switchAlternate(OwnedWeapon& owned, const Weapon& weapon, const nx::game::TickTime& time) {
    const WeaponAlternate& alternate = *weapon.alternate;
    owned.isAlternate = !owned.isAlternate;
    owned.nextSecondary = alternate.switchTime.count();
    if (alternate.onSequence && alternate.offSequence) {
        // A switch the view model plays holds the weapon's fire and its idle as long.
        m_animation =
            ViewAnimation{.weapon = owned.weapon,
                          .sequence = owned.isAlternate ? *alternate.onSequence : *alternate.offSequence,
                          .start = time.now};
        owned.nextShot = alternate.switchTime.count();
        m_nextIdle = alternate.switchTime.count();
    }
}

void Inventory::strike(OwnedWeapon& owned, const MeleeAttack& attack, std::uint32_t& count,
                       const Shooter& shooter, const WeaponTracer& tracer, const nx::game::TickTime& time) {
    // A line straight ahead along the view, without its kick, then a ducked player's hull.
    const nx::Vec3 end = shooter.eye + (nx::angleVectors(shooter.view).forward * attack.reach.value());
    nx::collision::Trace met = tracer(nx::collision::Hull::Point, shooter.eye, end);
    if (met.fraction >= 1.0F) {
        met = tracer(nx::collision::Hull::Ducked, shooter.eye, end);
    }
    const bool hits = met.fraction < 1.0F;
    makeSound(hits ? attack.hitSound : attack.missSound);
    const std::vector<std::uint32_t>& sequences = hits ? attack.hitSequences : attack.missSequences;
    m_animation = ViewAnimation{
        .weapon = owned.weapon, .sequence = sequences[count++ % sequences.size()], .start = time.now};
    const MeleeTiming& timing = hits ? attack.hit : attack.miss;
    owned.nextShot = timing.primary.count();
    owned.nextSecondary = timing.secondary.count();
    if (attack.idleAfter) {
        m_nextIdle = attack.idleAfter->count();
    }
}

void Inventory::reload(OwnedWeapon& owned, const Weapon& weapon, const nx::game::TickTime& time,
                       nx::Random& random) {
    const std::uint32_t carried = reserve(weapon.magazine->ammo);
    if (carried == 0 || owned.clip >= weapon.magazine->clipSize) {
        return;
    }
    const WeaponReload& reloading = *reloadOf(weapon, owned);
    m_nextAttack = reloading.time.count();
    m_nextIdle = reloading.time.count() + kIdleAfterReload;
    m_isReloading = true;
    m_animation = ViewAnimation{
        .weapon = owned.weapon, .sequence = anyOf(reloading.sequences, random), .start = time.now};
    if (const std::optional<WeaponFiring>& firing = firingOf(weapon, owned)) {
        owned.accuracy = firing->accuracy.initial;
        if (firing->accuracy.burst) {
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
    const WeaponView& view = viewOf(weapon, owned);
    if (weapon.magazine && owned.clip == 0 && !view.idlesWhenEmpty) {
        return; // it idles once its clip has rounds again
    }
    const double drawn = random.nextUnit();
    double shares = 0.0;
    const WeaponIdle* chosen = &view.idles.back();
    for (const WeaponIdle& candidate : view.idles) {
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
        owned.nextSecondary = std::max(owned.nextSecondary - frameTime, kLowestTimer);
    }
    m_nextIdle = std::max(m_nextIdle - frameTime, kLowestTimer);
    m_nextAttack = std::max(m_nextAttack - frameTime, kLowestTimer);
}

} // namespace cs
