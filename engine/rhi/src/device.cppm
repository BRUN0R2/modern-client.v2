// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The device, its swapchain and the command lists of a frame (docs/05-rendering.md, section 3):
/// what the renderer records against, whatever the backend.
export module nx.rhi:device;

import std;
import nx.core;
import :types;
import :handles;

export namespace nx::rhi {

/// A color attachment of a render pass.
struct ColorAttachment {
    TextureHandle texture;
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::Store;
    ClearColor clear{};
};

/// The attachments a render pass draws to, and the area it draws in.
struct RenderingDesc {
    std::span<const ColorAttachment> colors;
    Extent2d area;
};

/// The commands of a frame, recorded in order and run by the GPU when the frame is submitted.
class CommandList {
public:
    CommandList() = default;
    CommandList(const CommandList&) = delete;
    CommandList(CommandList&&) = delete;
    auto operator=(const CommandList&) -> CommandList& = delete;
    auto operator=(CommandList&&) -> CommandList& = delete;
    virtual ~CommandList() = default;

    /// Makes `texture`, used as `before` until now, ready for `after`: the commands that use it as
    /// `after` wait for the ones that used it as `before`.
    virtual void transition(TextureHandle texture, TextureState before, TextureState after) = 0;

    /// Starts a render pass: its color attachments must be in TextureState::RenderTarget.
    virtual void beginRendering(const RenderingDesc& desc) = 0;
    virtual void endRendering() = 0;
};

struct SwapchainDesc {
    Extent2d size; ///< The window's drawable size; the surface may impose its own.
    PresentMode presentMode = PresentMode::Fifo;
};

/// The image of the frame: draw to it, then leave it in TextureState::Present.
struct SwapchainImage {
    TextureHandle texture; ///< TextureState::Undefined when acquired: its old content is gone.
    Extent2d size;
    Format format = Format::Undefined;
};

/// The images a window shows, one per frame. It must not outlive its device.
class Swapchain {
public:
    Swapchain() = default;
    Swapchain(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = delete;
    auto operator=(const Swapchain&) -> Swapchain& = delete;
    auto operator=(Swapchain&&) -> Swapchain& = delete;
    virtual ~Swapchain() = default;

    /// The image to draw this frame, between Device::beginFrame and Device::submitFrame; nullopt
    /// when there is none to draw to (the window is minimized, or the swapchain was out of date and
    /// is recreated for the next frame). Errors: SurfaceLost, DeviceLost, OutOfHostMemory,
    /// OutOfDeviceMemory, DriverRefused.
    [[nodiscard]] virtual auto acquire() -> Result<std::optional<SwapchainImage>, RhiError> = 0;

    /// Recreates the images for the window's new drawable size. Errors: as acquire.
    [[nodiscard]] virtual auto resize(Extent2d size) -> Result<void, RhiError> = 0;
};

/// A GPU, and the frames it runs: up to a backend's frames in flight are recorded while the GPU
/// runs the ones before. Use it from one thread: the render thread.
class Device {
public:
    Device() = default;
    Device(const Device&) = delete;
    Device(Device&&) = delete;
    auto operator=(const Device&) -> Device& = delete;
    auto operator=(Device&&) -> Device& = delete;
    virtual ~Device() = default;

    [[nodiscard]] virtual auto adapter() const noexcept -> const AdapterInfo& = 0;

    /// The swapchain of the window the device was created for. Errors: SurfaceUnavailable (made
    /// without a window), and those of Swapchain::acquire.
    [[nodiscard]] virtual auto createSwapchain(const SwapchainDesc& desc)
        -> Result<std::unique_ptr<Swapchain>, RhiError> = 0;

    /// Starts a frame: waits until the GPU has finished the frame that last used this frame's
    /// resources, and gives the command list to record it in. Errors: DeviceLost, DriverRefused.
    [[nodiscard]] virtual auto beginFrame() -> Result<std::reference_wrapper<CommandList>, RhiError> = 0;

    /// Submits the frame's commands; with `presentTo`, shows the image acquired from it this frame,
    /// once the commands have drawn it. Errors: DeviceLost, SurfaceLost, OutOfHostMemory,
    /// OutOfDeviceMemory, DriverRefused.
    [[nodiscard]] virtual auto submitFrame() -> Result<void, RhiError> = 0;
    [[nodiscard]] virtual auto submitFrame(Swapchain& presentTo) -> Result<void, RhiError> = 0;

    /// Waits until the GPU has finished everything submitted: before resources it used are gone.
    virtual void waitIdle() noexcept = 0;

    /// Errors the backend's validation reported (the Vulkan validation layers in development
    /// builds), which the device also logs. Zero without validation.
    [[nodiscard]] virtual auto validationErrors() const noexcept -> std::uint64_t = 0;
};

} // namespace nx::rhi
