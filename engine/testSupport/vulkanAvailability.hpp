// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Include after importing std and nx.rhi. The test boundary owns its fatal assertion: a nonfatal
// failure followed by a skipped SetUp can still let GoogleTest enter an uninitialized fixture.

namespace nx::test {

inline constexpr std::string_view kNoVulkan = "no Vulkan 1.3 device on this machine";

[[nodiscard]] constexpr auto isVulkanUnavailable(rhi::RhiError code) noexcept -> bool {
    return code == rhi::RhiError::BackendUnavailable || code == rhi::RhiError::NoSuitableAdapter;
}

} // namespace nx::test
