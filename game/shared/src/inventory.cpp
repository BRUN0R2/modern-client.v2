// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.game;

namespace cs {

namespace {

/// The stream of random numbers the weapons draw from.
constexpr std::string_view kRandomStream = "weapons";

} // namespace

auto Inventory::give(WeaponIndex weapon, bool autoSwitch, const nx::game::TickTime& time, nx::Random& random)
    -> bool {
    const Weapon& given = m_catalog->weapon(weapon);
    const std::uint32_t rounds = given.magazine ? given.magazine->clipSize : 0;
    if (const std::optional<std::size_t> owned = find(weapon)) {
        takeRounds(*owned, rounds);
        return false;
    }
    m_weapons.push_back(OwnedWeapon{.weapon = weapon});
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

void Inventory::play(const UserCommand& command, const nx::game::TickTime& time) {
    nx::Random random{command.number.value, kRandomStream};
    if (command.weaponSelect) {
        if (const std::optional<std::size_t> selected = find(*command.weaponSelect);
            selected && selected != m_held) {
            draw(*selected, time, random);
        }
    }
    idle(time, random);
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
    const WeaponIndex weapon = m_weapons[index].weapon;
    const std::vector<std::uint32_t>& draws = m_catalog->weapon(weapon).view.drawSequences;
    m_animation = ViewAnimation{.weapon = weapon,
                                .sequence = draws[nx::narrow<std::size_t>(random.below(draws.size()))],
                                .start = time.now};
    m_nextIdle = time.now.after(kIdleAfterDraw, time.rate);
}

void Inventory::idle(const nx::game::TickTime& time, nx::Random& random) {
    if (!m_held || time.now < m_nextIdle) {
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
    m_nextIdle = time.now.after(chosen->wait, time.rate);
}

} // namespace cs
