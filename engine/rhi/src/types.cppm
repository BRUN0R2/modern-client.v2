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
    DeviceLost,        ///< The driver lost the device: a hang, a reset, a GPU removed.
    SurfaceLost,       ///< The window's surface is gone.
    UnsupportedFormat, ///< The device cannot use the format as the description asks.
    InvalidShader,     ///< The shader code is not of the device's shader format, or the driver refused it.
    DriverRefused,     ///< The driver refused an operation for another reason; the detail has its result.
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

/// The bytes of one texel of `format`; 0 for Undefined.
[[nodiscard]] constexpr auto bytesPerTexel(Format format) noexcept -> std::uint32_t {
    return format == Format::Undefined ? 0 : 4;
}

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

/// What a buffer is used for at a point of the frame, as TextureState is for textures.
enum class BufferState : std::uint8_t {
    Undefined,       ///< Its content is not needed.
    CopySource,      ///< Copied from.
    CopyDestination, ///< Copied to.
    Vertex,          ///< Read as vertices.
    Index,           ///< Read as indices.
    Uniform,         ///< Read as a uniform buffer.
    ShaderRead,      ///< Read as a storage buffer.
    HostRead,        ///< Read by the CPU through its mapping, once the frame is done.
};

/// What a buffer may be used for, from its creation.
enum class BufferUsage : std::uint8_t {
    Vertex,
    Index,
    Uniform,
    Storage,
    CopySource,
    CopyDestination,
    Count,
};

/// Where a buffer's memory is, and who writes it.
enum class MemoryLocation : std::uint8_t {
    Device,   ///< GPU memory, written by copies.
    Upload,   ///< CPU memory the GPU reads, mapped for writing (Device::mappedBytes).
    Readback, ///< CPU memory the GPU writes, mapped for reading.
};

struct BufferDesc {
    std::string_view label; ///< Required: it names the buffer to the debug tools.
    std::uint64_t size = 0;
    Flags<BufferUsage> usage{};
    MemoryLocation location = MemoryLocation::Device;
};

/// What a texture may be used for, from its creation.
enum class TextureUsage : std::uint8_t {
    Sampled,
    RenderTarget,
    DepthTarget,
    CopySource,
    CopyDestination,
    Count,
};

struct TextureDesc {
    std::string_view label; ///< Required: it names the texture to the debug tools.
    Format format = Format::Undefined;
    Extent2d extent;
    std::uint32_t mipLevels = 1;
    Flags<TextureUsage> usage{};
};

enum class Filter : std::uint8_t {
    Nearest,
    Linear,
};

enum class AddressMode : std::uint8_t {
    Repeat,
    ClampToEdge,
};

struct SamplerDesc {
    std::string_view label;
    Filter magnify = Filter::Linear;
    Filter minify = Filter::Linear;
    Filter mipmap = Filter::Linear;
    AddressMode address = AddressMode::Repeat;
    float maxAnisotropy = 1.0F; ///< 1 is off.
};

/// The code a device's shaders are in: what the build compiles the Slang sources to for it.
enum class ShaderFormat : std::uint8_t {
    Spirv, ///< Vulkan.
    Dxbc,  ///< DirectX 11 (future).
};

enum class ShaderStage : std::uint8_t {
    Vertex,
    Fragment,
    Count,
};

struct ShaderDesc {
    std::string_view label;
    std::span<const std::byte> code; ///< In the device's ShaderFormat; copied by the device.
};

/// What a binding of a bind group holds. Textures and samplers bind apart, as in DirectX 11.
enum class BindingType : std::uint8_t {
    UniformBuffer,
    StorageBuffer,
    SampledTexture,
    Sampler,
};

struct BindingLayout {
    std::uint32_t binding = 0;
    BindingType type = BindingType::UniformBuffer;
    Flags<ShaderStage> stages{};
};

struct BindGroupLayoutDesc {
    std::string_view label;
    std::span<const BindingLayout> bindings;
};

enum class VertexFormat : std::uint8_t {
    Float2,
    Float3,
    Float4,
    Unorm8x4, ///< Four bytes read as values from 0 to 1.
};

struct VertexAttribute {
    std::uint32_t location = 0;
    VertexFormat format = VertexFormat::Float3;
    std::uint32_t offset = 0;
};

/// The vertices of one vertex buffer slot.
struct VertexBufferLayout {
    std::uint32_t stride = 0;
    std::span<const VertexAttribute> attributes;
};

enum class PrimitiveTopology : std::uint8_t {
    TriangleList,
    TriangleStrip,
};

enum class CullMode : std::uint8_t {
    None,
    Front,
    Back,
};

/// Which winding faces the viewer.
enum class FrontFace : std::uint8_t {
    CounterClockwise,
    Clockwise,
};

enum class CompareOp : std::uint8_t {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
};

enum class BlendFactor : std::uint8_t {
    Zero,
    One,
    SourceAlpha,
    OneMinusSourceAlpha,
    SourceColor,
    DestinationColor,
};

/// result = source * sourceFactor + destination * destinationFactor, for color and alpha.
struct BlendState {
    BlendFactor sourceColor = BlendFactor::One;
    BlendFactor destinationColor = BlendFactor::Zero;
    BlendFactor sourceAlpha = BlendFactor::One;
    BlendFactor destinationAlpha = BlendFactor::Zero;
};

struct ColorTarget {
    Format format = Format::Undefined;
    std::optional<BlendState> blend{}; ///< None: the fragment replaces what is there.
};

struct DepthState {
    Format format = Format::Depth32Float;
    bool isWriteEnabled = true;
    CompareOp compare = CompareOp::Less;
};

enum class IndexFormat : std::uint8_t {
    Uint16,
    Uint32,
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

/// A rectangle of a render target, in pixels from its top-left corner.
struct Rect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    Extent2d size;
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
