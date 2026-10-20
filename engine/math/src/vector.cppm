// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Vectors of three and four floats.
///
/// The operators are namespace-scope functions rather than hidden friends, and the comparisons are
/// defaulted members: GCC 16 crashes on defaulted friend comparisons of classes declared in modules
/// (docs/17-build-and-tools.md, section 2.2).
export module nx.math:vector;

import std;
import nx.core;

export namespace nx {

/// Point or direction in GoldSrc world space, in hammer units: +X faces yaw 0, +Y is to its left
/// (yaw 90) and +Z is up.
struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    [[nodiscard]] constexpr auto operator==(const Vec3&) const noexcept -> bool = default;
};

/// Homogeneous coordinates and matrix rows.
struct Vec4 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;

    [[nodiscard]] constexpr auto operator==(const Vec4&) const noexcept -> bool = default;
};

[[nodiscard]] constexpr auto operator+(Vec3 a, Vec3 b) noexcept -> Vec3 {
    return Vec3{.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z};
}

[[nodiscard]] constexpr auto operator-(Vec3 a, Vec3 b) noexcept -> Vec3 {
    return Vec3{.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
}

[[nodiscard]] constexpr auto operator-(Vec3 v) noexcept -> Vec3 {
    return Vec3{.x = -v.x, .y = -v.y, .z = -v.z};
}

[[nodiscard]] constexpr auto operator*(Vec3 v, float scale) noexcept -> Vec3 {
    return Vec3{.x = v.x * scale, .y = v.y * scale, .z = v.z * scale};
}

[[nodiscard]] constexpr auto operator*(float scale, Vec3 v) noexcept -> Vec3 {
    return v * scale;
}

/// `divisor` must not be zero.
[[nodiscard]] constexpr auto operator/(Vec3 v, float divisor) noexcept -> Vec3 {
    expects(divisor != 0.0F, "Vec3: division by zero");
    return Vec3{.x = v.x / divisor, .y = v.y / divisor, .z = v.z / divisor};
}

constexpr auto operator+=(Vec3& a, Vec3 b) noexcept -> Vec3& {
    a = a + b;
    return a;
}

constexpr auto operator-=(Vec3& a, Vec3 b) noexcept -> Vec3& {
    a = a - b;
    return a;
}

constexpr auto operator*=(Vec3& v, float scale) noexcept -> Vec3& {
    v = v * scale;
    return v;
}

/// Evaluated left to right: (x * x' + y * y') + z * z'.
[[nodiscard]] constexpr auto dot(Vec3 a, Vec3 b) noexcept -> float {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/// Right-handed: cross(+X, +Y) == +Z.
[[nodiscard]] constexpr auto cross(Vec3 a, Vec3 b) noexcept -> Vec3 {
    return Vec3{.x = a.y * b.z - a.z * b.y, .y = a.z * b.x - a.x * b.z, .z = a.x * b.y - a.y * b.x};
}

[[nodiscard]] constexpr auto lengthSquared(Vec3 v) noexcept -> float {
    return dot(v, v);
}

[[nodiscard]] inline auto length(Vec3 v) noexcept -> float {
    return std::sqrt(lengthSquared(v));
}

[[nodiscard]] inline auto distance(Vec3 a, Vec3 b) noexcept -> float {
    return length(a - b);
}

/// Unit vector in the direction of `v`, which must not be the zero vector: it has no direction.
[[nodiscard]] inline auto normalized(Vec3 v) noexcept -> Vec3 {
    const float magnitude = length(v);
    expects(magnitude > 0.0F, "normalized: the zero vector has no direction");
    if (magnitude == 0.0F) {
        return Vec3{}; // the handler let execution continue ("observe")
    }
    return v / magnitude;
}

/// `a` at 0, `b` at 1: a + (b - a) * fraction.
[[nodiscard]] constexpr auto lerp(Vec3 a, Vec3 b, float fraction) noexcept -> Vec3 {
    return a + (b - a) * fraction;
}

[[nodiscard]] constexpr auto componentMin(Vec3 a, Vec3 b) noexcept -> Vec3 {
    return Vec3{.x = std::min(a.x, b.x), .y = std::min(a.y, b.y), .z = std::min(a.z, b.z)};
}

[[nodiscard]] constexpr auto componentMax(Vec3 a, Vec3 b) noexcept -> Vec3 {
    return Vec3{.x = std::max(a.x, b.x), .y = std::max(a.y, b.y), .z = std::max(a.z, b.z)};
}

/// Evaluated left to right, like dot(Vec3, Vec3).
[[nodiscard]] constexpr auto dot(Vec4 a, Vec4 b) noexcept -> float {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

/// The first three components.
[[nodiscard]] constexpr auto xyz(Vec4 v) noexcept -> Vec3 {
    return Vec3{.x = v.x, .y = v.y, .z = v.z};
}

/// `v` with a fourth component: 1 for a point, 0 for a direction.
[[nodiscard]] constexpr auto extend(Vec3 v, float w) noexcept -> Vec4 {
    return Vec4{.x = v.x, .y = v.y, .z = v.z, .w = w};
}

} // namespace nx

/// Formats as "(x y z)" with the float format spec applied to each component:
/// std::format("{:.1f}", Vec3{1, 2, 3}) == "(1.0 2.0 3.0)".
template <>
struct std::formatter<nx::Vec3> : std::formatter<float> {
    template <class FormatContext>
    auto format(nx::Vec3 v, FormatContext& context) const {
        auto out = context.out();
        bool first = true;
        for (const float component : {v.x, v.y, v.z}) {
            *out++ = first ? '(' : ' ';
            first = false;
            context.advance_to(out);
            out = std::formatter<float>::format(component, context);
        }
        *out++ = ')';
        return out;
    }
};
