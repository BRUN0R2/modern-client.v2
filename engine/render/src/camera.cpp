// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.render;

import std;
import nx.core;
import nx.math;

namespace nx::render {

namespace {

/// The aspect ratio `fov` is defined at.
constexpr float kFieldOfViewAspect = 4.0F / 3.0F;

/// How far the view reaches to the sides and up at a distance of 1.
struct HalfExtents {
    float width = 0.0F;
    float height = 0.0F;
};

[[nodiscard]] auto halfExtentsOf(Degrees fieldOfView, float aspect) noexcept -> HalfExtents {
    expects(aspect > 0.0F && fieldOfView.value() > 0.0F && fieldOfView.value() < 180.0F,
            "the camera: a positive aspect and a field of view below 180 degrees");
    // The vertical angle of a 4:3 view with that horizontal angle, kept at every aspect.
    const float height = std::tan(toRadians(fieldOfView).value() * 0.5F) / kFieldOfViewAspect;
    return {.width = height * aspect, .height = height};
}

} // namespace

auto viewMatrix(const Camera& camera) noexcept -> Mat4 {
    const AngleVectors axes = angleVectors(camera.angles);
    const auto row = [&camera](Vec3 axis) {
        return extend(axis, -dot(axis, camera.position));
    };
    return Mat4{.rows = {row(axes.right), row(axes.up), row(axes.forward), Vec4{.w = 1.0F}}};
}

auto projectionMatrix(Degrees fieldOfView, float aspect) noexcept -> Mat4 {
    const HalfExtents half = halfExtentsOf(fieldOfView, aspect);
    const float xScale = 1.0F / half.width;
    const float yScale = 1.0F / half.height;
    // depth = near / z: 1 at the near plane, toward 0 at infinity.
    return Mat4{.rows = {Vec4{.x = xScale}, Vec4{.y = yScale}, Vec4{.w = kNearPlane}, Vec4{.z = 1.0F}}};
}

auto viewProjectionMatrix(const Camera& camera, float aspect) noexcept -> Mat4 {
    return projectionMatrix(camera.fieldOfView, aspect) * viewMatrix(camera);
}

auto frustumPlanes(const Camera& camera, float aspect) noexcept -> std::array<Plane, 4> {
    const AngleVectors axes = angleVectors(camera.angles);
    const HalfExtents half = halfExtentsOf(camera.fieldOfView, aspect);
    // Each holds the eye and an edge of the view; its normal leans from that edge's side to the middle.
    const auto through = [&camera](Vec3 inward) {
        return Plane::through(camera.position, normalized(inward));
    };
    return {through(axes.right + (axes.forward * half.width)),
            through((axes.forward * half.width) - axes.right),
            through(axes.up + (axes.forward * half.height)), through((axes.forward * half.height) - axes.up)};
}

} // namespace nx::render
