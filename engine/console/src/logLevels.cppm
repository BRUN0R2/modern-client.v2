// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Log levels as cvars (docs/02-coding-standards.md, section 10): `log_level_net debug` sets the
/// threshold of the "net" category.
export module nx.console:logLevels;

import std;
import nx.core;
import :cvar;
import :console;

export namespace nx::console {

/// Name of the cvar that holds the level of a log category: `log_level_<category>`.
inline constexpr std::string_view kLogLevelCvarPrefix = "log_level_";

/// One `log_level_<category>` cvar per log category, applied to a Logger. A level is written as its
/// number or its name (`log_level_net debug`). The console has no change callbacks, so the owner
/// calls apply() once a frame, after Console::execute.
class LogLevelCvars {
public:
    /// Registers one cvar per category in `console`, defaulting to the level `logger` applies to the
    /// category now. Both must outlive the result. Every name is checked before any cvar is
    /// registered, so a failure registers none. Errors: those of Console::checkCvarName (a category
    /// whose cvar name the console cannot invoke, a name already taken or given twice).
    [[nodiscard]] static auto registerIn(Console& console, log::Logger& logger,
                                         std::span<const log::Category> categories)
        -> Result<LogLevelCvars, CvarError>;

    /// Copies to the logger every level changed since the last call.
    void apply();

private:
    struct Binding {
        log::Category category;
        Cvar<log::Level> cvar;
        std::uint64_t appliedRevision = 0;
    };

    LogLevelCvars(log::Logger& logger, std::vector<Binding> bindings) noexcept
        : m_logger{&logger}, m_bindings{std::move(bindings)} {}

    log::Logger* m_logger;
    std::vector<Binding> m_bindings;
};

} // namespace nx::console
