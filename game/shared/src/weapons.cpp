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
/// The keys of what the catalog knows a weapon by, which no var sets.
constexpr std::array<std::string_view, 4> kReadOnlyVars{"id", "classname", "slot", "position"};
/// The classname of a weapon made as the game runs: this, then its id.
constexpr std::string_view kClassnamePrefix = "weapon_";

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
            for (const std::optional<GameSound>* const sound :
                 {&attack->missSound, &attack->hitSound, &attack->bodySound}) {
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

/// DataInvalid when two of `items` have the same `key`, in any case, which names what they are:
/// `what`.
template <class Item>
[[nodiscard]] auto checkUnique(std::span<const Item> items, std::string Item::* key, std::string_view what)
    -> nx::Result<void, WeaponError> {
    std::set<std::string> seen;
    for (const Item& item : items) {
        if (!seen.insert(item.*key | std::views::transform(nx::toLowerAscii) | std::ranges::to<std::string>())
                 .second) {
            return nx::fail(WeaponError::DataInvalid, std::format("{} {:?} twice", what, item.*key));
        }
    }
    return {};
}

/// DataInvalid when `weapon`, of `where` (a file, or the catalog), lacks an id, a classname, a name,
/// a magazine's kind of ammo or its player's animation, or has a model path checkModelPath refuses,
/// or a view or firing checkView or checkFiring refuses.
[[nodiscard]] auto checkWeapon(const Weapon& weapon, std::string_view where)
    -> nx::Result<void, WeaponError> {
    if (weapon.id.empty() || weapon.classname.empty() || weapon.name.empty()) {
        return nx::fail(WeaponError::DataInvalid,
                        std::format("{}: a weapon without an id, a classname or a name", where));
    }
    if (weapon.magazine && weapon.magazine->ammo.empty()) {
        return nx::fail(WeaponError::DataInvalid,
                        std::format("{}: {:?} fires no kind of ammo", where, weapon.id));
    }
    if (weapon.models.playerAnimation.empty()) {
        return nx::fail(WeaponError::DataInvalid,
                        std::format("{}: {:?} without its player's animation", where, weapon.id));
    }
    if (auto checked = checkModelPath(weapon.models.view, "view", weapon.id, where); !checked) {
        return checked;
    }
    if (auto checked = checkModelPath(weapon.models.player, "player", weapon.id, where); !checked) {
        return checked;
    }
    if (auto checked = checkView(weapon.view, weapon.id, where); !checked) {
        return checked;
    }
    return checkFiring(weapon, where);
}

/// Whether `id` is one of a weapon made as the game runs: letters and digits alone, one at least.
[[nodiscard]] auto isWeaponId(std::string_view id) noexcept -> bool {
    return !id.empty() && std::ranges::all_of(id, [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9');
    });
}

/// The first key of `keyPath`.
[[nodiscard]] auto firstKey(std::string_view keyPath) noexcept -> std::string_view {
    return keyPath.substr(0, keyPath.find('.'));
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
    for (const AmmoType& type : types->ammo) {
        if (auto parsed = nx::assets::VfsPath::parse(type.picture.sprite); !parsed) {
            return nx::fail(
                WeaponError::DataInvalid,
                std::format("{}: the picture of {:?}, {:?}", sourceName, type.id, type.picture.sprite),
                std::move(parsed.error()));
        }
    }
    return std::move(*types);
}

auto readWeapon(std::string_view text, std::string_view sourceName) -> nx::Result<Weapon, WeaponError> {
    auto weapon = nx::data::parseToml<Weapon>(text, sourceName);
    if (!weapon) {
        return nx::fail(WeaponError::DataInvalid, std::string{sourceName}, std::move(weapon.error()));
    }
    if (auto checked = checkWeapon(*weapon, sourceName); !checked) {
        return std::unexpected{std::move(checked.error())};
    }
    return std::move(*weapon);
}

auto soundsOf(const Weapon& weapon) -> std::vector<const GameSound*> {
    std::vector<const std::optional<GameSound>*> places{&weapon.view.drawSound};
    const auto addFiring = [&places](const std::optional<WeaponFiring>& firing) {
        if (firing) {
            places.insert(places.end(), {&firing->sound, &firing->emptySound});
        }
    };
    addFiring(weapon.firing);
    if (const std::optional<WeaponAlternate>& alternate = weapon.alternate) {
        if (alternate->view) {
            places.push_back(&alternate->view->drawSound);
        }
        addFiring(alternate->firing);
    }
    if (const std::optional<WeaponMelee>& melee = weapon.melee) {
        for (const MeleeAttack* const attack : {&melee->slash, &melee->stab}) {
            places.insert(places.end(), {&attack->missSound, &attack->hitSound, &attack->bodySound});
        }
    }
    std::vector<const GameSound*> sounds;
    for (const std::optional<GameSound>* const place : places) {
        if (*place) {
            sounds.push_back(&**place);
        }
    }
    return sounds;
}

WeaponCatalog::WeaponCatalog(std::vector<AmmoType> ammo, std::vector<Weapon> weapons) noexcept
    : m_ammo{std::move(ammo)},
      m_weapons{std::make_move_iterator(weapons.begin()), std::make_move_iterator(weapons.end())},
      m_standings(m_weapons.size(), Standing{.isComplete = true}) {}

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
    WeaponCatalog catalog{std::move(ammo.ammo), std::move(weapons)};
    std::set<std::pair<WeaponSlot, std::uint32_t>> places;
    for (const Weapon& weapon : catalog.m_weapons) {
        if (auto checked = catalog.check(weapon, "the catalog"); !checked) {
            return std::unexpected{std::move(checked.error())};
        }
        if (!places.emplace(weapon.slot, weapon.position).second) {
            return nx::fail(
                WeaponError::DataInvalid,
                std::format("{:?} at position {} of its slot, as another is", weapon.id, weapon.position));
        }
    }
    return catalog;
}

