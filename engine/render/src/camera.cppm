// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The camera and the matrices it makes (docs/05-rendering.md, section 3.2): view space has x to
/// the right, y up and z forward; the projection writes reversed depth, 1 at the near plane and 0 at
/// infinity, which a 32-bit float stores with nearly uniform precision over the whole map. Depth
/// tests compare with CompareOp::Greater and clear to 0.
export module nx.render:camera;

import std;
import nx.core;
import nx.math;

export namespace nx::render {

/// The original's default `fov`.
inline constexpr Degrees kDefaultFieldOfView{90.0F};
/// The nearest a camera sees, in map units: as close as a player's view comes to a wall.
inline constexpr float kNearPlane = 4.0F;

struct Camera {
    Vec3 position{};
    EulerAngles angles{};
    /// `fov` as the original reads it: the horizontal angle of a 4:3 view. Wider screens see more
    /// to the sides at the same vertical angle ("Hor+"), as CS 1.6 does.
    Degrees fieldOfView = kDefaultFieldOfView;
};

/// Map space to view space: x to the camera's right, y its up, z its forward.
[[nodiscard]] auto viewMatrix(const Camera& camera) noexcept -> Mat4;

/// View space to clip space for a target `aspect` (width over height) wide, with reversed infinite
/// depth from kNearPlane.
[[nodiscard]] auto projectionMatrix(Degrees fieldOfView, float aspect) noexcept -> Mat4;

/// Map space to clip space.
[[nodiscard]] auto viewProjectionMatrix(const Camera& camera, float aspect) noexcept -> Mat4;

/// The left, right, bottom and top planes of what `camera` sees on a target of `aspect`, their fronts
/// inward: what lies wholly behind one of them is out of view. The near plane, a few units from the
/// eye, and the far one, at infinity, cull nothing worth a test.
[[nodiscard]] auto frustumPlanes(const Camera& camera, float aspect) noexcept -> std::array<Plane, 4>;

} // namespace nx::render
