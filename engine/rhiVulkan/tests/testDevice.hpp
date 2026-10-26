// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// A typed result lets fixture setup stop on unexpected failures before entering the test body.
// Include after importing std, nx.core, nx.rhi and nx.rhi.vulkan.

#include "vulkanAvailability.hpp"

namespace nx::test {

/// A device without a window, with validation; the caller distinguishes absence from a setup error.
[[nodiscard]] inline auto openTestDevice(std::uint32_t framesInFlight = 2)
    -> nx::Result<std::unique_ptr<nx::rhi::Device>, nx::rhi::RhiError> {
    return nx::rhi::vulkan::createDevice({.applicationName = "nxRhiVulkanTests",
                                          .isValidationEnabled = true,
                                          .framesInFlight = framesInFlight});
}

} // namespace nx::test