auto WeaponCatalog::findAmmo(std::string_view id) const noexcept -> const AmmoType* {
    const auto found = std::ranges::find(m_ammo, id, &AmmoType::id);
    return found == m_ammo.end() ? nullptr : &*found;
}

auto WeaponCatalog::findWeapon(std::string_view id) const noexcept -> const Weapon* {
    const std::optional<WeaponIndex> found = indexOf(id);
    return found ? &m_weapons[found->value] : nullptr;
}

auto WeaponCatalog::indexOf(std::string_view id) const noexcept -> std::optional<WeaponIndex> {
    // No two weapons' ids differ only in case (create, make): one at most is so named.
    const auto found = std::ranges::find_if(
        m_weapons, [id](const Weapon& weapon) { return nx::equalsIgnoringCase(weapon.id, id); });
    if (found == m_weapons.end()) {
        return std::nullopt;
    }
    return WeaponIndex{nx::narrow<std::uint32_t>(found - m_weapons.begin())};
}

auto WeaponCatalog::weapon(WeaponIndex index) const noexcept -> const Weapon& {
    nx::expects(index.value < m_weapons.size(), "WeaponCatalog::weapon: an index of the catalog");
    return m_weapons[index.value];
}

auto WeaponCatalog::create(const WeaponDraft& draft) -> nx::Result<WeaponIndex, WeaponError> {
    if (!isWeaponId(draft.id)) {
        return nx::fail(WeaponError::IdInvalid,
                        std::format("{:?}: a weapon's id is letters and digits alone", draft.id));
    }
    const std::string classname = std::format("{}{}", kClassnamePrefix, draft.id);
    if (std::ranges::any_of(m_weapons, [&](const Weapon& other) {
            return nx::equalsIgnoringCase(other.id, draft.id) ||
                   nx::equalsIgnoringCase(other.classname, classname);
        })) {
        return nx::fail(WeaponError::IdTaken,
                        std::format("{:?}: a weapon of the catalog is so called", draft.id));
    }
    Weapon made = draft.from ? weapon(*draft.from) : Weapon{};
    made.id = draft.id;
    made.classname = classname;
    made.name = draft.id;
    made.slot = draft.slot;
    made.position = 0;
    for (const Weapon& other : m_weapons) {
        if (other.slot == draft.slot) {
            made.position = std::max(made.position, other.position + 1);
        }
    }
    const bool isComplete = check(made, made.id).has_value();
    m_weapons.push_back(std::move(made));
    m_standings.push_back(Standing{.isComplete = isComplete});
    return WeaponIndex{nx::narrow<std::uint32_t>(m_weapons.size() - 1)};
}

