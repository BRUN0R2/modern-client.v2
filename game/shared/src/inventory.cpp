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
/// The stream a shotgun's pellets stray by, of the command's number.
constexpr std::string_view kPelletStream = "pellets";

/// A gun clicks on an empty clip, and fires no sooner than this after (PlayEmptySound).
constexpr nx::Seconds kEmptyClickDelay{0.2F};
/// A reloaded weapon idles this long after its reload is over (DefaultReload).
constexpr nx::Seconds kIdleAfterReload{0.5F};
/// The lowest the timers count down to (PostThink).
constexpr nx::Seconds kLowestNextShot{-1.0F};
constexpr nx::Seconds kLowestTimer{-0.001F};
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
[[nodiscard]] auto shotFrom(const Shooter& shooter, float spread, nx::HammerUnits reach,
                            const ShotRound& round, std::uint32_t seed, const ViewAnimation& animation)
    -> Shot {
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
                .spread = spread,
                .reach = reach,
                .round = round,
                .animation = animation};
}

/// The round a shot of `firing`, of a weapon whose ammo is `ammo`, fires, at `rangeModifier`.
[[nodiscard]] auto roundOf(const WeaponFiring& firing, const AmmoType* ammo, nx::Scale rangeModifier) noexcept
    -> ShotRound {
    return ShotRound{.damage = firing.damage,
                     .rangeModifier = rangeModifier,
                     .penetration = firing.penetration,
                     .penetrationPower = ammo != nullptr ? ammo->penetrationPower : 0,
                     .penetrationDistance = ammo != nullptr ? ammo->penetrationDistance : nx::HammerUnits{}};
}

/// The delay of `followUps` before the round after its `fired` rounds (0 the shot's own): its delay of
/// that place, the last for those past; none for one a weapon frame.
[[nodiscard]] auto delayOf(const FollowUps& followUps, std::uint32_t fired) -> nx::Seconds {
    if (!followUps.delays || followUps.delays->empty()) {
        return nx::Seconds{};
    }
    return (*followUps.delays)[std::min<std::size_t>(fired, followUps.delays->size() - 1)];
}

/// One of `sequences`, at random.
[[nodiscard]] auto anyOf(std::span<const std::uint32_t> sequences, nx::Random& random) -> std::uint32_t {
    return sequences[nx::narrow<std::size_t>(random.below(sequences.size()))];
}

/// How far a shot of `firing` strays, its weapon's accuracy `accuracy`, as `shooter` moves: in the
/// air, else moving faster than the spread's limit, else ducked, else standing; its inaccuracy the
/// accuracy for a burst's, 1 less it for a paced one's.
[[nodiscard]] auto spreadOf(const WeaponFiring& firing, float accuracy, const Shooter& shooter,
                            const std::optional<SpreadTerm>& more = std::nullopt) -> float {
    const WeaponSpread& spread = firing.spread;
    const bool isWalking = spread.walking && shooter.horizontalSpeed > spread.walking->above;
    const SpreadTerm& term = !shooter.isOnGround                             ? spread.air
                             : shooter.horizontalSpeed > spread.movingAbove  ? spread.moving
                             : isWalking                                     ? spread.walking->term
                             : shooter.isDucked && spread.ducked.has_value() ? *spread.ducked
                                                                             : spread.standing;
    const double inaccuracy = firing.accuracy.burst ? double{accuracy} : 1.0 - double{accuracy};
    double strays = double{term.base} + (double{term.scale} * inaccuracy);
    if (more) {
        strays += double{more->base} + (double{more->scale} * inaccuracy); // out of its sights
    }
    return static_cast<float>(strays);
}

/// What a shot strays by more for being out of `weapon`'s sights, when it has some and `owned` is out.
[[nodiscard]] auto unzoomedSpreadOf(const Weapon& weapon, const OwnedWeapon& owned) -> std::optional<SpreadTerm> {
    return weapon.zoom && owned.zoomLevel == 0 ? weapon.zoom->unzoomedSpread : std::nullopt;
}

