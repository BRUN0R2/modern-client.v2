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

/// Component-wise sum.
[[nodiscard]] constexpr auto operator+(Vec3 a, Vec3 b) noexcept -> Vec3 {
    return Vec3{.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z};
}

/// Component-wise difference: the displacement from `b` to `a`.
[[nodiscard]] constexpr auto operator-(Vec3 a, Vec3 b) noexcept -> Vec3 {
    return Vec3{.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
}

/// Every component negated: the opposite direction.
[[nodiscard]] constexpr auto operator-(Vec3 v) noexcept -> Vec3 {
    return Vec3{.x = -v.x, .y = -v.y, .z = -v.z};
}

/// Every component multiplied by `scale`.
[[nodiscard]] constexpr auto operator*(Vec3 v, float scale) noexcept -> Vec3 {
    return Vec3{.x = v.x * scale, .y = v.y * scale, .z = v.z * scale};
}

/// Same as `v * scale`.
[[nodiscard]] constexpr auto operator*(float scale, Vec3 v) noexcept -> Vec3 {
    return v * scale;
}

/// `divisor` must not be zero.
[[nodiscard]] constexpr auto operator/(Vec3 v, float divisor) noexcept -> Vec3 {
    expects(divisor != 0.0F, "Vec3: division by zero");
    return Vec3{.x = v.x / divisor, .y = v.y / divisor, .z = v.z / divisor};
}

/// `a = a + b`.
constexpr auto operator+=(Vec3& a, Vec3 b) noexcept -> Vec3& {
    a = a + b;
    return a;
}

/// `a = a - b`.
constexpr auto operator-=(Vec3& a, Vec3 b) noexcept -> Vec3& {
    a = a - b;
    return a;
}

/// `v = v * scale`.
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

/// The square of length(v): enough to compare lengths, without a square root.
[[nodiscard]] constexpr auto lengthSquared(Vec3 v) noexcept -> float {
    return dot(v, v);
}

/// The Euclidean length of `v`.
[[nodiscard]] inline auto length(Vec3 v) noexcept -> float {
    return std::sqrt(lengthSquared(v));
}

/// The Euclidean distance between the points `a` and `b`.
[[nodiscard]] inline auto distance(Vec3 a, Vec3 b) noexcept -> float {
    return length(a - b);
}

/// Unit vector in the direction of `v`, which must be finite and not the zero vector (it has no
/// direction). Any such vector has one, however large or small its components: they are divided
/// by the largest first, so their squares neither overflow nor vanish.
[[nodiscard]] inline auto normalized(Vec3 v) noexcept -> Vec3 {
    const float largest = std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)});
    expects(largest > 0.0F, "normalized: the zero vector has no direction");
    if (!(largest > 0.0F)) {
        return Vec3{}; // the handler let execution continue ("observe")
    }
    const Vec3 scaled = v / largest;
    return scaled / length(scaled);
}

/// `a` at 0, `b` at 1: a + (b - a) * fraction.
[[nodiscard]] constexpr auto lerp(Vec3 a, Vec3 b, float fraction) noexcept -> Vec3 {
    return a + (b - a) * fraction;
}

/// The smaller of each pair of components.
[[nodiscard]] constexpr auto componentMin(Vec3 a, Vec3 b) noexcept -> Vec3 {
    return Vec3{.x = std::min(a.x, b.x), .y = std::min(a.y, b.y), .z = std::min(a.z, b.z)};
}

/// The larger of each pair of components.
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
