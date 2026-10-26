// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.render;

import std;
import nx.core;

namespace nx::render {

namespace {

/// The brightness each letter after 'a' adds, and that of a style without a pattern.
constexpr std::uint16_t kValuePerLetter = 22;
constexpr std::uint16_t kUnsetValue = 256;
/// The letters the original steps through in a second (R_AnimateLight).
constexpr float kLettersPerSecond = 10.0F;

} // namespace

void LightStyles::set(std::size_t style, std::string_view pattern) {
    expects(style < kLightStyleCount, "LightStyles::set: a style below kLightStyleCount");
    if (style >= kLightStyleCount) {
        return; // the contract handler observed the invalid index
    }
    m_patterns[style] = pattern |
                        std::views::transform([](char letter) { return std::clamp(letter, 'a', 'z'); }) |
                        std::ranges::to<std::string>();
}

void LightStyles::setPatterns(std::span<const std::string, kLightStyleCount> patterns) {
    for (std::size_t style = 0; style < patterns.size(); ++style) {
        set(style, patterns[style]);
    }
}

auto LightStyles::valuesAt(Seconds time) const noexcept -> LightStyleValues {
    expects(time.count() >= 0.0F, "LightStyles::valuesAt: a time after the map started");
    const auto step = static_cast<std::uint64_t>(time.count() * kLettersPerSecond);
    LightStyleValues values{};
    for (std::size_t style = 0; style < kLightStyleCount; ++style) {
        const std::string& pattern = m_patterns[style];
        values[style] = pattern.empty()
                            ? kUnsetValue
                            : narrow<std::uint16_t>((pattern[step % pattern.size()] - 'a') * kValuePerLetter);
    }
    return values;
}

} // namespace nx::render