/// The angle a random kick of `kick` turns the view up by, `shooter` on the ground or not.
[[nodiscard]] auto randomUpOf(const RandomKick& kick, const Shooter& shooter, nx::Random& random) -> float {
    const bool inAir = !shooter.isOnGround && kick.airUpMinimum && kick.airUpMaximum;
    const float low = (inAir ? *kick.airUpMinimum : kick.upMinimum).value();
    const float high = (inAir ? *kick.airUpMaximum : kick.upMaximum).value();
    if (kick.isWhole.value_or(false)) {
        const auto lowest = static_cast<std::int64_t>(low);
        const auto span = static_cast<std::uint64_t>(static_cast<std::int64_t>(high) - lowest + 1);
        return static_cast<float>(lowest + static_cast<std::int64_t>(random.below(span)));
    }
    return low + (static_cast<float>(random.nextUnit()) * (high - low));
}

/// The accuracy of `burst` at its `shotsFired`th shot, before its maximum: the shots to its power over
/// its divisor, in whole numbers for a whole divisor as the original's int divides them, plus its offset.
[[nodiscard]] auto burstAccuracyOf(const BurstAccuracy& burst, std::uint32_t shotsFired) -> float {
    constexpr std::uint32_t kCubed = 3;
    std::uint64_t raised = 1;
    for (std::uint32_t factor = 0; factor < burst.power.value_or(kCubed); ++factor) {
        raised *= shotsFired;
    }
    const double whole = std::floor(burst.divisor);
    if (whole == burst.divisor) {
        return static_cast<float>(raised / static_cast<std::uint64_t>(whole)) + burst.offset;
    }
    return static_cast<float>((static_cast<double>(raised) / burst.divisor) + double{burst.offset});
}

/// The kick of `kicks` as `shooter` moves: moving at all, else in the air, else ducked, else standing;
/// in the air first for kicks that weigh it first.
[[nodiscard]] auto kickOf(const KickByMovement& kicks, const Shooter& shooter) -> const Kick& {
    if (kicks.airFirst.value_or(false) && !shooter.isOnGround) {
        return kicks.air;
    }
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
    // Up is taken off the punch in double precision, as the original subtracts it.
    const double up = double{(shots == 1 ? kick.upBase : (fired * kick.upModifier) + kick.upBase).value()};
    const nx::Degrees lateral =
        shots == 1 ? kick.lateralBase : (fired * kick.lateralModifier) + kick.lateralBase;
    punch.pitch = std::max(nx::Degrees{static_cast<float>(double{punch.pitch.value()} - up)}, -kick.upMax);
    punch.yaw = kicksLeft ? std::min(punch.yaw + lateral, kick.lateralMax)
                          : std::max(punch.yaw - lateral, -kick.lateralMax);
    if (random.below(std::uint64_t{kick.directionChange} + 1) == 0) {
        kicksLeft = !kicksLeft;
    }
}

} // namespace

