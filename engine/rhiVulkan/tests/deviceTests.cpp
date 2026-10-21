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

#include "testDevice.hpp"

namespace {

using nx::rhi::RhiError;
using nx::test::kNoVulkan;
using nx::test::openTestDevice;

TEST(VulkanDeviceTest, OpensTheBestAdapter) {
    const auto device = openTestDevice();
    if (!device) {
        GTEST_SKIP() << kNoVulkan;
    }
    EXPECT_FALSE(device->adapter().name.empty());
    EXPECT_FALSE(device->adapter().driver.empty());
    EXPECT_EQ(device->shaderFormat(), nx::rhi::ShaderFormat::Spirv);
    EXPECT_EQ(device->validationErrors(), 0U);
}

TEST(VulkanDeviceTest, ADeviceWithoutAWindowHasNoSwapchain) {
    const auto device = openTestDevice();
    if (!device) {
        GTEST_SKIP() << kNoVulkan;
    }
    const auto swapchain = device->createSwapchain({.size = {.width = 64, .height = 64}});
    ASSERT_FALSE(swapchain.has_value());
    EXPECT_EQ(swapchain.error().code, RhiError::SurfaceUnavailable);
}

TEST(VulkanDeviceTest, RunsMoreFramesThanItHasInFlight) {
    for (const std::uint32_t framesInFlight : {2U, nx::rhi::vulkan::kMaxFramesInFlight}) {
        const auto device = openTestDevice(framesInFlight);
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
    if (!openTestDevice()) {
        GTEST_SKIP() << kNoVulkan;
    }
    const auto second = openTestDevice();
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->validationErrors(), 0U);
}

TEST(VulkanDeviceTest, ADestroyedResourceLivesUntilItsFramesAreDone) {
    const auto device = openTestDevice();
    if (!device) {
        GTEST_SKIP() << kNoVulkan;
    }
    const auto buffer = device->createBuffer({.label = "test.retired",
                                              .size = 256,
                                              .usage = {nx::rhi::BufferUsage::CopyDestination},
                                              .location = nx::rhi::MemoryLocation::Device});
    ASSERT_TRUE(buffer.has_value()) << nx::describe(buffer.error());
    const auto commands = device->beginFrame();
    ASSERT_TRUE(commands.has_value());
    device->destroy(*buffer); // while the frame being recorded may still use it
    ASSERT_TRUE(device->submitFrame().has_value());
    for (int frame = 0; frame < 4; ++frame) { // the frames after it destroy it
        ASSERT_TRUE(device->beginFrame().has_value());
        ASSERT_TRUE(device->submitFrame().has_value());
    }
    device->waitIdle();
    EXPECT_EQ(device->validationErrors(), 0U);
}

} // namespace
