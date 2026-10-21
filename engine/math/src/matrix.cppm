// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Matrices, stored as rows and applied to column vectors (v' = M v), the layout of GoldSrc's bone
/// transforms. A default-constructed matrix is the identity.
///
/// View and projection matrices come with the renderer (nx.render, M2): their clip-space and depth
/// conventions are decisions of docs/05-rendering.md, not of this module.
export module nx.math:matrix;

import std;
import :vector;

export namespace nx {

/// Linear map of 3D space (rotation, scale).
struct Mat3 {
    static constexpr std::size_t kRowCount = 3;

    std::array<Vec3, kRowCount> rows{Vec3{.x = 1.0F}, Vec3{.y = 1.0F}, Vec3{.z = 1.0F}};

    [[nodiscard]] constexpr auto operator==(const Mat3&) const noexcept -> bool = default;
};

/// Affine transform: a Mat3 in the first three columns and the translation in the last one. Rows of
/// Vec4, 48 bytes: the bone palette entry of docs/05-rendering.md, section 9.1.
struct Mat3x4 {
    static constexpr std::size_t kRowCount = 3;

    std::array<Vec4, kRowCount> rows{Vec4{.x = 1.0F}, Vec4{.y = 1.0F}, Vec4{.z = 1.0F}};

    [[nodiscard]] constexpr auto operator==(const Mat3x4&) const noexcept -> bool = default;
};

/// General 4x4 matrix (composition with projections).
struct Mat4 {
    static constexpr std::size_t kRowCount = 4;

    std::array<Vec4, kRowCount> rows{Vec4{.x = 1.0F}, Vec4{.y = 1.0F}, Vec4{.z = 1.0F}, Vec4{.w = 1.0F}};

    [[nodiscard]] constexpr auto operator==(const Mat4&) const noexcept -> bool = default;
};

static_assert(sizeof(Mat3x4) == Mat3x4::kRowCount * sizeof(Vec4),
              "Mat3x4 is a tightly packed bone palette entry");

/// Rows become columns. The rows of the result are the columns of `m`.
[[nodiscard]] constexpr auto transposed(const Mat3& m) noexcept -> Mat3 {
    const auto& [r0, r1, r2] = m.rows;
    return Mat3{.rows = {Vec3{.x = r0.x, .y = r1.x, .z = r2.x}, Vec3{.x = r0.y, .y = r1.y, .z = r2.y},
                         Vec3{.x = r0.z, .y = r1.z, .z = r2.z}}};
}

[[nodiscard]] constexpr auto operator*(const Mat3& m, Vec3 v) noexcept -> Vec3 {
    const auto& [r0, r1, r2] = m.rows;
    return Vec3{.x = dot(r0, v), .y = dot(r1, v), .z = dot(r2, v)};
}

/// `b` first, then `a`.
[[nodiscard]] constexpr auto operator*(const Mat3& a, const Mat3& b) noexcept -> Mat3 {
    const Mat3 columnsOfB = transposed(b);
    const auto& [r0, r1, r2] = a.rows;
    return Mat3{.rows = {columnsOfB * r0, columnsOfB * r1, columnsOfB * r2}};
}

[[nodiscard]] constexpr auto affine(const Mat3& linear, Vec3 translation) noexcept -> Mat3x4 {
    const auto& [r0, r1, r2] = linear.rows;
    return Mat3x4{.rows = {extend(r0, translation.x), extend(r1, translation.y), extend(r2, translation.z)}};
}

[[nodiscard]] constexpr auto linearPart(const Mat3x4& m) noexcept -> Mat3 {
    const auto& [r0, r1, r2] = m.rows;
    return Mat3{.rows = {xyz(r0), xyz(r1), xyz(r2)}};
}

[[nodiscard]] constexpr auto translationPart(const Mat3x4& m) noexcept -> Vec3 {
    const auto& [r0, r1, r2] = m.rows;
    return Vec3{.x = r0.w, .y = r1.w, .z = r2.w};
}

/// Rotates, scales and translates `point`.
[[nodiscard]] constexpr auto transformPoint(const Mat3x4& m, Vec3 point) noexcept -> Vec3 {
    const Vec4 homogeneous = extend(point, 1.0F);
    const auto& [r0, r1, r2] = m.rows;
    return Vec3{.x = dot(r0, homogeneous), .y = dot(r1, homogeneous), .z = dot(r2, homogeneous)};
}

/// Rotates and scales `direction`; translation does not apply to directions.
[[nodiscard]] constexpr auto transformDirection(const Mat3x4& m, Vec3 direction) noexcept -> Vec3 {
    return linearPart(m) * direction;
}

/// `b` first, then `a`, as if both had the implicit last row (0 0 0 1).
[[nodiscard]] constexpr auto operator*(const Mat3x4& a, const Mat3x4& b) noexcept -> Mat3x4 {
    return affine(linearPart(a) * linearPart(b), transformPoint(a, translationPart(b)));
}

/// Inverse of a rigid transform (rotation and translation only), whose linear part is orthonormal:
/// the transposed rotation, applied to the negated translation. Other transforms need a general
/// inverse, which no caller needs yet.
[[nodiscard]] constexpr auto rigidInverse(const Mat3x4& m) noexcept -> Mat3x4 {
    const Mat3 inverseRotation = transposed(linearPart(m));
    return affine(inverseRotation, inverseRotation * -translationPart(m));
}

/// The 4x4 form of an affine transform: last row (0 0 0 1).
[[nodiscard]] constexpr auto toMat4(const Mat3x4& m) noexcept -> Mat4 {
    const auto& [r0, r1, r2] = m.rows;
    return Mat4{.rows = {r0, r1, r2, Vec4{.w = 1.0F}}};
}

/// Rows become columns.
[[nodiscard]] constexpr auto transposed(const Mat4& m) noexcept -> Mat4 {
    const auto& [r0, r1, r2, r3] = m.rows;
    return Mat4{.rows = {Vec4{.x = r0.x, .y = r1.x, .z = r2.x, .w = r3.x},
                         Vec4{.x = r0.y, .y = r1.y, .z = r2.y, .w = r3.y},
                         Vec4{.x = r0.z, .y = r1.z, .z = r2.z, .w = r3.z},
                         Vec4{.x = r0.w, .y = r1.w, .z = r2.w, .w = r3.w}}};
}

[[nodiscard]] constexpr auto operator*(const Mat4& m, Vec4 v) noexcept -> Vec4 {
    const auto& [r0, r1, r2, r3] = m.rows;
    return Vec4{.x = dot(r0, v), .y = dot(r1, v), .z = dot(r2, v), .w = dot(r3, v)};
}

/// `b` first, then `a`.
[[nodiscard]] constexpr auto operator*(const Mat4& a, const Mat4& b) noexcept -> Mat4 {
    const Mat4 columnsOfB = transposed(b);
    const auto& [r0, r1, r2, r3] = a.rows;
    return Mat4{.rows = {columnsOfB * r0, columnsOfB * r1, columnsOfB * r2, columnsOfB * r3}};
}

} // namespace nx
