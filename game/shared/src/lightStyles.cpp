// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.data;
import nx.assets;

namespace cs {

auto readWorldLightStyles(std::string_view text, std::string_view sourceName)
    -> nx::Result<WorldLightStyles, LightStyleError> {
    auto world = nx::data::parseToml<WorldLightStyles>(text, sourceName);
    if (!world) {
        return nx::fail(LightStyleError::DataInvalid, std::string{sourceName}, std::move(world.error()));
    }
    std::array<bool, nx::assets::kLightStyleCount> styles{};
    for (const WorldLightStyle& style : world->styles) {
        if (style.pattern.empty() ||
            !std::ranges::all_of(style.pattern, [](char letter) { return letter >= 'a' && letter <= 'z'; })) {
            return nx::fail(LightStyleError::BadPattern,
                            std::format("{}: style {}'s pattern {:?}, not letters from a to z", sourceName,
                                        style.style, style.pattern));
        }
        if (std::exchange(styles[style.style], true)) {
            return nx::fail(LightStyleError::StyleTwice,
                            std::format("{}: style {} twice", sourceName, style.style));
        }
    }
    return std::move(*world);
}

auto mapLightStyles(const nx::assets::BspView& map, const WorldLightStyles& world) -> MapLightStyles {
    MapLightStyles styles;
    for (const WorldLightStyle& style : world.styles) {
        nx::expects(style.style < styles.patterns.size(), "mapLightStyles: a valid world light style");
        if (style.style < styles.patterns.size()) {
            styles.patterns[style.style] = style.pattern;
        }
    }
    // A light with a name and a style from 32 on can be switched: 'a' when it starts off, else 'm', as
    // Quake's light.qc sets them, or the `pattern` the FGDs document.
    constexpr std::size_t kFirstSwitchableStyle = 32;
    constexpr std::uint32_t kStartsOff = 1;
    for (const nx::assets::BspEntity& entity : map.entities()) {
        const std::string_view classname = entity.find("classname").value_or("");
        if ((classname != "light" && classname != "light_spot") || !entity.find("targetname")) {
            continue;
        }
        const std::optional<int> style = entity.findInteger("style");
        if (!style || std::cmp_less(*style, kFirstSwitchableStyle) ||
            std::cmp_greater_equal(*style, nx::assets::kLightStyleCount)) {
            continue;
        }
        const auto flags = static_cast<std::uint32_t>(entity.findInteger("spawnflags").value_or(0));
        styles.patterns[nx::narrow<std::size_t>(*style)] =
            (flags & kStartsOff) != 0 ? "a" : entity.find("pattern").value_or("m");
    }
    return styles;
}

} // namespace cs
