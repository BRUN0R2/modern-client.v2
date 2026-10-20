// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Planes in the BSP form: unit normal and distance from the origin.
export module nx.math:plane;

import std;
import nx.core;
import :vector;
import :bounds;

export namespace nx {

/// Where a box lies relative to a plane.
enum class PlaneSide : std::uint8_t {
    Front,    ///< Every point at a signed distance of 0 or more.
    Back,     ///< Every point at a negative signed distance.
    Spanning, ///< Points on both sides.
};

/// The points p with dot(normal, p) == distance. `normal` has unit length and points to the front.
struct Plane {
    Vec3 normal{};
    float distance = 0.0F;

    /// The plane through `point` whose front is towards `facing` (unit length).
    [[nodiscard]] static constexpr auto through(Vec3 point, Vec3 facing) noexcept -> Plane {
        return Plane{.normal = facing, .distance = dot(facing, point)};
    }

    /// Positive in front, negative behind, 0 on the plane.
    [[nodiscard]] constexpr auto signedDistance(Vec3 point) const noexcept -> float {
        return dot(normal, point) - distance;
    }

    /// Classifies `box` (valid) by its two corners farthest along and against the normal. A box
    /// that touches the plane from the front is Front, as in the original BSP code: the same
    /// rule as signedDistance(point) >= 0.
    [[nodiscard]] constexpr auto sideOf(const Aabb& box) const noexcept -> PlaneSide {
        const Vec3 farthest{.x = normal.x >= 0.0F ? box.max.x : box.min.x,
                            .y = normal.y >= 0.0F ? box.max.y : box.min.y,
                            .z = normal.z >= 0.0F ? box.max.z : box.min.z};
        const Vec3 nearest{.x = normal.x >= 0.0F ? box.min.x : box.max.x,
                           .y = normal.y >= 0.0F ? box.min.y : box.max.y,
                           .z = normal.z >= 0.0F ? box.min.z : box.max.z};
        if (signedDistance(nearest) >= 0.0F) {
            return PlaneSide::Front;
        }
        if (signedDistance(farthest) < 0.0F) {
            return PlaneSide::Back;
        }
        return PlaneSide::Spanning;
    }

    [[nodiscard]] constexpr auto operator==(const Plane&) const noexcept -> bool = default;
};

} // namespace nx
