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

// The original's light curve bends at a knee: the light below it maps to the levels below
// kKneeLevel, the light above it to the rest. `brightness` lowers the knee, lifting the shadows.
constexpr double kKneeLevel = 0.125;
/// The knee without `brightness`, how it falls with brightness squared up to 1, and where it stays
/// above 1.
constexpr double kDarkKnee = 0.125;
constexpr double kKneeFall = 0.075;
constexpr double kBrightKnee = 0.05;

/// Below this light, `brightness` stretches the curve: the shadows it lifts.
[[nodiscard]] auto shadowKnee(double brightness) noexcept -> double {
    if (brightness <= 0.0) {
        return kDarkKnee;
    }
    return brightness <= 1.0 ? kDarkKnee - (brightness * brightness * kKneeFall) : kBrightKnee;
}

/// The highest value of a palette channel.
constexpr double kChannelTop = static_cast<double>(kChannelValues - 1);
/// The original keeps 10 bits of light and draws the upper 8.
constexpr unsigned kDroppedLightBits = 2;

} // namespace

auto textureGammaTable(const GammaSettings& settings) -> ChannelTable {
    const Bounded values = bounded(settings);
    const double exponent = values.textureGamma / values.gamma;
    ChannelTable table{};
    for (std::size_t index = 0; index < table.size(); ++index) {
        // Truncated, as the original converts.
        const double value =
            std::trunc(std::pow(static_cast<double>(index) / kChannelTop, exponent) * kChannelTop);
        table[index] = static_cast<std::uint8_t>(std::clamp(value, 0.0, kChannelTop));
    }
    return table;
}

auto lightGammaTable(const GammaSettings& settings) -> std::array<std::uint16_t, kLightTableSize> {
    const Bounded values = bounded(settings);
    const double knee = shadowKnee(values.brightness);
    constexpr double kTop = static_cast<double>(kLightTableSize - 1);
    std::array<std::uint16_t, kLightTableSize> table{};
    for (std::size_t index = 0; index < table.size(); ++index) {
        double light = std::pow(static_cast<double>(index) / kTop, values.lightGamma);
        if (values.brightness > 1.0) {
            light *= values.brightness;
        }
        light = light <= knee ? (light / knee) * kKneeLevel
                              : (((light - knee) / (1.0 - knee)) * (1.0 - kKneeLevel)) + kKneeLevel;
        // Truncated, as the original converts.
        const double level = std::clamp(std::trunc(std::pow(light, 1.0 / values.gamma) * kTop), 0.0, kTop);
        table[index] = static_cast<std::uint16_t>(level);
    }
    return table;
}

auto lightTable(const GammaSettings& settings) -> std::array<std::uint8_t, kLightTableSize> {
    std::array<std::uint8_t, kLightTableSize> table{};
    std::ranges::transform(lightGammaTable(settings), table.begin(), [](std::uint16_t level) {
        return narrow<std::uint8_t>(static_cast<unsigned>(level) >> kDroppedLightBits);
    });
    return table;
}

auto lightScale(const GammaSettings& settings) -> std::uint32_t {
    const Bounded values = bounded(settings);
    constexpr double kFixedPointOne = 256.0;
    constexpr double kHalf = 0.5; // rounded as the original rounds: half up, then cut
    return static_cast<std::uint32_t>((std::pow(2.0, 1.0 / values.lightGamma) * kFixedPointOne) + kHalf);
}

} // namespace nx::render
