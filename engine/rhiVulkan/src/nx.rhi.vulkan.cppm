// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.rhi.vulkan — the Vulkan 1.3 backend of nx.rhi (docs/05-rendering.md, section 4; ADR-0004):
/// volk loads the API, dynamic rendering and synchronization2 record the passes, and a timeline
/// semaphore paces the frames in flight. No Vulkan header crosses this interface: the device is an
/// nx::rhi::Device.
export module nx.rhi.vulkan;

import std;
import nx.core;
import nx.platform;
import nx.rhi;

export namespace nx::rhi::vulkan {

using rhi::kMaxFramesInFlight;

struct DeviceDesc {
    std::string_view applicationName = "Modern-Client.v2";
    /// The window to present to (Device::createSwapchain); none for work that is never shown.
    std::optional<platform::NativeWindow> window{};
    /// The Khronos validation layer, with synchronization validation, when it is installed: every
    /// message is logged and every error counted (Device::validationErrors).
    bool isValidationEnabled = false;
    std::uint32_t framesInFlight = 2; ///< 2 or kMaxFramesInFlight.
};

/// Opens the best GPU that has what the engine requires: Vulkan 1.3 with dynamic rendering,
/// synchronization2, timeline semaphores and shader draw parameters, a graphics queue and, with a
/// window, presentation to it. Anisotropic filtering is used when the GPU has it. A discrete GPU
/// wins over an integrated one, which wins over a software renderer. One device at a time: volk
/// loads one device's functions. Errors: BackendUnavailable (no Vulkan loader, or none of version
/// 1.3), NoSuitableAdapter, SurfaceUnavailable, OutOfHostMemory, OutOfDeviceMemory, DriverRefused.
[[nodiscard]] auto createDevice(const DeviceDesc& desc) -> Result<std::unique_ptr<Device>, RhiError>;

} // namespace nx::rhi::vulkan
