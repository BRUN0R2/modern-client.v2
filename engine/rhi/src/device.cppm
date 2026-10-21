// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The device, its resources, its swapchain and the command lists of a frame
/// (docs/05-rendering.md, section 3): what the renderer records against, whatever the backend.
///
/// Clip space is DirectX's whatever the backend: x to the right, y up, depth from 0 (near) to 1
/// (far); a viewport's rectangle counts from the top-left corner of its target.
export module nx.rhi:device;

import std;
import nx.core;
import :types;
import :handles;

export namespace nx::rhi {

/// A range of a buffer bound to a binding.
struct BufferBinding {
    BufferHandle buffer{};
    std::uint64_t offset = 0;
    std::uint64_t size = 0; ///< 0: from the offset to the end of the buffer.
};

/// What a binding of a bind group holds: as its layout's BindingType says.
using BindingResource = std::variant<BufferBinding, TextureHandle, SamplerHandle>;

struct Binding {
    std::uint32_t binding = 0;
    BindingResource resource{};
};

struct BindGroupDesc {
    std::string_view label{};
    BindGroupLayoutHandle layout{};
    std::span<const Binding> bindings{}; ///< One per binding of the layout.
};

/// A shader's entry point: the function of the stage in the shader's code.
struct ShaderEntry {
    ShaderHandle shader{};
    std::string_view entryPoint{};
};

struct RenderPipelineDesc {
    std::string_view label{};
    ShaderEntry vertex{};
    ShaderEntry fragment{};
    std::span<const VertexBufferLayout> vertexBuffers{}; ///< Slot by slot.
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    CullMode cullMode = CullMode::None;
    FrontFace frontFace = FrontFace::CounterClockwise;
    std::span<const ColorTarget> colorTargets{};
    std::optional<DepthState> depth{};                         ///< None: no depth test, no depth attachment.
    std::span<const BindGroupLayoutHandle> bindGroupLayouts{}; ///< Bind group 0, 1...
};

/// A color attachment of a render pass.
struct ColorAttachment {
    TextureHandle texture{};
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::Store;
    ClearColor clear{};
};

/// The depth attachment of a render pass.
struct DepthAttachment {
    TextureHandle texture{};
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::DontCare;
    float clear = 1.0F; ///< The far plane.
};

/// The attachments a render pass draws to, and the area it draws in.
struct RenderingDesc {
    std::span<const ColorAttachment> colors{};
    std::optional<DepthAttachment> depth{};
    Extent2d area{};
};

/// A copy between a buffer and a mip level of a texture.
struct TextureCopy {
    BufferHandle buffer{};
    std::uint64_t bufferOffset = 0;
    std::uint32_t bytesPerRow = 0; ///< 0: the rows are packed, extent.width texels each.
    TextureHandle texture{};
    std::uint32_t mipLevel = 0;
    Extent2d extent{};
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
    /// The same for a buffer.
    virtual void transition(BufferHandle buffer, BufferState before, BufferState after) = 0;

    /// Copies `size` bytes; the source must be in CopySource, the destination in CopyDestination.
    virtual void copyBuffer(BufferHandle source, std::uint64_t sourceOffset, BufferHandle destination,
                            std::uint64_t destinationOffset, std::uint64_t size) = 0;
    /// The texture must be in CopyDestination and the buffer in CopySource.
    virtual void copyBufferToTexture(const TextureCopy& copy) = 0;
    /// The texture must be in CopySource and the buffer in CopyDestination.
    virtual void copyTextureToBuffer(const TextureCopy& copy) = 0;

    /// Starts a render pass: its color attachments must be in RenderTarget, its depth attachment in
    /// DepthTarget. The viewport and the scissor then cover the whole area.
    virtual void beginRendering(const RenderingDesc& desc) = 0;
    virtual void endRendering() = 0;

