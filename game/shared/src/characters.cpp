// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.data;

namespace cs {

auto Characters::ofTeam(Team team) const -> std::vector<CharacterIndex> {
    std::vector<CharacterIndex> found;
    for (std::uint32_t index = 0; index < characters.size(); ++index) {
        if (characters[index].team == team) {
            found.push_back(CharacterIndex{index});
        }
    }
    return found;
}

auto Characters::character(CharacterIndex index) const noexcept -> const Character& {
    nx::expects(index.value < characters.size(), "Characters::character: one of them");
    return characters[std::min<std::size_t>(index.value, characters.size() - 1)];
}

auto modelOf(const Character& character) -> std::string {
    return std::format("models/player/{0}/{0}.mdl", character.name);
}

auto readCharacters(std::string_view text, std::string_view sourceName)
    -> nx::Result<Characters, CharacterError> {
    auto read = nx::data::parseToml<Characters>(text, sourceName);
    if (!read) {
        return nx::fail(CharacterError::DataInvalid, std::string{sourceName}, std::move(read.error()));
    }
    std::set<std::string_view> names;
    for (const Character& character : read->characters) {
        if (character.name.empty() || !std::ranges::all_of(character.name, [](char letter) {
                return std::isalnum(static_cast<unsigned char>(letter)) != 0;
            })) {
            return nx::fail(
                CharacterError::DataInvalid,
                std::format("{}: the character {:?}, not of letters and digits", sourceName, character.name));
        }
        if (!names.insert(character.name).second) {
            return nx::fail(CharacterError::DataInvalid,
                            std::format("{}: the character {:?} twice", sourceName, character.name));
        }
    }
    for (const Team team : {Team::Terrorists, Team::CounterTerrorists}) {
        if (read->ofTeam(team).empty()) {
            return nx::fail(CharacterError::DataInvalid,
                            std::format("{}: no character of the {}", sourceName, nx::enumName(team)));
        }
    }
    return std::move(*read);
}

} // namespace cs
