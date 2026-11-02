// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The window and the GPU next plays in, and what the GPU learned building its pipelines, kept from run to
/// run.
export module app.client:video;

import std;
import nx.core;
import nx.platform;
import nx.rhi;
import :commandLine;
import :client;

namespace app {

/// The window, the GPU and the frame's pace. The swapchain goes before its device, the device before
/// its window.
struct Video {
    nx::platform::Window window;
    std::unique_ptr<nx::rhi::Device> device;
    std::unique_ptr<nx::rhi::Swapchain> swapchain;
    nx::platform::SleepTimer sleepTimer;
};

/// A window's size in pixels as the GPU takes it.
[[nodiscard]] auto extentOf(nx::platform::PixelSize size) noexcept -> nx::rhi::Extent2d;

/// What the GPU learned building the pipelines the last time next ran; nothing the first time, or
/// when the file cannot be read.
[[nodiscard]] auto readPipelineCache() -> std::vector<std::byte>;

/// Keeps what `device` learned building its pipelines for the next run; logs what fails.
void savePipelineCache(const nx::rhi::Device& device);

/// Opens the window and the GPU, presenting as `isVsync` asks, the device given what the GPU learned
/// building the pipelines before (`pipelineCache`). Errors: WindowUnavailable, GpuFailed.
[[nodiscard]] auto openVideo(const ClientOptions& options, bool isVsync,
                             std::span<const std::byte> pipelineCache) -> nx::Result<Video, ClientError>;

} // namespace app
