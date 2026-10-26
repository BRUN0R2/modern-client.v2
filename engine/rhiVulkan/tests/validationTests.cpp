// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>
#include <volk.h>

import std;
import nx.core;
import nx.rhi;
import nx.rhi.vulkan;

namespace {

constexpr std::string_view kValidationLayer = "VK_LAYER_KHRONOS_validation";

TEST(VulkanValidationTest, RequestedValidationRequiresAnInstalledLayer) {
    if (volkInitialize() != VK_SUCCESS) {
        GTEST_SKIP() << "no Vulkan loader on this machine";
    }
    std::uint32_t count = 0;
    ASSERT_EQ(vkEnumerateInstanceLayerProperties(&count, nullptr), VK_SUCCESS);
    std::vector<VkLayerProperties> installed(count);
    ASSERT_EQ(vkEnumerateInstanceLayerProperties(&count, installed.data()), VK_SUCCESS);
    installed.resize(count);
    if (std::ranges::any_of(installed, [](const VkLayerProperties& layer) {
            return std::string_view{layer.layerName} == kValidationLayer;
        })) {
        GTEST_SKIP() << "the loader exposes installed layers despite the test's empty VK_LAYER_PATH";
    }
    const auto device = nx::rhi::vulkan::createDevice(
        {.applicationName = "nxVulkanValidationTests", .isValidationEnabled = true});
    if (!device && device.error().code == nx::rhi::RhiError::BackendUnavailable) {
        GTEST_SKIP() << nx::describe(device.error());
    }
    ASSERT_FALSE(device.has_value()) << "a validation request must never open an unvalidated device";
    EXPECT_EQ(device.error().code, nx::rhi::RhiError::ValidationUnavailable);
    EXPECT_NE(device.error().detail.find(kValidationLayer), std::string::npos);
}

} // namespace
