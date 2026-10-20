// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Axis-aligned bounding boxes: entity bounds (GoldSrc's mins and maxs), hulls and BSP node bounds.
export module nx.math:bounds;

import std;
import nx.core;
import :vector;

export namespace nx {

/// Closed box: points on a face are inside, and boxes that touch intersect. A box is valid when
/// `min` is not above `max` on any axis; a box with zero size on an axis (a face, an edge, a
/// point) is valid.
struct Aabb {
    Vec3 min{};
    Vec3 max{};

    /// The box of half size `halfExtents` (not negative) around `middle`.
    [[nodiscard]] static constexpr auto around(Vec3 middle, Vec3 halfExtents) noexcept -> Aabb {
        expects(halfExtents.x >= 0.0F && halfExtents.y >= 0.0F && halfExtents.z >= 0.0F,
                "Aabb::around: the half extents must not be negative");
        return Aabb{.min = middle - halfExtents, .max = middle + halfExtents};
    }

    [[nodiscard]] constexpr auto isValid() const noexcept -> bool {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }

    [[nodiscard]] constexpr auto contains(Vec3 point) const noexcept -> bool {
        return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }

    [[nodiscard]] constexpr auto intersects(const Aabb& other) const noexcept -> bool {
        return min.x <= other.max.x && max.x >= other.min.x && min.y <= other.max.y && max.y >= other.min.y &&
               min.z <= other.max.z && max.z >= other.min.z;
    }

    [[nodiscard]] constexpr auto center() const noexcept -> Vec3 { return lerp(min, max, kHalf); }

    [[nodiscard]] constexpr auto size() const noexcept -> Vec3 { return max - min; }

    /// The smallest box that contains both boxes.
    [[nodiscard]] constexpr auto merged(const Aabb& other) const noexcept -> Aabb {
        return Aabb{.min = componentMin(min, other.min), .max = componentMax(max, other.max)};
    }

    /// The smallest box that contains this one and `point`.
    [[nodiscard]] constexpr auto including(Vec3 point) const noexcept -> Aabb {
        return Aabb{.min = componentMin(min, point), .max = componentMax(max, point)};
    }

    [[nodiscard]] constexpr auto translated(Vec3 offset) const noexcept -> Aabb {
        return Aabb{.min = min + offset, .max = max + offset};
    }

    /// Grown by `margin` (not negative) on both sides of every axis: the Minkowski sum with a box of
    /// half size `margin`, which turns a box-against-box test into a point-against-box one.
    [[nodiscard]] constexpr auto expanded(Vec3 margin) const noexcept -> Aabb {
        expects(margin.x >= 0.0F && margin.y >= 0.0F && margin.z >= 0.0F,
                "Aabb::expanded: the margin must not be negative");
        return Aabb{.min = min - margin, .max = max + margin};
    }

    [[nodiscard]] constexpr auto operator==(const Aabb&) const noexcept -> bool = default;

private:
    static constexpr float kHalf = 0.5F;
};

} // namespace nx
