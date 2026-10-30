// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.console;
import nx.render;

namespace cs {

namespace {

constexpr nx::log::Category kLog{"client"};

/// A color channel from 0 to 255, as a share of all of it.
constexpr float kChannelTop = 255.0F;

/// The fog the cvars start with: a moderate fog of the original's maps, black, thickening without end.
constexpr std::string_view kDefaultColor = "0 0 0";
constexpr std::string_view kDefaultCurve = "exp2";
constexpr float kDefaultDensity = 0.002F;
constexpr float kDefaultFadeSeconds = 2.0F;

[[nodiscard]] auto isFogColor(std::string_view text) noexcept -> bool {
    return crosshairColorOf(text).has_value(); // the console's colors are written as the crosshair's
}

[[nodiscard]] auto isFogCurve(std::string_view text) noexcept -> bool {
    return fogCurveNamed(text).has_value();
}

} // namespace

auto fogCurveNamed(std::string_view text) noexcept -> std::optional<nx::render::FogCurve> {
    using nx::render::FogCurve;
    if (text == "linear") {
        return FogCurve::Linear;
    }
    if (text == "exp") {
        return FogCurve::Exponential;
    }
    if (text == "exp2") {
        return FogCurve::ExponentialSquared;
    }
    return std::nullopt;
}

auto FogCvars::registerIn(nx::console::Console& console) -> nx::Result<FogCvars, nx::console::CvarError> {
    using nx::console::CvarDesc;
    auto enabled = console.registerCvar(
        CvarDesc<bool>{.name = "fog_enable", .defaultValue = false, .description = "Whether the fog shows."});
    auto color = console.registerCvar(CvarDesc<std::string>{
        .name = "fog_color",
        .defaultValue = std::string{kDefaultColor},
        .description = "The fog's color: red, green and blue, from 0 to 255.",
        .rule = {.expectation = "three whole numbers from 0 to 255", .accepts = isFogColor}});
    auto curve = console.registerCvar(CvarDesc<std::string>{
        .name = "fog_curve",
        .defaultValue = std::string{kDefaultCurve},
        .description =
            "How the fog thickens: linear from fog_start to fog_end, or exp or exp2 by fog_density.",
        .rule = {.expectation = "linear, exp or exp2", .accepts = isFogCurve}});
    auto density =
        console.registerCvar(CvarDesc<float>{.name = "fog_density",
                                             .defaultValue = kDefaultDensity,
                                             .description = "How fast exp and exp2 fog thickens, per unit.",
                                             .minimum = 0.0F});
    auto start = console.registerCvar(CvarDesc<float>{.name = "fog_start",
                                                      .defaultValue = 0.0F,
                                                      .description = "The nearest the fog hides anything at.",
                                                      .minimum = 0.0F});
    auto end = console.registerCvar(
        CvarDesc<float>{.name = "fog_end",
                        .defaultValue = 0.0F,
                        .description = "The farthest the fog thickens to, past fog_start; 0, without end.",
                        .minimum = 0.0F});
    auto opacity = console.registerCvar(CvarDesc<float>{.name = "fog_opacity",
                                                        .defaultValue = 1.0F,
                                                        .description = "The most of a color the fog hides.",
                                                        .minimum = 0.0F,
                                                        .maximum = 1.0F});
    auto fade = console.registerCvar(
        CvarDesc<float>{.name = "fog_fade",
                        .defaultValue = kDefaultFadeSeconds,
                        .description = "The seconds the fog takes to come in, go out or change.",
                        .minimum = 0.0F});
    if (!enabled) {
        return std::unexpected{std::move(enabled.error())};
    }
    for (auto* const registered : {&color, &curve}) {
        if (!*registered) {
            return std::unexpected{std::move(registered->error())};
        }
    }
    for (auto* const registered : {&density, &start, &end, &opacity, &fade}) {
        if (!*registered) {
            return std::unexpected{std::move(registered->error())};
        }
    }
    return FogCvars{.enabled = *enabled,
                    .color = *color,
                    .curve = *curve,
                    .density = *density,
                    .start = *start,
                    .end = *end,
                    .opacity = *opacity,
                    .fade = *fade};
}

auto FogCvars::fog() const -> std::expected<std::optional<nx::render::Fog>, std::string_view> {
    if (!enabled.value()) {
        return std::optional<nx::render::Fog>{};
    }
    const std::optional<nx::render::ScreenColor> rgb = crosshairColorOf(color.value());
    const std::optional<nx::render::FogCurve> shape = fogCurveNamed(curve.value());
    nx::invariant(rgb.has_value() && shape.has_value(), "FogCvars: their rules keep their values valid");
    if (!rgb || !shape) {
        return std::unexpected{"fog_color or fog_curve"};
    }
    const nx::render::Fog made{
        .color = nx::render::FogColor{.red = static_cast<float>(rgb->red) / kChannelTop,
                                      .green = static_cast<float>(rgb->green) / kChannelTop,
                                      .blue = static_cast<float>(rgb->blue) / kChannelTop},
        .curve = *shape,
        .density = density.value(),
        .start = nx::HammerUnits{start.value()},
        .end = end.value() > 0.0F ? nx::HammerUnits{end.value()} : nx::render::kEndlessFog,
        .opacity = opacity.value()};
    if (!nx::render::isValid(made)) {
        return std::unexpected{"fog_end must be past fog_start, and a linear fog needs one"};
    }
    return std::optional{made};
}

auto FogCvars::fadeTime() const -> nx::Seconds {
    return nx::Seconds{fade.value()};
}

void FogControl::update(const FogCvars& cvars, nx::Seconds elapsed) {
    const auto asked = cvars.fog();
    if (!asked) {
        if (m_refused != asked.error()) {
            nx::log::warning(kLog, "the fog stays as it is: {}", asked.error());
            m_refused = asked.error();
        }
    } else {
        m_refused.reset();
        if (*asked != m_asked) {
            m_transition.show(*asked, cvars.fadeTime());
            m_asked = *asked;
        }
    }
    m_transition.advance(elapsed);
}

} // namespace cs
