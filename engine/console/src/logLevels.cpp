// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.console;

import std;
import nx.core;

namespace nx::console {

auto LogLevelCvars::registerIn(Console& console, log::Logger& logger,
                               std::span<const log::Category> categories)
    -> Result<LogLevelCvars, CvarError> {
    std::vector<std::string> names;
    names.reserve(categories.size());
    for (const log::Category category : categories) {
        std::string name = std::format("{}{}", kLogLevelCvarPrefix, category.name);
        if (auto available = console.checkCvarName(name); !available) {
            return std::unexpected(std::move(available.error()));
        }
        if (std::ranges::any_of(
                names, [&name](const std::string& other) { return equalsIgnoringCase(other, name); })) {
            return fail(CvarError::NameTaken, std::format("{} (a category given twice)", name));
        }
        names.push_back(std::move(name));
    }

    std::vector<Binding> bindings;
    bindings.reserve(categories.size());
    for (std::size_t index = 0; index < categories.size(); ++index) {
        const log::Category category = categories[index];
        const std::string description = std::format("Lowest level logged for the {} category", category.name);
        auto cvar = console.registerCvar(CvarDesc<log::Level>{.name = std::string_view{names[index]},
                                                              .defaultValue = logger.categoryLevel(category),
                                                              .description = description});
        // The names are free and the default is a level: registering fails only on a broken console.
        invariant(cvar.has_value(), "LogLevelCvars: a checked name failed to register");
        if (!cvar) {
            return std::unexpected(std::move(cvar.error()));
        }
        bindings.push_back(Binding{.category = category, .cvar = *cvar, .appliedRevision = cvar->revision()});
    }
    return LogLevelCvars{logger, std::move(bindings)};
}

void LogLevelCvars::apply() {
    for (Binding& binding : m_bindings) {
        if (binding.cvar.revision() != binding.appliedRevision) {
            m_logger->setCategoryLevel(binding.category, binding.cvar.value());
            binding.appliedRevision = binding.cvar.revision();
        }
    }
}

} // namespace nx::console
