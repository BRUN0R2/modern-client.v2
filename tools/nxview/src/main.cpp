// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// nxview: the map and model viewer (TODO.md, M2). Today it opens the window and runs the frames of
// the Vulkan backend, clearing each one: the device, the swapchain and their recreation on resize.

#include <cstdio>
#include <cstdlib>

import std;
import nx.core;
import nx.platform;
import nx.rhi;
import nx.rhi.vulkan;

namespace {

constexpr int kExitUnusable = 2; // the arguments, the window or the GPU
constexpr std::string_view kUsage = R"(usage: nxview [--validation]

Opens a window and draws to it with the Vulkan backend. Escape or closing the window ends it.

  --validation  the Khronos validation layer, when it is installed: its messages are logged and
                nxview exits with 1 when it reported an error
)";
/// How long a minimized window waits before it looks for events again.
constexpr std::chrono::milliseconds kMinimizedWait{50};
/// The color of an empty frame: the dark blue-grey of the original console.
constexpr nx::rhi::ClearColor kBackground{.red = 0.07F, .green = 0.09F, .blue = 0.12F, .alpha = 1.0F};

[[nodiscard]] auto extentOf(nx::platform::PixelSize size) noexcept -> nx::rhi::Extent2d {
    return {.width = size.width, .height = size.height};
}

/// Prints `failure` and returns kExitUnusable.
template <class Code>
[[nodiscard]] auto unusable(const nx::Failure<Code>& failure) -> int {
    std::println(stderr, "nxview: {}", nx::describe(failure));
    return kExitUnusable;
}

/// Records the frame's commands for `image`: clear it and leave it to be shown.
void recordFrame(nx::rhi::CommandList& commands, const nx::rhi::SwapchainImage& image) {
    commands.transition(image.texture, nx::rhi::TextureState::Undefined, nx::rhi::TextureState::RenderTarget);
    const std::array colors{nx::rhi::ColorAttachment{.texture = image.texture,
                                                     .load = nx::rhi::LoadOp::Clear,
                                                     .store = nx::rhi::StoreOp::Store,
                                                     .clear = kBackground}};
    commands.beginRendering({.colors = colors, .area = image.size});
    commands.endRendering();
    commands.transition(image.texture, nx::rhi::TextureState::RenderTarget, nx::rhi::TextureState::Present);
}

[[nodiscard]] auto run(bool isValidationEnabled) -> int {
    auto window = nx::platform::Window::create({.title = "nxview", .size = {.width = 1280, .height = 720}});
    if (!window) {
        return unusable(window.error());
    }
    auto native = window->nativeWindow();
    if (!native) {
        return unusable(native.error());
    }
    auto device = nx::rhi::vulkan::createDevice(
        {.applicationName = "nxview", .window = *native, .isValidationEnabled = isValidationEnabled});
    if (!device) {
        return unusable(device.error());
    }
    auto swapchain = (*device)->createSwapchain({.size = extentOf(window->pixelSize())});
    if (!swapchain) {
        return unusable(swapchain.error());
    }
    auto sleepTimer = nx::platform::SleepTimer::create();
    if (!sleepTimer) {
        return unusable(sleepTimer.error());
    }

    std::vector<nx::platform::WindowEvent> events;
    bool isRunning = true;
    while (isRunning) {
        events.clear();
        window->pollEvents(events);
        for (const nx::platform::WindowEvent& event : events) {
            if (std::holds_alternative<nx::platform::CloseRequested>(event)) {
                isRunning = false;
            } else if (const auto* key = std::get_if<nx::platform::KeyChanged>(&event);
                       key != nullptr && key->key == nx::platform::Key::Escape && key->isDown) {
                isRunning = false;
            } else if (const auto* resized = std::get_if<nx::platform::Resized>(&event)) {
                if (auto recreated = (*swapchain)->resize(extentOf(resized->size)); !recreated) {
                    return unusable(recreated.error());
                }
            }
        }

        auto commands = (*device)->beginFrame();
        if (!commands) {
            return unusable(commands.error());
        }
        auto image = (*swapchain)->acquire();
        if (!image) {
            return unusable(image.error());
        }
        if (*image) {
            recordFrame(commands->get(), **image);
            if (auto submitted = (*device)->submitFrame(**swapchain); !submitted) {
                return unusable(submitted.error());
            }
        } else {
            if (auto submitted = (*device)->submitFrame(); !submitted) {
                return unusable(submitted.error());
            }
            sleepTimer->sleepUntil(nx::platform::MonotonicClock::now() + kMinimizedWait); // nothing to show
        }
    }

    (*device)->waitIdle();
    const std::uint64_t validationErrors = (*device)->validationErrors();
    swapchain->reset(); // before its device
    if (validationErrors != 0) {
        std::println(stderr, "nxview: the validation layer reported {} errors", validationErrors);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

} // namespace

auto main(int argumentCount, char** argumentValues) -> int {
    const auto arguments = std::span{argumentValues, nx::narrow<std::size_t>(argumentCount)} |
                           std::views::drop(1) | std::ranges::to<std::vector<std::string_view>>();
    if (std::ranges::contains(arguments, std::string_view{"--help"})) {
        std::print("{}", kUsage);
        return EXIT_SUCCESS;
    }
    if (std::ranges::any_of(arguments,
                            [](std::string_view argument) { return argument != "--validation"; })) {
        std::print(stderr, "{}", kUsage);
        return kExitUnusable;
    }
    return run(std::ranges::contains(arguments, std::string_view{"--validation"}));
}