auto Inventory::give(WeaponIndex asked, AutoSwitch autoSwitch, const nx::game::TickTime& time,
                     nx::Random& random) -> bool {
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
    if (!m_held || (autoSwitch == AutoSwitch::ToHeavier &&
                    given.weight > m_catalog->weapon(m_weapons[*m_held].weapon).weight)) {
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
                      const nx::game::TickTime& time) -> WeaponAttacks {
    WeaponAttacks attacks;
    std::vector<Shot>& shots = attacks.shots;
    // As the original's PostThink, which counts the timers down once the frame is over.
    const nx::Seconds frameTime = frameTimeOf(command);
    const auto over = [&] {
        countDown(frameTime);
        return attacks;
    };
    if (!m_held || m_nextAttack > nx::Seconds{}) {
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
    if (owned.followUpsLeft > 0 && owned.nextFollowUp < nx::Seconds{}) {
        // The rounds a shot fires after it, once their delay is past, whatever the buttons (FireRemaining).
        if (const std::optional<Shot> shot =
                fireFollowUp(owned, weapon, shooter, command.number.value, time)) {
            shots.push_back(*shot);
            attacks.motions.set(HolderMotion::Attacked);
        }
    }
    const bool isAttacking = command.buttons.test(Button::Attack);
    // `Attack2` counts for a weapon with an alternate or melee attacks alone (HasSecondaryAttack).
    const bool switches = (weapon.alternate.has_value() || weapon.melee.has_value() || weapon.zoom.has_value()) &&
                          command.buttons.test(Button::Attack2);
    if (owned.resumeZoom > 0 && owned.nextShot <= nx::Seconds{} && owned.clip > 0) {
        owned.zoomLevel = std::exchange(owned.resumeZoom, 0); // through its sights again
    }
    if (switches && owned.nextSecondary <= nx::Seconds{}) {
        if (weapon.zoom && !weapon.alternate && !weapon.melee) {
            switchZoom(owned, weapon, time);
        } else if (weapon.melee) {
            strike(owned, weapon.melee->stab, owned.stabs, shooter, tracer, time, attacks.blows);
            attacks.motions.set(HolderMotion::Attacked);
        } else {
            switchAlternate(owned, weapon, time);
        }
        return over();
    }
    if (isAttacking && owned.nextShot <= nx::Seconds{}) {
        if (!shooter.canAttack) {
            return over(); // the frame is spent on the press all the same
        }
        if (weapon.melee) {
            strike(owned, weapon.melee->slash, owned.slashes, shooter, tracer, time, attacks.blows);
            attacks.motions.set(HolderMotion::Attacked);
        } else if (firingOf(weapon, owned)) {
            if (owned.clip == 0 && isShellReloaded(owned, weapon) && reserve(weapon.magazine->ammo) > 0) {
                reloadShells(owned, weapon, time, random); // an empty shotgun reloads instead
                return over();
            }
            std::vector<Shot> fired = fire(owned, weapon, shooter, command.number.value, time, random);
            if (!fired.empty()) {
                std::ranges::move(fired, std::back_inserter(shots));
                attacks.motions.set(HolderMotion::Attacked);
            }
        }
        return over();
    }
    const bool canReload = reloadOf(weapon, owned).has_value() && owned.followUpsLeft == 0;
    if (command.buttons.test(Button::Reload) && canReload && !m_isReloading &&
        owned.nextShot < nx::Seconds{}) {
        if (isShellReloaded(owned, weapon)) {
            reloadShells(owned, weapon, time, random);
        } else {
            reload(owned, weapon, time, random);
        }
        attacks.motions.set(HolderMotion::Reloaded);
        return over();
    }
    if (isAttacking || switches) {
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
    if (isShellReloaded(owned, weapon) && m_nextIdle <= nx::Seconds{}) {
        // A shotgun's WeaponIdle: on with its reload while it may load more, else its pump; empty, it starts one.
        const std::uint32_t carried = reserve(weapon.magazine->ammo);
        if (m_shellReload == 0 && owned.clip == 0 && carried > 0) {
            reloadShells(owned, weapon, time, random);
            attacks.motions.set(HolderMotion::Reloaded);
            return over();
        }
        if (m_shellReload != 0) {
            if (owned.clip < weapon.magazine->clipSize && carried > 0) {
                reloadShells(owned, weapon, time, random);
            } else {
                const ShellReload& shells = *reloadOf(weapon, owned)->shells;
                m_animation =
                    ViewAnimation{.weapon = owned.weapon, .sequence = shells.pumpSequence, .start = time.now};
                m_shellReload = 0;
                m_nextIdle = shells.pumpTime;
            }
            return over();
        }
    }
    // An empty clip is reloaded once the weapon may fire again; else the weapon idles.
    const bool isUsable = !weapon.magazine || owned.clip > 0 || reserve(weapon.magazine->ammo) > 0;
    if (isUsable && canReload && owned.clip == 0 && owned.nextShot < nx::Seconds{}) {
        reload(owned, weapon, time, random);
        attacks.motions.set(HolderMotion::Reloaded);
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
    if (!m_held) {
        return kUnarmedSpeed;
    }
    const OwnedWeapon& owned = m_weapons[*m_held];
    const Weapon& weapon = m_catalog->weapon(owned.weapon);
    if (weapon.zoom && weapon.zoom->maxSpeed && owned.zoomLevel > 0) {
        return *weapon.zoom->maxSpeed;
    }
    return weapon.maxSpeed;
}

auto Inventory::fov() const noexcept -> std::optional<std::uint32_t> {
    if (!m_held) {
        return std::nullopt;
    }
    const OwnedWeapon& owned = m_weapons[*m_held];
    const Weapon& weapon = m_catalog->weapon(owned.weapon);
    if (!weapon.zoom || owned.zoomLevel == 0 || owned.zoomLevel > weapon.zoom->fovs.size()) {
        return std::nullopt;
    }
    return weapon.zoom->fovs[owned.zoomLevel - 1];
}

auto Inventory::isScoped() const noexcept -> bool {
    return fov().has_value() && m_catalog->weapon(m_weapons[*m_held].weapon).zoom->hasScope.value_or(false);
}

auto Inventory::takeSounds() -> std::vector<SoundMade> {
    return std::exchange(m_sounds, {});
}

void Inventory::removeAll() {
    std::vector<SoundMade> sounds = std::move(m_sounds);
    *this = Inventory{*m_catalog};
    m_sounds = std::move(sounds);
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

void Inventory::makeSound(const std::optional<GameSound>& sound, const nx::game::TickTime& time) {
    if (sound) {
        m_sounds.push_back(SoundMade{.sound = &*sound, .at = time.now, .volume = sound->volume});
    }
}

void Inventory::drop(WeaponIndex weapon, const nx::game::TickTime& time, nx::Random& random) {
    const std::optional<std::size_t> dropped = find(weapon);
    nx::expects(dropped.has_value(), "Inventory::drop: a weapon the player has");
    const bool wasHeld = m_held == dropped;
    m_weapons.erase(m_weapons.begin() + nx::narrow<std::ptrdiff_t>(*dropped));
    for (std::optional<std::size_t>* const kept : {&m_held, &m_before}) {
        if (*kept == dropped) {
            kept->reset();
        } else if (*kept > dropped) {
            --**kept;
        }
    }
    if (!wasHeld) {
        return;
    }
    m_isReloading = false;
    m_animation.reset();
    // GetNextBestWeapon: among the slots in their order, each's newest first, the first of the most weight.
    std::optional<WeaponIndex> heaviest;
    for (const WeaponSlot slot : {WeaponSlot::Primary, WeaponSlot::Secondary, WeaponSlot::Knife,
                                  WeaponSlot::Grenade, WeaponSlot::Bomb}) {
        for (const WeaponIndex candidate : weaponsIn(slot)) {
            if (!heaviest || m_catalog->weapon(candidate).weight > m_catalog->weapon(*heaviest).weight) {
                heaviest = candidate;
            }
        }
    }
    if (heaviest) {
        draw(*find(*heaviest), time, random);
    }
}

auto Inventory::weaponsIn(WeaponSlot slot) const -> std::vector<WeaponIndex> {
    std::vector<WeaponIndex> weapons;
    for (const OwnedWeapon& owned : m_weapons | std::views::reverse) {
        if (m_catalog->weapon(owned.weapon).slot == slot) {
            weapons.push_back(owned.weapon);
        }
    }
    return weapons;
}

void Inventory::spawned() noexcept {
    m_nextAttack = nx::Seconds{};
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
    owned.firesLeft = owned.clip % 2 == 0; // ELITE's Deploy: an even clip fires the left first
    for (OwnedWeapon& carried : m_weapons) {
        carried.zoomLevel = 0; // out of every sights (DefaultDeploy's m_iFOV)
        carried.resumeZoom = 0;
    }
    m_shellReload = 0;
    owned.slashes = 0;       // a knife drawn slashes from its first sequence again (CKnife::Deploy)
    const WeaponView& view = viewOf(weapon, owned);
    m_animation = ViewAnimation{
        .weapon = owned.weapon, .sequence = anyOf(view.drawSequences, random), .start = time.now};
    makeSound(view.drawSound, time);
    m_nextIdle = kIdleAfterDraw;
    m_nextAttack = kAttackAfterDraw;
}

auto Inventory::fire(OwnedWeapon& owned, const Weapon& weapon, Shooter& shooter, std::uint32_t seed,
                     const nx::game::TickTime& time, nx::Random& random) -> std::vector<Shot> {
    const WeaponFiring& firing = *firingOf(weapon, owned);
    m_shellReload = 0; // firing stops a reload a shell at a time (PrimaryAttack's m_fInSpecialReload)
    // The spread is of the accuracy before this shot changes it, but of a timed accuracy of this shot's.
    float spread = spreadOf(firing, owned.accuracy, shooter, unzoomedSpreadOf(weapon, owned));
    if (firing.isAutomatic) {
        owned.isBursting = true;
        ++owned.shotsFired;
    } else if (++owned.shotsFired > 1) {
        return {}; // once a press
    }
    if (const std::optional<BurstAccuracy>& burst = firing.accuracy.burst) {
        owned.accuracy = std::min(burstAccuracyOf(*burst, owned.shotsFired), burst->maximum);
    } else if (const std::optional<PacedAccuracy>& paced = firing.accuracy.paced) {
        if (owned.lastShot) {
            const nx::Seconds since = time.now.since(*owned.lastShot, time.rate);
            owned.accuracy -= (paced->threshold - since).count() * paced->rate;
            owned.accuracy = std::clamp(owned.accuracy, paced->minimum, paced->maximum);
        }
        owned.lastShot = time.now;
    } else if (const std::optional<TimedAccuracy>& timed = firing.accuracy.timed) {
        if (owned.lastShot) {
            const nx::Seconds since = time.now.since(*owned.lastShot, time.rate);
            owned.accuracy = std::min((since.count() * timed->rate) + timed->offset, timed->maximum);
        } else if (timed->firstShot) {
            owned.accuracy = *timed->firstShot;
        }
        owned.lastShot = time.now;
        if (timed->isOfThisShot.value_or(false)) {
            spread = spreadOf(firing, owned.accuracy, shooter, unzoomedSpreadOf(weapon, owned));
        }
    }
    if (owned.clip == 0) {
        owned.nextShot = kEmptyClickDelay;
        makeSound(firing.emptySound, time);
        return {};
    }
    --owned.clip;
    makeSound(firing.sound, time);
    const nx::Seconds cycle =
        weapon.zoom && owned.zoomLevel > 0 && weapon.zoom->cycle ? *weapon.zoom->cycle : firing.cycle;
    owned.nextShot = cycle;
    owned.nextSecondary = cycle;
    if (weapon.zoom && weapon.zoom->unzoomsOnShot.value_or(false) && owned.zoomLevel > 0) {
        // Out of its sights until it may fire again (m_bResumeZoom, m_iLastZoom).
        owned.resumeZoom = owned.zoomLevel;
        owned.zoomLevel = 0;
    }
    m_nextIdle = firing.idleAfterShot;
    owned.followUpsLeft = firing.followUps ? firing.followUps->rounds : 0;
    owned.followUpSpread = spread;
    owned.nextFollowUp = firing.followUps ? delayOf(*firing.followUps, 0) : nx::Seconds{};
    std::uint32_t sequence = owned.clip == 0 && firing.lastShotSequence ? *firing.lastShotSequence
                                                                        : anyOf(firing.shootSequences, random);
    if (const std::optional<DualHands>& hands = firing.hands) {
        const bool isLast = owned.clip == 0;
        sequence = owned.firesLeft ? (isLast ? hands->leftLastSequence : anyOf(hands->leftSequences, random))
                                   : (isLast ? hands->rightLastSequence : anyOf(hands->rightSequences, random));
    }
    m_animation = ViewAnimation{.weapon = owned.weapon, .sequence = sequence, .start = time.now};

    // The shot goes along the view the kick turned so far; then the kick.
    std::vector<Shot> shots;
    if (const std::optional<Pellets>& pellets = firing.pellets) {
        // FireBullets: each pellet its own way within the cone, drawn again while out of its circle.
        const nx::AngleVectors axes =
            nx::angleVectors(nx::EulerAngles{.pitch = shooter.view.pitch + shooter.punch.pitch,
                                             .yaw = shooter.view.yaw + shooter.punch.yaw,
                                             .roll = shooter.view.roll + shooter.punch.roll});
        nx::Random stray{seed, kPelletStream};
        const auto half = [&stray] { return stray.nextUnit() - 0.5; };
        ShotRound round = roundOf(firing, ammoOf(weapon), firing.rangeModifier);
        round.isPellet = true;
        for (std::uint32_t pellet = 0; pellet < pellets->count; ++pellet) {
            double across = 0.0;
            double up = 0.0;
            do {
                across = half() + half();
                up = half() + half();
            } while ((across * across) + (up * up) > 1.0);
            const auto right = static_cast<float>(across) * pellets->cone;
            const auto upward = static_cast<float>(up) * pellets->cone;
            shots.push_back(Shot{.origin = shooter.eye,
                                 .direction = axes.forward + (axes.right * right) + (axes.up * upward),
                                 .spread = pellets->cone,
                                 .reach = firing.reach,
                                 .round = round,
                                 .animation = *m_animation});
        }
    } else {
        shots.push_back(shotFrom(shooter, spread, firing.reach,
                                 roundOf(firing, ammoOf(weapon), firing.rangeModifier), seed, *m_animation));
    }
    Shot& shot = shots.front();
    if (const std::optional<DualHands>& hands = firing.hands) {
        // From the hand's side of the eye, along the view turned by the kick (ELITEFire's v_right * 5).
        const nx::AngleVectors axes =
            nx::angleVectors(nx::EulerAngles{.pitch = shooter.view.pitch + shooter.punch.pitch,
                                             .yaw = shooter.view.yaw + shooter.punch.yaw,
                                             .roll = shooter.view.roll + shooter.punch.roll});
        const float side = owned.firesLeft ? -hands->offset.value() : hands->offset.value();
        shot.origin = shot.origin + (axes.right * side);
        owned.firesLeft = !owned.firesLeft;
    }
    if (const std::optional<RandomKick>& kick = firing.recoil.random) {
        // punchangle.x -= up + punchangle.x * share; punchangle.y += a side at random.
        const float up = randomUpOf(*kick, shooter, random);
        const float share = kick->punchShare.value_or(0.0F) * shooter.punch.pitch.value();
        shooter.punch.pitch = shooter.punch.pitch - nx::Degrees{up + share};
        if (kick->lateral) {
            const float side = kick->lateral->value();
            shooter.punch.yaw =
                shooter.punch.yaw + nx::Degrees{(static_cast<float>(random.nextUnit()) * 2.0F * side) - side};
        }
    }
    if (firing.recoil.kicks) {
        kickBack(kickOf(*firing.recoil.kicks, shooter), owned.shotsFired, owned.kicksLeft, shooter.punch,
                 random);
    } else if (firing.recoil.punch) {
        shooter.punch.pitch = shooter.punch.pitch - *firing.recoil.punch;
    }
    return shots;
}

void Inventory::switchZoom(OwnedWeapon& owned, const Weapon& weapon, const nx::game::TickTime& time) {
    const WeaponZoom& zoom = *weapon.zoom;
    owned.zoomLevel = owned.zoomLevel >= zoom.fovs.size() ? 0 : owned.zoomLevel + 1;
    owned.resumeZoom = 0;
    owned.nextSecondary = zoom.switchTime;
    makeSound(zoom.sound, time);
}

auto Inventory::isShellReloaded(const OwnedWeapon& owned, const Weapon& weapon) const -> bool {
    const std::optional<WeaponReload>& reloading = reloadOf(weapon, owned);
    return reloading && reloading->shells;
}

void Inventory::reloadShells(OwnedWeapon& owned, const Weapon& weapon, const nx::game::TickTime& time,
                             nx::Random& random) {
    const WeaponReload& reloading = *reloadOf(weapon, owned);
    const ShellReload& shells = *reloading.shells;
    std::uint32_t& carried = m_reserves.try_emplace(weapon.magazine->ammo, 0).first->second;
    switch (m_shellReload) {
    case 0:
        if (carried == 0 || owned.clip >= weapon.magazine->clipSize || owned.nextShot > nx::Seconds{}) {
            return;
        }
        owned.zoomLevel = 0;
        m_shellReload = 1;
        m_animation = ViewAnimation{.weapon = owned.weapon, .sequence = shells.startSequence, .start = time.now};
        m_nextAttack = shells.startTime;
        m_nextIdle = shells.startTime;
        owned.nextShot = shells.startTime;
        owned.nextSecondary = shells.startTime;
        return;
    case 1:
        if (m_nextIdle > nx::Seconds{}) {
            return;
        }
        m_shellReload = 2;
        m_animation = ViewAnimation{
            .weapon = owned.weapon, .sequence = anyOf(reloading.sequences, random), .start = time.now};
        m_nextIdle = shells.shellTime;
        return;
    default:
        if (carried > 0 && owned.clip < weapon.magazine->clipSize) {
            ++owned.clip;
            --carried;
        }
        m_shellReload = 1;
        return;
    }
}

auto Inventory::fireFollowUp(OwnedWeapon& owned, const Weapon& weapon, const Shooter& shooter,
                             std::uint32_t seed, const nx::game::TickTime& time) -> std::optional<Shot> {
    const std::optional<WeaponFiring>& firing = firingOf(weapon, owned);
    if (owned.clip == 0 || !firing || !firing->followUps) {
        owned.followUpsLeft = 0;
        return std::nullopt;
    }
    // The burst's sequence plays on: the view model's own events show each round, which sounds as a
    // shot does.
    const FollowUps& followUps = *firing->followUps;
    --owned.clip;
    --owned.followUpsLeft;
    owned.nextFollowUp = delayOf(followUps, followUps.rounds - owned.followUpsLeft);
    makeSound(firing->sound, time);
    nx::invariant(m_animation.has_value(), "a follow-up shot has its burst's view animation");
    ShotRound round = roundOf(*firing, ammoOf(weapon), followUps.rangeModifier);
    if (followUps.damage) {
        round.damage = *followUps.damage;
    }
    return shotFrom(shooter, followUps.spread.value_or(owned.followUpSpread), firing->reach, round, seed,
                    *m_animation);
}

auto Inventory::ammoOf(const Weapon& weapon) const noexcept -> const AmmoType* {
    return weapon.magazine ? m_catalog->findAmmo(weapon.magazine->ammo) : nullptr;
}

void Inventory::switchAlternate(OwnedWeapon& owned, const Weapon& weapon, const nx::game::TickTime& time) {
    const WeaponAlternate& alternate = *weapon.alternate;
    owned.isAlternate = !owned.isAlternate;
    owned.nextSecondary = alternate.switchTime;
    if (alternate.onSequence && alternate.offSequence) {
        // A switch the view model plays holds the weapon's fire and its idle as long.
        m_animation =
            ViewAnimation{.weapon = owned.weapon,
                          .sequence = owned.isAlternate ? *alternate.onSequence : *alternate.offSequence,
                          .start = time.now};
        owned.nextShot = alternate.switchTime;
        m_nextIdle = alternate.switchTime;
    }
}

namespace {

/// Nearer than this, a corner's line is the nearest (FindHullIntersection's first distance).
constexpr double kNoIntersection = 1.0e6;
/// A stab is in a player's back when the way it strikes, along the ground, is more than this along the
/// player's body's forward (Stab's flDot).
constexpr float kInTheBack = 0.8F;

/// Whether a strike along `forward` meets a player whose body is turned by `angles` in its back (Stab):
/// `forward` along the ground, made of length 1, more than kInTheBack along the body's forward along the
/// ground, as it is.
[[nodiscard]] auto isInTheBack(nx::Vec3 forward, nx::EulerAngles angles) noexcept -> bool {
    const nx::Vec3 body = nx::angleVectors(angles).forward;
    const float length = std::sqrt((forward.x * forward.x) + (forward.y * forward.y));
    if (length == 0.0F) {
        return false; // the original's Normalize of none is none
    }
    return ((forward.x / length) * body.x) + ((forward.y / length) * body.y) > kInTheBack;
}

/// Where a hull's way that met the world or a brush entity at `hull` meets what it met, as the knife's
/// FindHullIntersection approximates it: a line from `eye` to twice as far as the hull stopped, else
/// of the lines to the corners of a ducked player's box about that end the one that meets something
/// nearest (its distance in double precision, as the original's Length); else the hull's way.
[[nodiscard]] auto hullIntersection(nx::Vec3 eye, const WeaponHit& hull, const WeaponTracer& tracer)
    -> WeaponHit {
    const nx::Vec3 hullEnd = eye + ((hull.trace.end - eye) * 2.0F);
    if (WeaponHit line = tracer(nx::collision::Hull::Point, eye, hullEnd); line.trace.fraction < 1.0F) {
        return line;
    }
    const nx::Aabb ducked = nx::collision::hullBoxOf(nx::collision::Hull::Ducked);
    const std::array<nx::Vec3, 2> corners{ducked.min, ducked.max};
    WeaponHit nearest = hull;
    double distance = kNoIntersection;
    for (const nx::Vec3& x : corners) {
        for (const nx::Vec3& y : corners) {
            for (const nx::Vec3& z : corners) {
                const nx::Vec3 end{.x = hullEnd.x + x.x, .y = hullEnd.y + y.y, .z = hullEnd.z + z.z};
                WeaponHit corner = tracer(nx::collision::Hull::Point, eye, end);
                if (corner.trace.fraction >= 1.0F) {
                    continue;
                }
                const nx::Vec3 along = corner.trace.end - eye;
                const double length =
                    std::sqrt((double{along.x} * double{along.x}) + (double{along.y} * double{along.y}) +
                              (double{along.z} * double{along.z}));
                if (length < distance) {
                    nearest = std::move(corner);
                    distance = length;
                }
            }
        }
    }
    return nearest;
}

} // namespace

void Inventory::strike(OwnedWeapon& owned, const MeleeAttack& attack, std::uint32_t& count,
                       const Shooter& shooter, const WeaponTracer& tracer, const nx::game::TickTime& time,
                       std::vector<Blow>& blows) {
    // A line straight ahead along the view, without its kick, then a ducked player's hull.
    const nx::Vec3 forward = nx::angleVectors(shooter.view).forward;
    const nx::Vec3 end = shooter.eye + (forward * attack.reach.value());
    WeaponHit met = tracer(nx::collision::Hull::Point, shooter.eye, end);
    if (met.trace.fraction >= 1.0F) {
        met = tracer(nx::collision::Hull::Ducked, shooter.eye, end);
        if (met.trace.fraction < 1.0F && met.isBrushModel) {
            met = hullIntersection(shooter.eye, met, tracer);
        }
    }
    const bool hits = met.trace.fraction < 1.0F;
    makeSound(!hits ? attack.missSound : met.isBody ? attack.bodySound : attack.hitSound, time);
    const std::vector<std::uint32_t>& sequences = hits ? attack.hitSequences : attack.missSequences;
    m_animation = ViewAnimation{
        .weapon = owned.weapon, .sequence = sequences[count++ % sequences.size()], .start = time.now};
    const MeleeTiming& timing = hits ? attack.hit : attack.miss;
    owned.nextShot = timing.primary;
    owned.nextSecondary = timing.secondary;
    if (attack.idleAfter) {
        m_nextIdle = *attack.idleAfter;
    }
    // What it met takes its blow once the next attack's time is set (TraceAttack, ApplyMultiDamage), more
    // in a player's back.
    if (hits && met.entity) {
        nx::Damage damage = attack.damage;
        if (attack.backMultiplier && met.playerAngles && isInTheBack(forward, *met.playerAngles)) {
            damage = damage * *attack.backMultiplier;
        }
        blows.push_back(
            Blow{.entity = *met.entity, .trace = met.trace, .damage = damage, .direction = forward});
    }
}

void Inventory::reload(OwnedWeapon& owned, const Weapon& weapon, const nx::game::TickTime& time,
                       nx::Random& random) {
    const std::uint32_t carried = reserve(weapon.magazine->ammo);
    if (carried == 0 || owned.clip >= weapon.magazine->clipSize) {
        return;
    }
    const WeaponReload& reloading = *reloadOf(weapon, owned);
    owned.zoomLevel = 0; // out of its sights (the scoped weapons' Reload)
    owned.resumeZoom = 0;
    m_nextAttack = reloading.time;
    m_nextIdle = reloading.time + kIdleAfterReload;
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
    if (!m_held || m_nextIdle > nx::Seconds{}) {
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
    m_nextIdle = chosen->wait;
}

void Inventory::countDown(nx::Seconds frameTime) noexcept {
    for (OwnedWeapon& owned : m_weapons) {
        owned.nextShot = std::max(owned.nextShot - frameTime, kLowestNextShot);
        owned.nextSecondary = std::max(owned.nextSecondary - frameTime, kLowestTimer);
        owned.nextFollowUp = std::max(owned.nextFollowUp - frameTime, kLowestTimer);
    }
    m_nextIdle = std::max(m_nextIdle - frameTime, kLowestTimer);
    m_nextAttack = std::max(m_nextAttack - frameTime, kLowestTimer);
}

} // namespace cs
