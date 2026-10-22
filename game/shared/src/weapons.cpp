// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.data;
import nx.assets;

namespace cs {

namespace {

/// The shares of a weapon's idles add up to 1 within this much.
constexpr double kShareTolerance = 1e-4;

/// DataInvalid when `path`, the `role` model of the weapon `id` of `sourceName`, is not a path of
/// the game's files.
[[nodiscard]] auto checkModelPath(std::string_view path, std::string_view role, std::string_view id,
                                  std::string_view sourceName) -> nx::Result<void, WeaponError> {
    if (auto parsed = nx::assets::VfsPath::parse(path); !parsed) {
        return nx::fail(WeaponError::DataInvalid,
                        std::format("{}: the {} model of {:?}, {:?}", sourceName, role, id, path),
                        std::move(parsed.error()));
    }
    return {};
}

/// DataInvalid when `view`, of the weapon `id` of `where` (a file, or the catalog), lacks a draw
/// sequence or an idle, waits no time to idle again, has idles whose shares do not add up to 1, or
/// a draw sound soundProblem refuses.
[[nodiscard]] auto checkView(const WeaponView& view, std::string_view id, std::string_view where)
    -> nx::Result<void, WeaponError> {
    if (view.drawSequences.empty() || view.idles.empty()) {
        return nx::fail(WeaponError::DataInvalid,
                        std::format("{}: {:?} without a draw sequence or an idle", where, id));
    }
    double shares = 0.0;
    for (const WeaponIdle& idle : view.idles) {
        if (idle.wait <= nx::Seconds{}) {
            return nx::fail(WeaponError::DataInvalid,
                            std::format("{}: an idle of {:?} that waits no time to idle again", where, id));
        }
        shares += double{idle.share};
    }
    if (std::abs(shares - 1.0) > kShareTolerance) {
        return nx::fail(
            WeaponError::DataInvalid,
            std::format("{}: the idles of {:?} have shares adding up to {}, not 1", where, id, shares));
    }
    if (view.drawSound) {
        if (const std::optional<std::string_view> problem = soundProblem(*view.drawSound)) {
            return nx::fail(WeaponError::DataInvalid, std::format("{}: {:?} {}", where, id, *problem));
        }
    }
    return {};
}

/// What is wrong with `firing`, when anything is: no shoot sequence or cycle, not one accuracy, a
/// paced accuracy whose minimum is above its maximum, or a sound soundProblem refuses.
[[nodiscard]] auto firingProblem(const WeaponFiring& firing) -> std::optional<std::string_view> {
    if (firing.shootSequences.empty() || firing.cycle <= nx::Seconds{}) {
        return "fires without a shoot sequence or a cycle";
    }
    if (firing.accuracy.burst.has_value() == firing.accuracy.paced.has_value()) {
        return "fires without one accuracy, by bursts or by pace";
    }
    if (firing.accuracy.paced && firing.accuracy.paced->minimum > firing.accuracy.paced->maximum) {
        return "fires with a paced accuracy whose minimum is above its maximum";
    }
    for (const std::optional<GameSound>* const sound : {&firing.sound, &firing.emptySound}) {
        if (*sound) {
            if (const std::optional<std::string_view> problem = soundProblem(**sound)) {
                return problem;
            }
        }
    }
    return std::nullopt;
}

/// DataInvalid when `weapon`, of `where` (a file, or the catalog), fires or is reloaded without a
/// magazine, fires with a problem (firingProblem), or is reloaded without a sequence; the same of its
/// alternate, which also may not play one of its switch's sequences alone, nor have a view checkView
/// refuses.
[[nodiscard]] auto checkFiring(const Weapon& weapon, std::string_view where)
    -> nx::Result<void, WeaponError> {
    const auto invalid = [&](std::string_view problem) {
        return nx::fail(WeaponError::DataInvalid, std::format("{}: {:?} {}", where, weapon.id, problem));
    };
    std::vector<const WeaponFiring*> firings;
    std::vector<const WeaponReload*> reloads;
    if (weapon.firing) {
        firings.push_back(&*weapon.firing);
    }
    if (weapon.reload) {
        reloads.push_back(&*weapon.reload);
    }
    if (const std::optional<WeaponAlternate>& alternate = weapon.alternate) {
        if (alternate->onSequence.has_value() != alternate->offSequence.has_value()) {
            return invalid("switches with one of its sequences alone");
        }
        if (alternate->view) {
            if (auto checked = checkView(*alternate->view, weapon.id, where); !checked) {
                return checked;
            }
        }
        if (alternate->firing) {
            firings.push_back(&*alternate->firing);
        }
        if (alternate->reload) {
            reloads.push_back(&*alternate->reload);
        }
    }
    if ((!firings.empty() || !reloads.empty()) && !weapon.magazine) {
        return invalid("fires or is reloaded without a magazine");
    }
    for (const WeaponFiring* const firing : firings) {
        if (const std::optional<std::string_view> problem = firingProblem(*firing)) {
            return invalid(*problem);
        }
    }
    if (std::ranges::any_of(reloads, [](const WeaponReload* reload) { return reload->sequences.empty(); })) {
        return invalid("is reloaded without a sequence");
    }
    if (const std::optional<WeaponMelee>& melee = weapon.melee) {
        if (weapon.alternate) {
            return invalid("has melee attacks and an alternate, both of +attack2");
        }
        for (const MeleeAttack* const attack : {&melee->slash, &melee->stab}) {
            if (attack->missSequences.empty() || attack->hitSequences.empty()) {
                return invalid("attacks without a miss or a hit sequence");
            }
            for (const std::optional<GameSound>* const sound : {&attack->missSound, &attack->hitSound}) {
                if (*sound) {
                    if (const std::optional<std::string_view> problem = soundProblem(**sound)) {
                        return invalid(*problem);
                    }
                }
            }
        }
    }
    return {};
}

/// DataInvalid when two of `items` have the same `key`, which names what they are: `what`.
template <class Item>
[[nodiscard]] auto checkUnique(std::span<const Item> items, std::string Item::* key, std::string_view what)
    -> nx::Result<void, WeaponError> {
    std::set<std::string_view> seen;
    for (const Item& item : items) {
        if (!seen.insert(item.*key).second) {
            return nx::fail(WeaponError::DataInvalid, std::format("{} {:?} twice", what, item.*key));
        }
    }
    return {};
}

} // namespace

auto readAmmoTypes(std::string_view text, std::string_view sourceName) -> nx::Result<AmmoTypes, WeaponError> {
    auto types = nx::data::parseToml<AmmoTypes>(text, sourceName);
    if (!types) {
        return nx::fail(WeaponError::DataInvalid, std::string{sourceName}, std::move(types.error()));
    }
    if (std::ranges::contains(types->ammo, std::string_view{}, &AmmoType::id)) {
        return nx::fail(WeaponError::DataInvalid,
                        std::format("{}: a kind of ammo without an id", sourceName));
    }
    return std::move(*types);
}

auto readWeapon(std::string_view text, std::string_view sourceName) -> nx::Result<Weapon, WeaponError> {
    auto weapon = nx::data::parseToml<Weapon>(text, sourceName);
    if (!weapon) {
        return nx::fail(WeaponError::DataInvalid, std::string{sourceName}, std::move(weapon.error()));
    }
    if (weapon->id.empty() || weapon->classname.empty()) {
        return nx::fail(WeaponError::DataInvalid,
                        std::format("{}: a weapon without an id or a classname", sourceName));
    }
    if (weapon->magazine && weapon->magazine->ammo.empty()) {
        return nx::fail(WeaponError::DataInvalid,
                        std::format("{}: {:?} fires no kind of ammo", sourceName, weapon->id));
    }
    if (weapon->models.playerAnimation.empty()) {
        return nx::fail(WeaponError::DataInvalid,
                        std::format("{}: {:?} without its player's animation", sourceName, weapon->id));
    }
    if (auto checked = checkModelPath(weapon->models.view, "view", weapon->id, sourceName); !checked) {
        return std::unexpected{std::move(checked.error())};
    }
    if (auto checked = checkModelPath(weapon->models.player, "player", weapon->id, sourceName); !checked) {
        return std::unexpected{std::move(checked.error())};
    }
    if (auto checked = checkView(weapon->view, weapon->id, sourceName); !checked) {
        return std::unexpected{std::move(checked.error())};
    }
    if (auto checked = checkFiring(*weapon, sourceName); !checked) {
        return std::unexpected{std::move(checked.error())};
    }
    return std::move(*weapon);
}

WeaponCatalog::WeaponCatalog(std::vector<AmmoType> ammo, std::vector<Weapon> weapons) noexcept
    : m_ammo{std::move(ammo)}, m_weapons{std::move(weapons)} {}

auto WeaponCatalog::make(AmmoTypes ammo, std::vector<Weapon> weapons)
    -> nx::Result<WeaponCatalog, WeaponError> {
    if (auto checked = checkUnique<AmmoType>(ammo.ammo, &AmmoType::id, "the kind of ammo"); !checked) {
        return std::unexpected{std::move(checked.error())};
    }
    if (auto checked = checkUnique<Weapon>(weapons, &Weapon::id, "the weapon"); !checked) {
        return std::unexpected{std::move(checked.error())};
    }
    if (auto checked = checkUnique<Weapon>(weapons, &Weapon::classname, "the weapon class"); !checked) {
        return std::unexpected{std::move(checked.error())};
    }
    std::set<std::pair<WeaponSlot, std::uint32_t>> places;
    for (const Weapon& weapon : weapons) {
        if (auto checked = checkView(weapon.view, weapon.id, "the catalog"); !checked) {
            return std::unexpected{std::move(checked.error())};
        }
        if (auto checked = checkFiring(weapon, "the catalog"); !checked) {
            return std::unexpected{std::move(checked.error())};
        }
        if (!places.emplace(weapon.slot, weapon.position).second) {
            return nx::fail(
                WeaponError::DataInvalid,
                std::format("{:?} at position {} of its slot, as another is", weapon.id, weapon.position));
        }
        if (weapon.magazine && !std::ranges::contains(ammo.ammo, weapon.magazine->ammo, &AmmoType::id)) {
            return nx::fail(WeaponError::DataInvalid, std::format("{:?} fires {:?}, not a kind of ammo",
                                                                  weapon.id, weapon.magazine->ammo));
        }
    }
    return WeaponCatalog{std::move(ammo.ammo), std::move(weapons)};
}

auto WeaponCatalog::findAmmo(std::string_view id) const noexcept -> const AmmoType* {
    const auto found = std::ranges::find(m_ammo, id, &AmmoType::id);
    return found == m_ammo.end() ? nullptr : &*found;
}

auto WeaponCatalog::findWeapon(std::string_view id) const noexcept -> const Weapon* {
    const auto found = std::ranges::find(m_weapons, id, &Weapon::id);
    return found == m_weapons.end() ? nullptr : &*found;
}

auto WeaponCatalog::indexOf(std::string_view id) const noexcept -> std::optional<WeaponIndex> {
    const auto found = std::ranges::find(m_weapons, id, &Weapon::id);
    if (found == m_weapons.end()) {
        return std::nullopt;
    }
    return WeaponIndex{nx::narrow<std::uint32_t>(found - m_weapons.begin())};
}

auto WeaponCatalog::weapon(WeaponIndex index) const noexcept -> const Weapon& {
    nx::expects(index.value < m_weapons.size(), "WeaponCatalog::weapon: an index of the catalog");
    return m_weapons[index.value];
}

} // namespace cs
