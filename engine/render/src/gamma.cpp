// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.render;

import std;
import nx.core;

namespace nx::render {

namespace {

/// The settings in the ranges the original holds them to.
struct Bounded {
    double gamma = 0.0;
    double brightness = 0.0;
    double textureGamma = 0.0;
    double lightGamma = 0.0;
};

[[nodiscard]] auto bounded(const GammaSettings& settings) noexcept -> Bounded {
    constexpr double kLowestGamma = 1.8;
    constexpr double kHighestGamma = 3.0;
    constexpr double kHighestBrightness = 3.0;
    return {.gamma = std::clamp(double{settings.gamma}, kLowestGamma, kHighestGamma),
            .brightness = std::clamp(double{settings.brightness}, 0.0, kHighestBrightness),
            .textureGamma = std::clamp(double{settings.textureGamma}, kLowestGamma, kHighestGamma),
            .lightGamma = std::clamp(double{settings.lightGamma}, kLowestGamma, kHighestGamma)};
}

/// Below this light, `brightness` stretches the curve: the shadows it lifts.
[[nodiscard]] auto shadowKnee(double brightness) noexcept -> double {
    if (brightness <= 0.0) {
        return 0.125;
    }
    return brightness <= 1.0 ? 0.125 - (brightness * brightness * 0.075) : 0.05;
}

} // namespace

auto textureGammaTable(const GammaSettings& settings) -> std::array<std::uint8_t, 256> {
    const Bounded values = bounded(settings);
    const double exponent = values.textureGamma / values.gamma;
    std::array<std::uint8_t, 256> table{};
    for (std::size_t index = 0; index < table.size(); ++index) {
        // Truncated, as the original converts.
        const auto value = static_cast<int>(std::pow(static_cast<double>(index) / 255.0, exponent) * 255.0);
        table[index] = narrow<std::uint8_t>(std::clamp(value, 0, 255));
    }
    return table;
}

auto lightTable(const GammaSettings& settings) -> std::array<std::uint8_t, kLightTableSize> {
    const Bounded values = bounded(settings);
    const double knee = shadowKnee(values.brightness);
    constexpr double kTop = static_cast<double>(kLightTableSize - 1);
    std::array<std::uint8_t, kLightTableSize> table{};
    for (std::size_t index = 0; index < table.size(); ++index) {
        double light = std::pow(static_cast<double>(index) / kTop, values.lightGamma);
        if (values.brightness > 1.0) {
            light *= values.brightness;
        }
        light = light <= knee ? (light / knee) * 0.125 : (((light - knee) / (1.0 - knee)) * 0.875) + 0.125;
        const auto level = static_cast<int>(std::pow(light, 1.0 / values.gamma) * kTop);
        table[index] = narrow<std::uint8_t>(std::clamp(level, 0, static_cast<int>(kTop)) >> 2);
    }
    return table;
}

auto lightTableScale(const GammaSettings& settings) -> float {
    const Bounded values = bounded(settings);
    constexpr double kFixedPointOne = 256.0;
    constexpr double kShiftedOut = 16384.0; // >> 14
    const auto lightScale = static_cast<int>((std::pow(2.0, 1.0 / values.lightGamma) * kFixedPointOne) + 0.5);
    return static_cast<float>(static_cast<double>(kNormalLightStyleValue) * lightScale / kShiftedOut);
}

} // namespace nx::render
