// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What a player hears as it uses what is in front of it (docs/07-simulation-and-physics.md, section
/// 5.2): data/cstrike/use.toml, with ReGameDLL_CS's sounds (CBasePlayer::PlayerUse and UseEmpty; GPL,
/// consulted: docs/20 section 3).
export module cs.shared:use;

import std;
import nx.core;
import nx.meta;
import nx.data;
import :sounds;

export namespace cs {

/// Why the use's data could not be read.
enum class UseError : std::uint8_t {
    DataInvalid, ///< The file breaks its schema or its rules (cause: the DataError, when there is one).
};

/// data/cstrike/use.toml: what the use key going down sounds like.
struct UseRules {
    GameSound found{};   ///< With something to use in front of the player (PlayerUse).
    GameSound nothing{}; ///< With nothing to use in front of it (UseEmpty).

    /// Every file its sounds play, each once, in the order the file names them.
    [[nodiscard]] auto soundFiles() const -> std::vector<std::string_view>;
};

/// Reads `text`, the file `sourceName`: each sound with a file. Errors: DataInvalid.
[[nodiscard]] auto readUseRules(std::string_view text, std::string_view sourceName)
    -> nx::Result<UseRules, UseError>;

} // namespace cs

template <>
struct nx::meta::Description<cs::UseRules> {
    static constexpr std::string_view kName = "cs::UseRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::UseRules::found>("found"),
        nx::meta::field<&cs::UseRules::nothing>("nothing"),
    };
};
