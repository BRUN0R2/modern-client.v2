// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.console;
import nx.data;
import cs.shared;

namespace cs {

namespace {

using nx::console::CommandLine;
using nx::console::Console;
using nx::console::ConsoleError;

constexpr std::string_view kWhitespace = " \t";

[[nodiscard]] auto noMap(std::string_view command) -> std::string {
    return std::format("Can't {:?}, not playing a map", command);
}

/// The weapon `name` names in `weapons`. Errors: InvalidArguments.
[[nodiscard]] auto weaponNamed(const WeaponCatalog& weapons, std::string_view name)
    -> nx::Result<WeaponIndex, ConsoleError> {
    if (const std::optional<WeaponIndex> weapon = findWeaponNamed(weapons, name)) {
        return *weapon;
    }
    return nx::fail(ConsoleError::InvalidArguments, std::format("no weapon {:?}", name));
}

/// The slot `name` names, as the weapons' data names it ("primary"). Errors: InvalidArguments.
[[nodiscard]] auto slotNamed(std::string_view name) -> nx::Result<WeaponSlot, ConsoleError> {
    if (const std::optional<WeaponSlot> slot = nx::data::enumeratorNamed<WeaponSlot>(name)) {
        return *slot;
    }
    return nx::fail(ConsoleError::InvalidArguments,
                    std::format("no slot {:?}: primary, secondary, knife, grenade or bomb", name));
}

/// `failure` of the catalog as the console's: CommandFailed, its cause the catalog's.
[[nodiscard]] auto failed(std::string_view command, nx::Failure<WeaponError> failure)
    -> std::unexpected<nx::Failure<ConsoleError>> {
    return nx::fail(ConsoleError::CommandFailed, std::string{command}, std::move(failure));
}

/// What a weapon is, a line of `list_weapons`.
[[nodiscard]] auto describeWeapon(const WeaponCatalog& weapons, WeaponIndex weapon) -> std::string {
    const Weapon& made = weapons.weapon(weapon);
    std::string line = std::format("{} ({}, {:?}): {} slot, position {}", made.id, made.classname, made.name,
                                   nx::data::dataNameOf(made.slot), made.position);
    if (!weapons.isComplete(weapon)) {
        line += ", incomplete";
    }
    if (const WeaponIndex madeFor = weapons.madeFor(weapon); madeFor != weapon) {
        line += std::format(", replaced by {}", weapons.weapon(madeFor).id);
    }
    return line;
}

/// The commands, each a member: they share the console, the catalog and the actions, and live as
/// long as a handler of the console's holds them.
class WeaponCommands : public std::enable_shared_from_this<WeaponCommands> {
public:
    WeaponCommands(Console& console, WeaponCatalog& weapons, WeaponActions actions) noexcept
        : m_console{&console}, m_weapons{&weapons}, m_actions{std::move(actions)} {}

    /// Registers `weapon_<id>`, which draws `weapon`.
    [[nodiscard]] auto registerSelect(WeaponIndex weapon) -> nx::Result<void, ConsoleError> {
        const std::string& classname = m_weapons->weapon(weapon).classname;
        return m_console->registerCommand(
            {.name = std::string_view{classname}, .description = "Draws the weapon, when the player has it."},
            [self = shared_from_this(), weapon,
             classname](Console&, const CommandLine&) -> nx::Result<void, ConsoleError> {
                if (!self->m_actions.select(weapon)) {
                    self->m_console->print(noMap(classname));
                }
                return {};
            });
    }

