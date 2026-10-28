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
    ValidationUnavailable, ///< Requested validation lacks its layer or debug messenger extension.
};

/// The fewest and the most frames a device records while the GPU runs the ones before
/// (r_frames_in_flight), on every backend: the most is what a ring of per-frame resources needs.
inline constexpr std::uint32_t kMinFramesInFlight = 2;
inline constexpr std::uint32_t kMaxFramesInFlight = 3;

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
    /// Light beyond 1 (high dynamic range): three unsigned floats of 11, 11 and 10 bits, no alpha, in
    /// four bytes.
    Rg11b10Ufloat,
    /// Four half floats, in eight bytes.
    Rgba16Float,
};

/// The bytes of one texel of `format`; 0 for Undefined.
[[nodiscard]] constexpr auto bytesPerTexel(Format format) noexcept -> std::uint32_t {
    constexpr std::uint32_t kFourBytes = 4;
    constexpr std::uint32_t kEightBytes = 8;
    switch (format) {
    case Format::Undefined:
        return 0;
    case Format::Rgba16Float:
        return kEightBytes;
    case Format::Rgba8Unorm:
    case Format::Bgra8Unorm:
    case Format::Rgba8Srgb:
    case Format::Bgra8Srgb:
    case Format::Depth32Float:
    case Format::Rg11b10Ufloat:
        break;
    }
    return kFourBytes;
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

/// A buffer to create (Device::createBuffer).
struct BufferDesc {
    std::string_view label{}; ///< Required: it names the buffer to the debug tools.
    std::uint64_t size = 0;   ///< In bytes.
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

/// The faces of a cube texture, its layers six by six: +x, -x, +y, -y, +z and -z.
inline constexpr std::uint32_t kCubeFaces = 6;

/// A texture to create (Device::createTexture).
struct TextureDesc {
    std::string_view label{}; ///< Required: it names the texture to the debug tools.
    Format format = Format::Undefined;
    Extent2d extent{};
    std::uint32_t mipLevels = 1;
    /// More than 1: layers of one size, each with its mip levels, sampled as a Texture2DArray. A
    /// depth target has 1; a render target draws to one of them at a time (ColorAttachment).
    std::uint32_t arrayLayers = 1;
    Flags<TextureUsage> usage{};
    /// Its layers are the faces of cubes, kCubeFaces a cube, square: sampled as a TextureCube (one
    /// cube) or a TextureCubeArray.
    bool isCube = false;
};

/// How a sampler reads between texels, or between mip levels.
enum class Filter : std::uint8_t {
    Nearest, ///< The nearest one.
    Linear,  ///< A blend of the nearest ones.
};

/// What a sampler reads outside a texture's 0 to 1.
enum class AddressMode : std::uint8_t {
    Repeat,      ///< The texture again.
    ClampToEdge, ///< Its nearest edge.
};

/// A sampler to create (Device::createSampler).
struct SamplerDesc {
    std::string_view label{}; ///< Names the sampler to the debug tools.
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

/// A stage of a render pipeline that runs a shader's entry point.
enum class ShaderStage : std::uint8_t {
    Vertex,
    Fragment,
    Count,
};

/// A shader to create (Device::createShader): a module of entry points that pipelines run.
struct ShaderDesc {
    std::string_view label{};          ///< Names the shader to the debug tools.
    std::span<const std::byte> code{}; ///< In the device's ShaderFormat; copied by the device.
};

/// What a binding of a bind group holds. Textures and samplers bind apart, as in DirectX 11.
enum class BindingType : std::uint8_t {
    UniformBuffer,
    StorageBuffer,
    SampledTexture,
    Sampler,
};

/// A binding of a bind group layout: its number in the shaders, what it holds, the stages that read it.
struct BindingLayout {
    std::uint32_t binding = 0;
    BindingType type = BindingType::UniformBuffer;
    Flags<ShaderStage> stages{};
};

/// A bind group layout to create (Device::createBindGroupLayout): the bindings of a set of the
/// shaders, which the bind groups of the layout fill.
struct BindGroupLayoutDesc {
    std::string_view label{}; ///< Names the layout to the debug tools.
    std::span<const BindingLayout> bindings{};
};

/// How a vertex attribute is stored in its buffer.
enum class VertexFormat : std::uint8_t {
    Float,    ///< One float (a float in shaders).
    Float2,   ///< Two floats (a float2 in shaders).
    Float3,   ///< Three floats.
    Float4,   ///< Four floats.
    Unorm8x4, ///< Four bytes read as values from 0 to 1.
    Uint8x4,  ///< Four bytes read as unsigned integers (a uint4 in shaders).
    Uint32,   ///< An unsigned integer (a uint in shaders).
};

/// An attribute of a vertex: the shader's input `location`, read in `format` at `offset` bytes from the
/// vertex's start.
struct VertexAttribute {
    std::uint32_t location = 0;
    VertexFormat format = VertexFormat::Float3;
    std::uint32_t offset = 0;
};

/// The vertices of one vertex buffer slot.
struct VertexBufferLayout {
    std::uint32_t stride = 0; ///< In bytes, from a vertex to the next.
    std::span<const VertexAttribute> attributes{};
};

/// How a render pipeline joins its vertices into triangles.
enum class PrimitiveTopology : std::uint8_t {
    TriangleList,  ///< Every three vertices a triangle.
    TriangleStrip, ///< Each vertex a triangle with the two before it.
};

/// Which triangles a render pipeline leaves out.
enum class CullMode : std::uint8_t {
    None,  ///< None.
    Front, ///< Those facing the viewer (FrontFace).
    Back,  ///< Those facing away.
};

/// Which winding faces the viewer.
enum class FrontFace : std::uint8_t {
    CounterClockwise,
    Clockwise,
};

/// When a depth test passes: the fragment's depth compared to the one stored.
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

/// What a color is multiplied by when it blends (BlendState).
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

/// A color attachment a render pipeline draws to: its format, and how it blends.
struct ColorTarget {
    Format format = Format::Undefined;
    std::optional<BlendState> blend{}; ///< None: the fragment replaces what is there.
};

/// How far a pipeline moves its fragments' depth before the test, so that what lies on a surface
/// drawn before (a decal on a wall) is not hidden by it: `constant` times the depth format's least
/// step, plus `slope` times the triangle's steepest depth change across a pixel. Positive values
/// raise the depth.
struct DepthBias {
    float constant = 0.0F;
    float slope = 0.0F;
};

/// The depth attachment of a render pipeline and its test.
struct DepthState {
    Format format = Format::Depth32Float;
    bool isWriteEnabled = true; ///< A fragment that passes stores its depth.
    CompareOp compare = CompareOp::Less;
    std::optional<DepthBias> bias{}; ///< None: fragments keep their depth.
};

/// What an index buffer's indices are.
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

/// The color a render pass clears a color attachment to, each channel from 0 to 1.
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
    Extent2d size{};

    [[nodiscard]] inline auto operator==(const Rect&) const noexcept -> bool = default;
};

/// The kind of GPU an adapter is.
enum class AdapterKind : std::uint8_t {
    Discrete,
    Integrated,
    Virtual,
    Cpu, ///< A software renderer, such as lavapipe in CI.
    Other,
};

/// What a device runs on.
struct AdapterInfo {
    std::string name{}; ///< The GPU's, as its driver reports it.
    AdapterKind kind = AdapterKind::Other;
    std::string driver{}; ///< The driver's name and version, as it reports them.
};

} // namespace nx::rhi