    virtual void setPipeline(RenderPipelineHandle pipeline) = 0;
    /// Binds `group` as bind group `index` of the pipeline set last.
    virtual void setBindGroup(std::uint32_t index, BindGroupHandle group) = 0;
    virtual void setVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset) = 0;
    virtual void setIndexBuffer(BufferHandle buffer, std::uint64_t offset, IndexFormat format) = 0;
    /// The part of the targets clip space maps to, with depths from 0 to 1.
    virtual void setViewport(const Rect& area) = 0;
    /// The part of the targets fragments may be drawn to.
    virtual void setScissor(const Rect& area) = 0;
    virtual void draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex,
                      std::uint32_t firstInstance) = 0;
    virtual void drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex,
                             std::int32_t vertexOffset, std::uint32_t firstInstance) = 0;
};

struct SwapchainDesc {
    Extent2d size{}; ///< The window's drawable size; the surface may impose its own.
    PresentMode presentMode = PresentMode::Fifo;
};

/// The image of the frame: draw to it, then leave it in TextureState::Present.
struct SwapchainImage {
    TextureHandle texture{}; ///< TextureState::Undefined when acquired: its old content is gone.
    Extent2d size{};
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

/// A GPU, its resources and the frames it runs: up to a backend's frames in flight are recorded
/// while the GPU runs the ones before. Use it from one thread: the render thread.
///
/// A resource lives until it is destroyed (Device::destroy), which happens once the GPU has
/// finished every frame that may use it; a resource left at the device's end is destroyed with it.
class Device {
public:
    Device() = default;
    Device(const Device&) = delete;
    Device(Device&&) = delete;
    auto operator=(const Device&) -> Device& = delete;
    auto operator=(Device&&) -> Device& = delete;
    virtual ~Device() = default;

    [[nodiscard]] virtual auto adapter() const noexcept -> const AdapterInfo& = 0;
    /// What the code of ShaderDesc must be.
    [[nodiscard]] virtual auto shaderFormat() const noexcept -> ShaderFormat = 0;

    /// Errors (every create): OutOfHostMemory, OutOfDeviceMemory, DriverRefused.
    [[nodiscard]] virtual auto createBuffer(const BufferDesc& desc) -> Result<BufferHandle, RhiError> = 0;
    /// Errors: also UnsupportedFormat.
    [[nodiscard]] virtual auto createTexture(const TextureDesc& desc) -> Result<TextureHandle, RhiError> = 0;
    [[nodiscard]] virtual auto createSampler(const SamplerDesc& desc) -> Result<SamplerHandle, RhiError> = 0;
    /// Errors: also InvalidShader.
    [[nodiscard]] virtual auto createShader(const ShaderDesc& desc) -> Result<ShaderHandle, RhiError> = 0;
    [[nodiscard]] virtual auto createBindGroupLayout(const BindGroupLayoutDesc& desc)
        -> Result<BindGroupLayoutHandle, RhiError> = 0;
    [[nodiscard]] virtual auto createBindGroup(const BindGroupDesc& desc)
        -> Result<BindGroupHandle, RhiError> = 0;
    /// Errors: also InvalidShader (an entry point the shader does not have).
    [[nodiscard]] virtual auto createRenderPipeline(const RenderPipelineDesc& desc)
        -> Result<RenderPipelineHandle, RhiError> = 0;

    virtual void destroy(BufferHandle buffer) = 0;
    virtual void destroy(TextureHandle texture) = 0;
    virtual void destroy(SamplerHandle sampler) = 0;
    virtual void destroy(ShaderHandle shader) = 0;
    virtual void destroy(BindGroupLayoutHandle layout) = 0;
    virtual void destroy(BindGroupHandle group) = 0;
    virtual void destroy(RenderPipelineHandle pipeline) = 0;

    /// The memory of an Upload or Readback buffer, mapped for as long as the buffer lives. Read a
    /// Readback buffer once the frame that wrote it has finished (waitIdle, or frames in flight
    /// later).
    [[nodiscard]] virtual auto mappedBytes(BufferHandle buffer) -> std::span<std::byte> = 0;

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

    /// Waits until the GPU has finished everything submitted.
    virtual void waitIdle() noexcept = 0;

    /// Errors the backend's validation reported (the Vulkan validation layers in development
    /// builds), which the device also logs. Zero without validation.
    [[nodiscard]] virtual auto validationErrors() const noexcept -> std::uint64_t = 0;
};

} // namespace nx::rhi
