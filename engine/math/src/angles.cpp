// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.math;

import std;
import nx.core;

namespace nx {

namespace {

constexpr Degrees kFullTurnDegrees = kHalfTurnDegrees + kHalfTurnDegrees;
constexpr double kRadiansPerDegree = std::numbers::pi / static_cast<double>(kHalfTurnDegrees.value());
constexpr double kDegreesPerRadian = static_cast<double>(kHalfTurnDegrees.value()) / std::numbers::pi;

[[nodiscard]] auto degreesFromRadians(double radians) noexcept -> Degrees {
    return Degrees{static_cast<float>(radians * kDegreesPerRadian)};
}

} // namespace

auto detail::sineCosine(Degrees angle) noexcept -> SineCosine {
    const double radians = static_cast<double>(angle.value()) * kRadiansPerDegree;
    return SineCosine{.sine = static_cast<float>(std::sin(radians)),
                      .cosine = static_cast<float>(std::cos(radians))};
}

auto detail::sineCosine(Radians angle) noexcept -> SineCosine {
    const auto radians = static_cast<double>(angle.value());
    return SineCosine{.sine = static_cast<float>(std::sin(radians)),
                      .cosine = static_cast<float>(std::cos(radians))};
}

auto rotationMatrix(EulerAngles angles) noexcept -> Mat3 {
    const auto [sinPitch, cosPitch] = detail::sineCosine(angles.pitch);
    const auto [sinYaw, cosYaw] = detail::sineCosine(angles.yaw);
    const auto [sinRoll, cosRoll] = detail::sineCosine(angles.roll);
    return Mat3{.rows = {
                    Vec3{.x = cosPitch * cosYaw,
                         .y = sinPitch * sinRoll * cosYaw - cosRoll * sinYaw,
                         .z = sinPitch * cosRoll * cosYaw + sinRoll * sinYaw},
                    Vec3{.x = cosPitch * sinYaw,
                         .y = sinPitch * sinRoll * sinYaw + cosRoll * cosYaw,
                         .z = sinPitch * cosRoll * sinYaw - sinRoll * cosYaw},
                    Vec3{.x = -sinPitch, .y = cosPitch * sinRoll, .z = cosPitch * cosRoll},
                }};
}

auto angleVectors(EulerAngles angles) noexcept -> AngleVectors {
    const auto [forward, left, up] = transposed(rotationMatrix(angles)).rows;
    return AngleVectors{.forward = forward, .right = -left, .up = up};
}

auto intoAxes(const AngleVectors& axes, Vec3 point) noexcept -> Vec3 {
    return Vec3{.x = dot(axes.forward, point), .y = -dot(axes.right, point), .z = dot(axes.up, point)};
}

auto outOfAxes(const AngleVectors& axes, Vec3 direction) noexcept -> Vec3 {
    return Vec3{.x = dot(direction, Vec3{.x = axes.forward.x, .y = -axes.right.x, .z = axes.up.x}),
                .y = dot(direction, Vec3{.x = axes.forward.y, .y = -axes.right.y, .z = axes.up.y}),
                .z = dot(direction, Vec3{.x = axes.forward.z, .y = -axes.right.z, .z = axes.up.z})};
}

auto vectorAngles(Vec3 direction) noexcept -> EulerAngles {
    expects(direction != Vec3{}, "vectorAngles: the zero vector has no direction");
    const auto x = static_cast<double>(direction.x);
    const auto y = static_cast<double>(direction.y);
    const auto z = static_cast<double>(direction.z);
    const double horizontal = std::sqrt(x * x + y * y);
    // atan2(0, -0) is a half turn: straight up or down gets yaw 0 whatever the signs of zero.
    const double yaw = horizontal == 0.0 ? 0.0 : std::atan2(y, x);
    return EulerAngles{
        .pitch = degreesFromRadians(-std::atan2(z, horizontal)), .yaw = degreesFromRadians(yaw), .roll = {}};
}

auto normalizeAngle(Degrees angle) noexcept -> Degrees {
    expects(std::isfinite(angle.value()), "normalizeAngle: the angle must be finite");
    // fmod is exact and keeps the sign of the angle: the remainder is in (-360, 360).
    float wrapped = std::fmod(angle.value(), kFullTurnDegrees.value());
    if (wrapped > kHalfTurnDegrees.value()) {
        wrapped -= kFullTurnDegrees.value();
    } else if (wrapped <= -kHalfTurnDegrees.value()) {
        wrapped += kFullTurnDegrees.value();
    }
    return Degrees{wrapped};
}

} // namespace nx
