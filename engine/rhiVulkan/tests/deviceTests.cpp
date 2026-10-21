// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The Vulkan backend on whatever GPU the machine has: in CI on Linux, lavapipe (Mesa's software
// Vulkan) with the validation layers, so every test also requires that validation reports no
// error. A machine without Vulkan skips them (docs/18-testing-and-quality.md).

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.rhi;
import nx.rhi.vulkan;

namespace {

using nx::rhi::Device;
using nx::rhi::RhiError;

constexpr std::string_view kNoVulkan = "no Vulkan 1.3 device on this machine";

/// A device without a window, with validation; null when the machine has no Vulkan 1.3 device.
[[nodiscard]] auto openDevice(std::uint32_t framesInFlight = 2) -> std::unique_ptr<Device> {
    auto device = nx::rhi::vulkan::createDevice({.applicationName = "nxRhiVulkanTests",
                                                 .isValidationEnabled = true,
                                                 .framesInFlight = framesInFlight});
    if (!device) {
        const RhiError code = device.error().code;
        EXPECT_TRUE(code == RhiError::BackendUnavailable || code == RhiError::NoSuitableAdapter)
            << nx::describe(device.error());
        return nullptr;
    }
    return std::move(*device);
}

TEST(VulkanDeviceTest, OpensTheBestAdapter) {
    const auto device = openDevice();
    if (!device) {
        GTEST_SKIP() << kNoVulkan;
    }
    EXPECT_FALSE(device->adapter().name.empty());
    EXPECT_FALSE(device->adapter().driver.empty());
    EXPECT_EQ(device->validationErrors(), 0U);
}

TEST(VulkanDeviceTest, ADeviceWithoutAWindowHasNoSwapchain) {
    const auto device = openDevice();
    if (!device) {
        GTEST_SKIP() << kNoVulkan;
    }
    const auto swapchain = device->createSwapchain({.size = {.width = 64, .height = 64}});
    ASSERT_FALSE(swapchain.has_value());
    EXPECT_EQ(swapchain.error().code, RhiError::SurfaceUnavailable);
}

TEST(VulkanDeviceTest, RunsMoreFramesThanItHasInFlight) {
    for (const std::uint32_t framesInFlight : {2U, nx::rhi::vulkan::kMaxFramesInFlight}) {
        const auto device = openDevice(framesInFlight);
        if (!device) {
            GTEST_SKIP() << kNoVulkan;
        }
        for (int frame = 0; frame < 10; ++frame) {
            const auto commands = device->beginFrame();
            ASSERT_TRUE(commands.has_value()) << nx::describe(commands.error());
            const auto submitted = device->submitFrame();
            ASSERT_TRUE(submitted.has_value()) << nx::describe(submitted.error());
        }
        device->waitIdle();
        EXPECT_EQ(device->validationErrors(), 0U) << framesInFlight << " frames in flight";
    }
}

TEST(VulkanDeviceTest, AnotherDeviceOpensOnceTheFirstIsClosed) {
    if (!openDevice()) {
        GTEST_SKIP() << kNoVulkan;
    }
    const auto second = openDevice();
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->validationErrors(), 0U);
}

} // namespace
