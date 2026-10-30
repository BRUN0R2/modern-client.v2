// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.render;

import std;
import nx.core;
import nx.math;

namespace nx::render {

namespace {

/// The gamma of the display the original's colors are made for (shaders/colorEncoding.slang).
constexpr float kDisplayGamma = 2.2F;

/// FogBlock::eye's w: how far a point is, along the view or from the eye.
constexpr float kAlongTheView = 0.0F;
constexpr float kFromTheEye = 1.0F;

[[nodiscard]] auto isShare(float value) noexcept -> bool {
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

/// How thick `fog` is `past` its start, from 0 to 1, as its curve thickens.
[[nodiscard]] auto thicknessOf(const Fog& fog, float past) noexcept -> float {
    switch (fog.curve) {
    case FogCurve::Linear:
        return past / (fog.end.value() - fog.start.value());
    case FogCurve::Exponential:
        return 1.0F - std::exp(-fog.density * past);
    case FogCurve::ExponentialSquared: {
        const float scaled = fog.density * past;
        return 1.0F - std::exp(-(scaled * scaled));
    }
    case FogCurve::Count:
        break;
    }
    return 0.0F;
}

/// `first` toward `second` by `share`.
[[nodiscard]] auto between(float first, float second, float share) noexcept -> float {
    return first + ((second - first) * share);
}

/// `fog` hiding nothing, as far as it reaches: what it comes in from and goes back out to.
[[nodiscard]] auto offFormOf(const Fog& fog) noexcept -> Fog {
    Fog off = fog;
    off.start = fogReachOf(fog);
    off.opacity = 0.0F;
    return off;
}

/// `from` toward `to` by `share`: their colors, densities, starts, opacities and finite ends between
/// them; the curve and an endless end `to`'s.
[[nodiscard]] auto fogBetween(const Fog& from, const Fog& to, float share) noexcept -> Fog {
    const bool areEndsFinite = std::isfinite(from.end.value()) && std::isfinite(to.end.value());
    return Fog{.color = FogColor{.red = between(from.color.red, to.color.red, share),
                                 .green = between(from.color.green, to.color.green, share),
                                 .blue = between(from.color.blue, to.color.blue, share)},
               .curve = to.curve,
               .density = between(from.density, to.density, share),
               .start = HammerUnits{between(from.start.value(), to.start.value(), share)},
               .end = areEndsFinite ? HammerUnits{between(from.end.value(), to.end.value(), share)} : to.end,
               .opacity = between(from.opacity, to.opacity, share)};
}

/// `share` of a transition's time eased in and out: slow first and last (smoothstep).
[[nodiscard]] auto easedOf(float share) noexcept -> float {
    return share * share * (3.0F - (2.0F * share));
}

} // namespace

auto isValid(const Fog& fog) noexcept -> bool {
    const bool isLinear = fog.curve == FogCurve::Linear;
    return isShare(fog.color.red) && isShare(fog.color.green) && isShare(fog.color.blue) &&
           std::to_underlying(fog.curve) < std::to_underlying(FogCurve::Count) &&
           std::isfinite(fog.density) && fog.density >= 0.0F && std::isfinite(fog.start.value()) &&
           fog.start >= HammerUnits{} && fog.end > fog.start &&
           (std::isfinite(fog.end.value()) || !isLinear) && isShare(fog.opacity);
}

auto fogAmountAt(const Fog& fog, HammerUnits distance) noexcept -> float {
    const float past = std::max(std::min(distance.value(), fog.end.value()) - fog.start.value(), 0.0F);
    return std::clamp(thicknessOf(fog, past), 0.0F, 1.0F) * fog.opacity;
}

auto fogReachOf(const Fog& fog) noexcept -> HammerUnits {
    // Where the curve is kFogReachThickness thick: past e^-x = 1 - it, x the density times the distance
    // past the start, squared for GL_EXP2.
    const float exponent = -std::log(1.0F - kFogReachThickness);
    float past = fog.end.value() - fog.start.value();
    if (fog.curve == FogCurve::Exponential || fog.curve == FogCurve::ExponentialSquared) {
        if (fog.density <= 0.0F) {
            return fog.start;
        }
        const float reach =
            (fog.curve == FogCurve::Exponential ? exponent : std::sqrt(exponent)) / fog.density;
        past = std::min(past, reach);
    }
    return fog.start + HammerUnits{past};
}

auto fogBlockOf(const std::optional<Fog>& fog, const Camera& camera, ColorEncoding encoding) noexcept
    -> FogBlock {
    if (!fog || fog->opacity <= 0.0F) {
        return {};
    }
    expects(isValid(*fog), "fogBlockOf: a valid fog");
    const bool isLinear = encoding == ColorEncoding::Linear;
    const auto encoded = [isLinear](float channel) {
        return isLinear ? std::pow(channel, kDisplayGamma) : channel;
    };
    const Vec3 forward = angleVectors(camera.angles).forward;
    return FogBlock{.color = Vec4{.x = encoded(fog->color.red),
                                  .y = encoded(fog->color.green),
                                  .z = encoded(fog->color.blue),
                                  .w = fog->opacity},
                    .shape = Vec4{.x = static_cast<float>(std::to_underlying(fog->curve)),
                                  .y = fog->density,
                                  .z = fog->start.value(),
                                  .w = fog->end.value()},
                    .eye = Vec4{.x = camera.position.x,
                                .y = camera.position.y,
                                .z = camera.position.z,
                                .w = isLinear ? kFromTheEye : kAlongTheView},
                    .forward = Vec4{.x = forward.x, .y = forward.y, .z = forward.z}};
}

void FogTransition::show(std::optional<Fog> fog, Seconds duration) noexcept {
    expects(!fog || isValid(*fog), "FogTransition::show: a valid fog or none");
    expects(duration >= Seconds{}, "FogTransition::show: a duration of none or more");
    const std::optional<Fog> now = shown();
    m_target = fog;
    m_duration = std::max(duration, Seconds{});
    m_elapsed = Seconds{};
    if (fog) {
        m_from = now.value_or(offFormOf(*fog));
        m_to = *fog;
    } else if (now) {
        m_from = *now;
        m_to = offFormOf(*now);
    } else {
        m_from.reset();
        m_to.reset();
    }
}

void FogTransition::advance(Seconds elapsed) noexcept {
    expects(elapsed >= Seconds{}, "FogTransition::advance: an elapsed time of none or more");
    m_elapsed = std::min(m_elapsed + std::max(elapsed, Seconds{}), m_duration);
}

auto FogTransition::shown() const noexcept -> std::optional<Fog> {
    if (!m_from || !m_to) {
        return std::nullopt;
    }
    const float share = m_duration > Seconds{} ? std::clamp(m_elapsed / m_duration, 0.0F, 1.0F) : 1.0F;
    if (!m_target && share >= 1.0F) {
        return std::nullopt; // gone back out
    }
    return fogBetween(*m_from, *m_to, easedOf(share));
}

} // namespace nx::render
