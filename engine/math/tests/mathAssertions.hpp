// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Approximate comparisons for the nx.math tests: EXPECT_TRUE(isNear(actual, expected)) reports
// both values at the call site. Include after `import nx.math;`.

namespace nx::test {

/// Default tolerance: a few float ulps around 1, enough for one trigonometric round trip.
inline constexpr float kTolerance = 1e-5F;

[[nodiscard]] inline auto isNear(float actual, float expected, float tolerance = kTolerance)
    -> testing::AssertionResult {
    if (std::abs(actual - expected) <= tolerance) {
        return testing::AssertionSuccess();
    }
    return testing::AssertionFailure()
           << std::format("{} is not within {} of {}", actual, tolerance, expected);
}

[[nodiscard]] inline auto isNear(nx::Vec3 actual, nx::Vec3 expected, float tolerance = kTolerance)
    -> testing::AssertionResult {
    if (std::abs(actual.x - expected.x) <= tolerance && std::abs(actual.y - expected.y) <= tolerance &&
        std::abs(actual.z - expected.z) <= tolerance) {
        return testing::AssertionSuccess();
    }
    return testing::AssertionFailure()
           << std::format("{} is not within {} of {}", actual, tolerance, expected);
}

[[nodiscard]] inline auto isNear(const nx::Mat3& actual, const nx::Mat3& expected,
                                 float tolerance = kTolerance) -> testing::AssertionResult {
    for (std::size_t row = 0; row < nx::Mat3::kRowCount; ++row) {
        if (const auto result = isNear(actual.rows[row], expected.rows[row], tolerance); !result) {
            return testing::AssertionFailure() << "row " << row << ": " << result.message();
        }
    }
    return testing::AssertionSuccess();
}

/// q and -q are the same rotation: compares up to sign.
[[nodiscard]] inline auto isSameRotation(nx::Quat actual, nx::Quat expected, float tolerance = kTolerance)
    -> testing::AssertionResult {
    const auto near = [tolerance](nx::Quat a, nx::Quat b) {
        return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance &&
               std::abs(a.z - b.z) <= tolerance && std::abs(a.w - b.w) <= tolerance;
    };
    if (near(actual, expected) || near(actual, -expected)) {
        return testing::AssertionSuccess();
    }
    return testing::AssertionFailure()
           << std::format("({} {} {} {}) is not the rotation ({} {} {} {})", actual.x, actual.y, actual.z,
                          actual.w, expected.x, expected.y, expected.z, expected.w);
}

} // namespace nx::test
