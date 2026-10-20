// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// GoldSrc angles: view and entity orientation as pitch, yaw and roll in degrees, and the axes they
/// produce (the original's AngleVectors). Implemented from the definition of the rotation, not from
/// the original code (docs/20-legal-and-licensing.md, section 3).
export module nx.math:angles;

import std;
import nx.core;
import :vector;
import :matrix;

export namespace nx {

/// Orientation in degrees, in the GoldSrc convention, applied as roll, then pitch, then yaw:
/// - pitch turns about +Y; positive looks down;
/// - yaw turns about +Z; 0 faces +X and positive turns left (counter-clockwise seen from above);
/// - roll turns about the forward axis; positive leans the view to the right.
struct EulerAngles {
    Degrees pitch{};
    Degrees yaw{};
    Degrees roll{};

    [[nodiscard]] constexpr auto operator==(const EulerAngles&) const noexcept -> bool = default;
};

/// The unit axes of an orientation, as returned by the original's AngleVectors.
struct AngleVectors {
    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};
};

/// Rotation that takes +X, +Y and +Z to the forward, left and up axes of `angles`:
/// Rz(yaw) * Ry(pitch) * Rx(roll), each a right-handed rotation about its axis.
[[nodiscard]] auto rotationMatrix(EulerAngles angles) noexcept -> Mat3;

/// Forward, right and up of `angles`: the columns of rotationMatrix(angles), the second one negated
/// (right is -left). Pitch 0, yaw 0 and roll 0 give +X, -Y and +Z.
[[nodiscard]] auto angleVectors(EulerAngles angles) noexcept -> AngleVectors;

/// Pitch and yaw (roll 0) that face `direction`, which must not be the zero vector, so that
/// angleVectors(vectorAngles(d)).forward is d normalized. Pitch is in [-90, 90] and yaw in
/// [-180, 180]; straight up or down gives yaw 0.
///
/// The original's VectorAngles returns the opposite pitch (positive up, the convention of model
/// angles) and yaw in [0, 360). Here the result is in the convention of EulerAngles itself, so code
/// ported from the original must not negate the pitch to get a view angle.
[[nodiscard]] auto vectorAngles(Vec3 direction) noexcept -> EulerAngles;

/// The same direction as `angle`, which must be finite, in (-180, 180].
[[nodiscard]] auto normalizeAngle(Degrees angle) noexcept -> Degrees;

namespace detail {

struct SineCosine {
    float sine = 0.0F;
    float cosine = 0.0F;
};

/// Sine and cosine of `angle`, evaluated in double and rounded once to float (see nx.math).
[[nodiscard]] auto sineCosine(Degrees angle) noexcept -> SineCosine;

} // namespace detail

} // namespace nx

/// Formats as "(pitch yaw roll)" in degrees, with the float format spec applied to each angle.
template <>
struct std::formatter<nx::EulerAngles> : std::formatter<float> {
    template <class FormatContext>
    auto format(const nx::EulerAngles& angles, FormatContext& context) const {
        auto out = context.out();
        bool first = true;
        for (const nx::Degrees angle : {angles.pitch, angles.yaw, angles.roll}) {
            *out++ = first ? '(' : ' ';
            first = false;
            context.advance_to(out);
            out = std::formatter<float>::format(angle.value(), context);
        }
        *out++ = ')';
        return out;
    }
};
