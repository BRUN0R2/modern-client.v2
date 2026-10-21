// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The vocabulary of the RHI (docs/05-rendering.md, section 3): its errors, formats, states and
/// the descriptions resources and passes are made from.
export module nx.rhi:types;

import std;
import nx.core;

export namespace nx::rhi {

/// What can go wrong on the GPU. The detail has what the API reported.
enum class RhiError : std::uint8_t {
    BackendUnavailable, ///< The GPU API cannot be loaded: no driver for it is installed.
    NoSuitableAdapter,  ///< No GPU has what the engine requires; the detail says why each was refused.
    SurfaceUnavailable, ///< There is no window to present to, or the window system refused it.
    OutOfHostMemory,
    OutOfDeviceMemory,
    DeviceLost,    ///< The driver lost the device: a hang, a reset, a GPU removed.
    SurfaceLost,   ///< The window's surface is gone.
    DriverRefused, ///< The driver refused an operation for another reason; the detail has its result.
};

/// A size in texels.
struct Extent2d {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] inline auto operator==(const Extent2d&) const noexcept -> bool = default;
};

/// How a texture stores its texels.
enum class Format : std::uint8_t {
    Undefined,
    Rgba8Unorm,
    Bgra8Unorm,
    Rgba8Srgb,
    Bgra8Srgb,
    Depth32Float,
};

/// What a texture is used for at a point of the frame. Moving a texture from one state to another
/// is a transition (CommandList::transition), which the render graph will derive.
enum class TextureState : std::uint8_t {
    Undefined,       ///< Its content is not needed: what the texture was before does not matter.
    RenderTarget,    ///< Drawn to as a color attachment.
    DepthTarget,     ///< Drawn to as the depth attachment.
    ShaderRead,      ///< Sampled by shaders.
    CopySource,      ///< Copied from.
    CopyDestination, ///< Copied to.
    Present,         ///< Shown by the swapchain.
};

/// How a swapchain shows its images: the r_present_mode cvar.
enum class PresentMode : std::uint8_t {
    Fifo,      ///< Waits for the display's refresh (vsync); every backend has it.
    Mailbox,   ///< Replaces the waiting image with the newest one: no tearing, no wait.
    Immediate, ///< Shows at once, tearing.
};

/// What a render pass does with an attachment's previous content.
enum class LoadOp : std::uint8_t {
    Load,     ///< Keeps it.
    Clear,    ///< Replaces it with the clear value.
    DontCare, ///< Discards it: every texel will be drawn.
};

/// What a render pass does with what it drew in an attachment.
enum class StoreOp : std::uint8_t {
    Store,
    DontCare,
};

struct ClearColor {
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;
    float alpha = 1.0F;
};

/// The kind of GPU an adapter is.
enum class AdapterKind : std::uint8_t {
    Discrete,
    Integrated,
    Virtual,
    Cpu, ///< A software renderer, such as lavapipe in CI.
    Other,
};

struct AdapterInfo {
    std::string name;
    AdapterKind kind = AdapterKind::Other;
    std::string driver; ///< The driver's name and version, as it reports them.
};

} // namespace nx::rhi
