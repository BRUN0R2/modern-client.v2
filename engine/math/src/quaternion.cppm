// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Rotation quaternions, for interpolating orientations (bone animation, docs/05-rendering.md,
/// section 9).
export module nx.math:quaternion;

import std;
import nx.core;
import :vector;
import :matrix;
import :angles;

export namespace nx {

/// x, y and z are the vector part and w the scalar part. Rotations are unit quaternions; q and -q
/// are the same rotation. A default-constructed quaternion is the identity.
struct Quat {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;

    /// Rotation of `angle` about `axis`, which must have unit length.
    [[nodiscard]] static auto fromAxisAngle(Vec3 axis, Radians angle) noexcept -> Quat;

    /// The rotation of rotationMatrix(angles).
    [[nodiscard]] static auto fromEulerAngles(EulerAngles angles) noexcept -> Quat;

    [[nodiscard]] constexpr auto operator==(const Quat&) const noexcept -> bool = default;
};

[[nodiscard]] constexpr auto operator-(Quat q) noexcept -> Quat {
    return Quat{.x = -q.x, .y = -q.y, .z = -q.z, .w = -q.w};
}

/// Composition: `b` first, then `a`.
[[nodiscard]] constexpr auto operator*(Quat a, Quat b) noexcept -> Quat {
    return Quat{.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                .y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                .z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                .w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

/// The inverse rotation of a unit quaternion.
[[nodiscard]] constexpr auto conjugate(Quat q) noexcept -> Quat {
    return Quat{.x = -q.x, .y = -q.y, .z = -q.z, .w = q.w};
}

[[nodiscard]] constexpr auto dot(Quat a, Quat b) noexcept -> float {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

/// `q` scaled to unit length; `q` must not be zero.
[[nodiscard]] inline auto normalized(Quat q) noexcept -> Quat {
    const float magnitude = std::sqrt(dot(q, q));
    expects(magnitude > 0.0F, "normalized: the zero quaternion is not a rotation");
    if (magnitude == 0.0F) {
        return Quat{}; // the handler let execution continue ("observe")
    }
    return Quat{.x = q.x / magnitude, .y = q.y / magnitude, .z = q.z / magnitude, .w = q.w / magnitude};
}

/// `v` rotated by the unit quaternion `q`.
[[nodiscard]] constexpr auto rotate(Quat q, Vec3 v) noexcept -> Vec3 {
    // q v q* expanded: v + w t + u x t, with u the vector part and t = 2 (u x v).
    const Vec3 vectorPart{.x = q.x, .y = q.y, .z = q.z};
    const Vec3 crossProduct = cross(vectorPart, v);
    const Vec3 twiceCross = crossProduct + crossProduct;
    return v + twiceCross * q.w + cross(vectorPart, twiceCross);
}

/// The rotation matrix of the unit quaternion `q`.
[[nodiscard]] auto rotationMatrix(Quat q) noexcept -> Mat3;

/// Spherical interpolation along the shorter arc: `a` at 0, `b` at 1, constant angular speed.
/// `fraction` must be in [0, 1]; `a` and `b` must be unit quaternions. The result has unit length.
[[nodiscard]] auto slerp(Quat a, Quat b, float fraction) noexcept -> Quat;

} // namespace nx
