// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.data;

namespace cs {

auto UseRules::soundFiles() const -> std::vector<std::string_view> {
    std::vector<std::string_view> files;
    for (const GameSound* const sound : {&found, &nothing}) {
        for (const std::string& file : sound->files) {
            if (!std::ranges::contains(files, std::string_view{file})) {
                files.emplace_back(file);
            }
        }
    }
    return files;
}

auto readUseRules(std::string_view text, std::string_view sourceName) -> nx::Result<UseRules, UseError> {
    auto rules = nx::data::parseToml<UseRules>(text, sourceName);
    if (!rules) {
        return nx::fail(UseError::DataInvalid, std::string{sourceName}, std::move(rules.error()));
    }
    for (const auto& [what, sound] : {std::pair{"the sound of something to use", &rules->found},
                                      std::pair{"the sound of nothing to use", &rules->nothing}}) {
        if (const std::optional<std::string_view> problem = soundProblem(*sound)) {
            return nx::fail(UseError::DataInvalid, std::format("{}: {} {}", sourceName, what, *problem));
        }
    }
    return std::move(*rules);
}

} // namespace cs
