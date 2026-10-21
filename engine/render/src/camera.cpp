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

} // namespace

auto viewMatrix(const Camera& camera) noexcept -> Mat4 {
    const AngleVectors axes = angleVectors(camera.angles);
    const auto row = [&camera](Vec3 axis) {
        return extend(axis, -dot(axis, camera.position));
    };
    return Mat4{.rows = {row(axes.right), row(axes.up), row(axes.forward), Vec4{.w = 1.0F}}};
}

auto projectionMatrix(Degrees fieldOfView, float aspect) noexcept -> Mat4 {
    expects(aspect > 0.0F && fieldOfView.value() > 0.0F && fieldOfView.value() < 180.0F,
            "projectionMatrix: a positive aspect and a field of view below 180 degrees");
    // The vertical angle of a 4:3 view with that horizontal angle, kept at every aspect.
    const float halfWidth43 = std::tan(toRadians(fieldOfView).value() * 0.5F);
    const float halfHeight = halfWidth43 / kFieldOfViewAspect;
    const float yScale = 1.0F / halfHeight;
    const float xScale = yScale / aspect;
    // depth = near / z: 1 at the near plane, toward 0 at infinity.
    return Mat4{.rows = {Vec4{.x = xScale}, Vec4{.y = yScale}, Vec4{.w = kNearPlane}, Vec4{.z = 1.0F}}};
}

auto viewProjectionMatrix(const Camera& camera, float aspect) noexcept -> Mat4 {
    return projectionMatrix(camera.fieldOfView, aspect) * viewMatrix(camera);
}

} // namespace nx::render