    auto create(const CommandLine& line) -> nx::Result<void, ConsoleError> {
        const std::span<const std::string> words = line.arguments();
        if (words.size() != 2 && words.size() != 3) {
            return nx::fail(ConsoleError::InvalidArguments, "usage: create_weapon <id> <slot> [from weapon]");
        }
        auto slot = slotNamed(words[1]);
        if (!slot) {
            return std::unexpected{std::move(slot.error())};
        }
        std::optional<WeaponIndex> from;
        if (words.size() == 3) {
            auto named = weaponNamed(*m_weapons, words[2]);
            if (!named) {
                return std::unexpected{std::move(named.error())};
            }
            from = *named;
        }
        auto made = m_weapons->create({.id = words[0], .slot = *slot, .from = from});
        if (!made) {
            return failed("create_weapon", std::move(made.error()));
        }
        if (auto registered = registerSelect(*made); !registered) {
            return registered;
        }
        m_console->print(std::format("Made {}", describeWeapon(*m_weapons, *made)));
        return {};
    }

    auto setVar(const CommandLine& line) -> nx::Result<void, ConsoleError> {
        const std::span<const std::string> words = line.arguments();
        if (words.size() < 3) {
            return nx::fail(ConsoleError::InvalidArguments,
                            "usage: set_weapon_var <weapon> <key path> <value>, as its data writes it");
        }
        auto weapon = weaponNamed(*m_weapons, words[0]);
        if (!weapon) {
            return std::unexpected{std::move(weapon.error())};
        }
        const bool wasComplete = m_weapons->isComplete(*weapon);
        if (auto set = m_weapons->setVar(*weapon, words[1], textAfterWords(line.argumentText, 2)); !set) {
            return failed("set_weapon_var", std::move(set.error()));
        }
        if (!wasComplete && m_weapons->isComplete(*weapon)) {
            m_console->print(std::format("{} is complete", m_weapons->weapon(*weapon).id));
        }
        return {};
    }

    auto getVar(const CommandLine& line) -> nx::Result<void, ConsoleError> {
        const std::span<const std::string> words = line.arguments();
        if (words.size() != 1 && words.size() != 2) {
            return nx::fail(ConsoleError::InvalidArguments, "usage: get_weapon_var <weapon> [key path]");
        }
        auto weapon = weaponNamed(*m_weapons, words[0]);
        if (!weapon) {
            return std::unexpected{std::move(weapon.error())};
        }
        if (words.size() == 1) {
            for (const nx::data::FieldText& field : m_weapons->vars(*weapon)) {
                m_console->print(std::format("{} = {}", field.keyPath, field.value));
            }
            if (auto complete = m_weapons->checkComplete(*weapon); !complete) {
                m_console->print(nx::describe(complete.error()));
            }
            return {};
        }
        auto value = m_weapons->var(*weapon, words[1]);
        if (!value) {
            return failed("get_weapon_var", std::move(value.error()));
        }
        m_console->print(std::format("{} = {}", words[1], value->value_or("(none)")));
        return {};
    }

    void list() {
        for (std::uint32_t index = 0; index < m_weapons->weapons().size(); ++index) {
            m_console->print(describeWeapon(*m_weapons, WeaponIndex{index}));
        }
    }

    auto replace(const CommandLine& line) -> nx::Result<void, ConsoleError> {
        const std::span<const std::string> words = line.arguments();
        if (words.size() != 1 && words.size() != 2) {
            return nx::fail(ConsoleError::InvalidArguments, "usage: replace_weapon <weapon> [replacement]");
        }
        auto original = weaponNamed(*m_weapons, words[0]);
        if (!original) {
            return std::unexpected{std::move(original.error())};
        }
        std::optional<WeaponIndex> replacement;
        if (words.size() == 2) {
            auto named = weaponNamed(*m_weapons, words[1]);
            if (!named) {
                return std::unexpected{std::move(named.error())};
            }
            replacement = *named;
        }
        if (auto replaced = m_weapons->replace(*original, replacement); !replaced) {
            return failed("replace_weapon", std::move(replaced.error()));
        }
        m_console->print(describeWeapon(*m_weapons, *original));
        return {};
    }

