// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The characters a player of each team plays as (docs/10-counter-strike-gameplay.md, section 5):
/// data/cstrike/characters.toml, as the original's appearance menu offers them, in its order (ReGameDLL_CS's
/// HandleMenu_ChooseAppearance, GPL, consulted: docs/20 section 3), each its model
/// models/player/<name>/<name>.mdl.
export module cs.shared:characters;

import std;
import nx.core;
import nx.meta;
import nx.data;
import :player;

export namespace cs {

/// Why the characters could not be read.
enum class CharacterError : std::uint8_t {
    DataInvalid, ///< The file breaks its schema or its rules (cause: the DataError, when there is one).
};

/// A character a player plays as: its name, which names its model, and its team.
struct Character {
    std::string name{};
    Team team{};
};

/// A character of the game's, by its order.
struct CharacterIndex {
    std::uint32_t value = 0;

    [[nodiscard]] inline auto operator==(const CharacterIndex&) const noexcept -> bool = default;
};

/// data/cstrike/characters.toml: the characters, each team's in the order its menu offers them.
struct Characters {
    std::vector<Character> characters{};

    /// The characters of `team`, in their order.
    [[nodiscard]] auto ofTeam(Team team) const -> std::vector<CharacterIndex>;
    /// The character `index` names, which must be one of them.
    [[nodiscard]] auto character(CharacterIndex index) const noexcept -> const Character&;
};

/// The model of `character`: models/player/<name>/<name>.mdl.
[[nodiscard]] auto modelOf(const Character& character) -> std::string;

/// Reads `text`, the file `sourceName`: each name once, of letters and digits, and a character for
/// each team at least. Errors: DataInvalid.
[[nodiscard]] auto readCharacters(std::string_view text, std::string_view sourceName)
    -> nx::Result<Characters, CharacterError>;

} // namespace cs

template <>
struct nx::meta::Description<cs::Character> {
    static constexpr std::string_view kName = "cs::Character";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::Character::name>("name"),
        nx::meta::field<&cs::Character::team>("team"),
    };
};

template <>
struct nx::meta::Description<cs::Characters> {
    static constexpr std::string_view kName = "cs::Characters";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::Characters::characters>("characters"),
    };
};
