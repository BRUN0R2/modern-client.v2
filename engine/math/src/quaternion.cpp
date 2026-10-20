// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.math;

import std;
import nx.core;

namespace nx {

namespace {

/// A quaternion holds the sine and cosine of half its rotation angle.
constexpr float kHalfAngle = 0.5F;

/// Above this cosine between the endpoints, sin(angle) is too small to divide by precisely; the
/// normalized linear interpolation is then indistinguishable from the spherical one.
constexpr float kNearlyParallelCosine = 0.9995F;

/// How far a unit axis may be from length 1 (squared length, float rounding of normalized()).
constexpr float kUnitLengthTolerance = 1e-4F;

} // namespace

auto Quat::fromAxisAngle(Vec3 axis, Radians angle) noexcept -> Quat {
    expects(std::abs(lengthSquared(axis) - 1.0F) <= kUnitLengthTolerance,
            "Quat::fromAxisAngle: the axis must have unit length");
    const double halfAngle = static_cast<double>(angle.value()) * static_cast<double>(kHalfAngle);
    const auto sine = static_cast<float>(std::sin(halfAngle));
    return Quat{.x = axis.x * sine,
                .y = axis.y * sine,
                .z = axis.z * sine,
                .w = static_cast<float>(std::cos(halfAngle))};
}

auto Quat::fromEulerAngles(EulerAngles angles) noexcept -> Quat {
    // Rz(yaw) * Ry(pitch) * Rx(roll), as rotationMatrix(EulerAngles), from the half angles.
    const auto [sinPitch, cosPitch] = detail::sineCosine(angles.pitch * kHalfAngle);
    const auto [sinYaw, cosYaw] = detail::sineCosine(angles.yaw * kHalfAngle);
    const auto [sinRoll, cosRoll] = detail::sineCosine(angles.roll * kHalfAngle);
    return Quat{.x = sinRoll * cosPitch * cosYaw - cosRoll * sinPitch * sinYaw,
                .y = cosRoll * sinPitch * cosYaw + sinRoll * cosPitch * sinYaw,
                .z = cosRoll * cosPitch * sinYaw - sinRoll * sinPitch * cosYaw,
                .w = cosRoll * cosPitch * cosYaw + sinRoll * sinPitch * sinYaw};
}

auto rotationMatrix(Quat q) noexcept -> Mat3 {
    // The usual expansion of q v q*, with the factors of two folded into x2, y2 and z2.
    const float x2 = q.x + q.x;
    const float y2 = q.y + q.y;
    const float z2 = q.z + q.z;
    const float xx = q.x * x2;
    const float xy = q.x * y2;
    const float xz = q.x * z2;
    const float yy = q.y * y2;
    const float yz = q.y * z2;
    const float zz = q.z * z2;
    const float wx = q.w * x2;
    const float wy = q.w * y2;
    const float wz = q.w * z2;
    return Mat3{.rows = {
                    Vec3{.x = 1.0F - (yy + zz), .y = xy - wz, .z = xz + wy},
                    Vec3{.x = xy + wz, .y = 1.0F - (xx + zz), .z = yz - wx},
                    Vec3{.x = xz - wy, .y = yz + wx, .z = 1.0F - (xx + yy)},
                }};
}

auto slerp(Quat a, Quat b, float fraction) noexcept -> Quat {
    expects(fraction >= 0.0F && fraction <= 1.0F, "slerp: the fraction must be in [0, 1]");
    float cosine = dot(a, b);
    if (cosine < 0.0F) {
        // -b is the same rotation as b, on the shorter arc from a.
        b = -b;
        cosine = -cosine;
    }

    float weightA = 1.0F - fraction;
    float weightB = fraction;
    if (cosine <= kNearlyParallelCosine) {
        const double angle = std::acos(static_cast<double>(cosine));
        const double sine = std::sin(angle);
        weightA = static_cast<float>(std::sin(static_cast<double>(weightA) * angle) / sine);
        weightB = static_cast<float>(std::sin(static_cast<double>(weightB) * angle) / sine);
    }
    return normalized(Quat{.x = a.x * weightA + b.x * weightB,
                           .y = a.y * weightA + b.y * weightB,
                           .z = a.z * weightA + b.z * weightB,
                           .w = a.w * weightA + b.w * weightB});
}

} // namespace nx