    auto give(const CommandLine& line) -> nx::Result<void, ConsoleError> {
        const std::span<const std::string> words = line.arguments();
        if (words.size() != 1) {
            return nx::fail(ConsoleError::InvalidArguments, "usage: give <weapon>");
        }
        auto weapon = weaponNamed(*m_weapons, words[0]);
        if (!weapon) {
            return std::unexpected{std::move(weapon.error())};
        }
        if (auto complete = m_weapons->checkComplete(m_weapons->madeFor(*weapon)); !complete) {
            return failed("give", std::move(complete.error()));
        }
        if (!m_actions.give(*weapon)) {
            m_console->print(noMap("give"));
        }
        return {};
    }

private:
    Console* m_console;
    WeaponCatalog* m_weapons;
    WeaponActions m_actions;
};

} // namespace

auto findWeaponNamed(const WeaponCatalog& weapons, std::string_view name) noexcept
    -> std::optional<WeaponIndex> {
    if (const std::optional<WeaponIndex> byId = weapons.indexOf(name)) {
        return byId;
    }
    const auto found = std::ranges::find_if(weapons.weapons(), [name](const Weapon& weapon) {
        return nx::equalsIgnoringCase(weapon.classname, name);
    });
    if (found == weapons.weapons().end()) {
        return std::nullopt;
    }
    return WeaponIndex{nx::narrow<std::uint32_t>(found - weapons.weapons().begin())};
}

auto textAfterWords(std::string_view argumentText, std::size_t words) noexcept -> std::string_view {
    std::string_view rest = argumentText;
    for (std::size_t word = 0; word < words; ++word) {
        const std::size_t start = rest.find_first_not_of(kWhitespace);
        if (start == std::string_view::npos) {
            return {};
        }
        rest.remove_prefix(start);
        rest.remove_prefix(std::min(rest.find_first_of(kWhitespace), rest.size()));
    }
    const std::size_t start = rest.find_first_not_of(kWhitespace);
    if (start == std::string_view::npos) {
        return {};
    }
    rest.remove_prefix(start);
    return rest.substr(0, rest.find_last_not_of(kWhitespace) + 1);
}

auto registerWeaponCommands(Console& console, WeaponCatalog& weapons, WeaponActions actions)
    -> nx::Result<void, ConsoleError> {
    auto commands = std::make_shared<WeaponCommands>(console, weapons, std::move(actions));
    for (std::uint32_t index = 0; index < weapons.weapons().size(); ++index) {
        if (auto registered = commands->registerSelect(WeaponIndex{index}); !registered) {
            return registered;
        }
    }
    const auto add = [&console](nx::console::CommandDesc desc, nx::console::CommandHandler handler) {
        return console.registerCommand(desc, std::move(handler));
    };
    for (auto&& registered : {
             add({.name = "create_weapon",
                  .description = "Makes a weapon: create_weapon <id> <slot> [from weapon], its values the "
                                 "other's, copied once, or none."},
                 [commands](Console&, const CommandLine& line) { return commands->create(line); }),
             add({.name = "set_weapon_var",
                  .description = "Sets a weapon's value by the key path of its data: set_weapon_var <weapon> "
                                 "<key path> <value>."},
                 [commands](Console&, const CommandLine& line) { return commands->setVar(line); }),
             add({.name = "get_weapon_var",
                  .description = "Prints a weapon's value by its key path, or all of them: get_weapon_var "
                                 "<weapon> [key path]."},
                 [commands](Console&, const CommandLine& line) { return commands->getVar(line); }),
             add({.name = "list_weapons", .description = "Prints the weapons."},
                 [commands](Console&, const CommandLine&) {
                     commands->list();
                     return nx::Result<void, ConsoleError>{};
                 }),
             add({.name = "replace_weapon",
                  .description = "Makes a weapon wherever the game makes another, or stops: replace_weapon "
                                 "<weapon> [replacement]."},
                 [commands](Console&, const CommandLine& line) { return commands->replace(line); }),
             add({.name = "give", .description = "Gives the player a weapon: give <weapon>."},
                 [commands](Console&, const CommandLine& line) { return commands->give(line); }),
         }) {
        if (!registered) {
            return registered;
        }
    }
    return {};
}

} // namespace cs
