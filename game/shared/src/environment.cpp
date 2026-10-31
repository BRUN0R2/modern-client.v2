// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.data;
import nx.assets;

namespace cs {

namespace {

/// Whether `name` names a sky the client can look for: of some name, with no path in it (gfx/env/<name>).
[[nodiscard]] auto isSkyName(std::string_view name) noexcept -> bool {
    return !name.empty() && name.find_first_of("/\\:.") == std::string_view::npos;
}

} // namespace

auto readWorldEnvironment(std::string_view text, std::string_view sourceName)
    -> nx::Result<WorldEnvironment, EnvironmentError> {
    auto environment = nx::data::parseToml<WorldEnvironment>(text, sourceName);
    if (!environment) {
        return nx::fail(EnvironmentError::DataInvalid, std::string{sourceName},
                        std::move(environment.error()));
    }
    if (environment->sky && !isSkyName(*environment->sky)) {
        return nx::fail(EnvironmentError::DataInvalid, std::format("{}: the sky {:?} names no sky of gfx/env",
                                                                   sourceName, *environment->sky));
    }
    return std::move(*environment);
}

auto skyNameOf(const nx::assets::BspView& map, const WorldEnvironment& environment)
    -> std::optional<std::string_view> {
    if (environment.sky) {
        return std::string_view{*environment.sky};
    }
    if (map.entities().empty()) {
        return std::nullopt;
    }
    return map.entities()[0].find("skyname");
}

} // namespace cs
