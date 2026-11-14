// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module app.client;

import std;
import nx.core;
import nx.platform;
import nx.rhi;
import nx.rhi.vulkan;

namespace app {

namespace {

constexpr nx::log::Category kLog{"client"};

/// The file of the cache folder (nx::platform::cacheDirectory) that keeps what the GPU learned
/// building next's pipelines.
constexpr std::string_view kPipelineCacheFile = "pipelineCache.bin";

} // namespace

auto extentOf(nx::platform::PixelSize size) noexcept -> nx::rhi::Extent2d {
    return {.width = size.width, .height = size.height};
}

/// What the GPU learned building the pipelines the last time next ran; nothing the first time, or
/// when the file cannot be read.
auto readPipelineCache() -> std::vector<std::byte> {
    const auto folder = nx::platform::cacheDirectory();
    if (!folder) {
        return {};
    }
    return nx::platform::readFile(*folder / kPipelineCacheFile).value_or(std::vector<std::byte>{});
}

/// Keeps what `device` learned building its pipelines for the next run; logs what fails.
void savePipelineCache(const nx::rhi::Device& device) {
    auto learned = device.pipelineCache();
    auto folder = nx::platform::cacheDirectory();
    if (!learned || !folder) {
        nx::log::warning(kLog, "the pipeline cache is not kept: {}",
                         !learned ? nx::describe(learned.error()) : nx::describe(folder.error()));
        return;
    }
    if (auto made = nx::platform::createDirectories(*folder); !made) {
        nx::log::warning(kLog, "the pipeline cache is not kept: {}", nx::describe(made.error()));
        return;
    }
    if (auto written = nx::platform::writeFile(*folder / kPipelineCacheFile, *learned); !written) {
        nx::log::warning(kLog, "the pipeline cache is not kept: {}", nx::describe(written.error()));
    }
}

auto presentModeOf(bool isVsync) noexcept -> nx::rhi::PresentMode {
    return isVsync ? nx::rhi::PresentMode::Fifo : nx::rhi::PresentMode::Immediate;
}

/// Opens the window and the GPU, presenting as `isVsync` asks, the device given what the GPU learned
/// building the pipelines before (`pipelineCache`). Errors: WindowUnavailable, GpuFailed.
auto openVideo(const ClientOptions& options, bool isVsync, std::span<const std::byte> pipelineCache)
    -> nx::Result<Video, ClientError> {
    // Hidden until its first image (Client::drawFrame): never empty while the GPU starts and the map loads.
    auto window = nx::platform::Window::create({.title = "Modern-Client.v2", .isShown = false});
    if (!window) {
        return nx::fail(ClientError::WindowUnavailable, std::move(window.error()));
    }
    auto native = window->nativeWindow();
    auto size = window->pixelSize();
    if (!native || !size) {
        return !native ? nx::fail(ClientError::WindowUnavailable, std::move(native.error()))
                       : nx::fail(ClientError::WindowUnavailable, std::move(size.error()));
    }
    auto device = nx::rhi::vulkan::createDevice({.applicationName = "next",
                                                 .window = *native,
                                                 .isValidationEnabled = options.isValidationEnabled,
                                                 .pipelineCache = pipelineCache});
    if (!device) {
        return nx::fail(ClientError::GpuFailed, std::move(device.error()));
    }
    auto swapchain =
        (*device)->createSwapchain({.size = extentOf(*size), .presentMode = presentModeOf(isVsync)});
    if (!swapchain) {
        return nx::fail(ClientError::GpuFailed, std::move(swapchain.error()));
    }
    auto sleepTimer = nx::platform::SleepTimer::create();
    if (!sleepTimer) {
        return nx::fail(ClientError::GpuFailed, "the frame's pace", std::move(sleepTimer.error()));
    }
    if (auto captured = window->setMouseMode(nx::platform::MouseMode::Relative); !captured) {
        nx::log::warning(kLog, "{}", nx::describe(captured.error())); // the keys still play
    }
    return Video{.window = std::move(*window),
                 .device = std::move(*device),
                 .swapchain = std::move(*swapchain),
                 .sleepTimer = std::move(*sleepTimer)};
}

} // namespace app
