// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Test helper: the Vulkan device of the tests, without a window and with validation. Include after
// <gtest/gtest.h> and `import nx.rhi.vulkan;`.

namespace nx::test {

inline constexpr std::string_view kNoVulkan = "no Vulkan 1.3 device on this machine";

/// A device without a window, with validation; null when the machine has no Vulkan 1.3 device.
[[nodiscard]] inline auto openTestDevice(std::uint32_t framesInFlight = 2)
    -> std::unique_ptr<nx::rhi::Device> {
    auto device = nx::rhi::vulkan::createDevice({.applicationName = "nxRhiVulkanTests",
                                                 .isValidationEnabled = true,
                                                 .framesInFlight = framesInFlight});
    if (!device) {
        const nx::rhi::RhiError code = device.error().code;
        EXPECT_TRUE(code == nx::rhi::RhiError::BackendUnavailable ||
                    code == nx::rhi::RhiError::NoSuitableAdapter)
            << nx::describe(device.error());
        return nullptr;
    }
    return std::move(*device);
}

} // namespace nx::test