auto WeaponCatalog::setVar(WeaponIndex weapon, std::string_view keyPath, std::string_view text)
    -> nx::Result<void, WeaponError> {
    const Weapon& current = this->weapon(weapon);
    if (std::ranges::contains(kReadOnlyVars, firstKey(keyPath))) {
        return nx::fail(WeaponError::VarReadOnly,
                        std::format("{}: {} is what the catalog knows it by", current.id, keyPath));
    }
    Weapon changed = current;
    if (auto assigned = nx::data::assignField(changed, keyPath, text, current.id); !assigned) {
        return nx::fail(WeaponError::VarInvalid, std::move(assigned.error()));
    }
    Standing& standing = m_standings[weapon.value];
    auto checked = check(changed, changed.id);
    if (standing.isComplete && !checked) {
        return nx::fail(WeaponError::VarInvalid,
                        std::format("{}: {} = {} leaves it incomplete", current.id, keyPath, text),
                        std::move(checked.error()));
    }
    m_weapons[weapon.value] = std::move(changed);
    standing.isComplete = checked.has_value();
    ++standing.revision;
    return {};
}

auto WeaponCatalog::var(WeaponIndex weapon, std::string_view keyPath) const
    -> nx::Result<std::optional<std::string>, WeaponError> {
    const Weapon& held = this->weapon(weapon);
    auto written = nx::data::formatField(held, keyPath, held.id);
    if (!written) {
        return nx::fail(WeaponError::VarInvalid, std::move(written.error()));
    }
    return std::move(*written);
}

auto WeaponCatalog::vars(WeaponIndex weapon) const -> std::vector<nx::data::FieldText> {
    return nx::data::listFields(this->weapon(weapon));
}

auto WeaponCatalog::isComplete(WeaponIndex weapon) const noexcept -> bool {
    return standing(weapon).isComplete;
}

auto WeaponCatalog::checkComplete(WeaponIndex weapon) const -> nx::Result<void, WeaponError> {
    const Weapon& held = this->weapon(weapon);
    if (auto checked = check(held, held.id); !checked) {
        return nx::fail(WeaponError::Incomplete, std::format("{}: not a weapon yet", held.id),
                        std::move(checked.error()));
    }
    return {};
}

auto WeaponCatalog::revision(WeaponIndex weapon) const noexcept -> std::uint32_t {
    return standing(weapon).revision;
}

auto WeaponCatalog::replace(WeaponIndex original, std::optional<WeaponIndex> replacement)
    -> nx::Result<void, WeaponError> {
    const Weapon& replaced = weapon(original);
    if (replacement) {
        const Weapon& made = weapon(*replacement);
        const auto invalid = [&](std::string_view problem) {
            return nx::fail(WeaponError::ReplacementInvalid,
                            std::format("{} by {}: {}", replaced.id, made.id, problem));
        };
        if (*replacement == original) {
            return invalid("a weapon replaced by itself");
        }
        if (standing(*replacement).replacement) {
            return invalid("the replacement is replaced in its turn");
        }
        if (std::ranges::contains(m_standings, std::optional{original}, &Standing::replacement)) {
            return invalid("the weapon replaces another");
        }
        if (!isComplete(*replacement)) {
            return invalid("the replacement is incomplete");
        }
    }
    m_standings[original.value].replacement = replacement;
    return {};
}

auto WeaponCatalog::madeFor(WeaponIndex weapon) const noexcept -> WeaponIndex {
    return standing(weapon).replacement.value_or(weapon);
}

auto WeaponCatalog::check(const Weapon& weapon, std::string_view where) const
    -> nx::Result<void, WeaponError> {
    if (auto checked = checkWeapon(weapon, where); !checked) {
        return checked;
    }
    if (weapon.magazine && findAmmo(weapon.magazine->ammo) == nullptr) {
        return nx::fail(WeaponError::DataInvalid, std::format("{}: {:?} fires {:?}, not a kind of ammo",
                                                              where, weapon.id, weapon.magazine->ammo));
    }
    return {};
}

auto WeaponCatalog::standing(WeaponIndex weapon) const noexcept -> const Standing& {
    nx::expects(weapon.value < m_standings.size(), "WeaponCatalog::standing: an index of the catalog");
    return m_standings[weapon.value];
}

} // namespace cs
