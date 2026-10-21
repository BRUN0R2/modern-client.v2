// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

#include "vulkanApi.hpp"

module nx.rhi.vulkan;

import std;
import nx.core;
import nx.platform;
import nx.rhi;

namespace nx::rhi::vulkan {

namespace {

constexpr log::Category kLog{"rhi"};
constexpr std::uint32_t kRequiredApiVersion = VK_API_VERSION_1_3;
constexpr std::string_view kValidationLayer = "VK_LAYER_KHRONOS_validation";
constexpr std::uint64_t kNoTimeout = std::numeric_limits<std::uint64_t>::max();
/// The color attachments a render pass may have: what DirectX 11 allows.
constexpr std::size_t kMaxColorAttachments = 8;
/// The first word of every SPIR-V module.
constexpr std::uint32_t kSpirvMagic = 0x07230203;
/// The bind groups one descriptor pool holds; a full pool is followed by another.
constexpr std::uint32_t kBindGroupsPerPool = 1024;

/// Whether a device is open: volk keeps one device's functions.
constinit std::atomic<bool> isDeviceOpen{false};

[[nodiscard]] auto resultName(VkResult result) -> std::string {
    switch (result) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_OUT_OF_POOL_MEMORY:
        return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_FRAGMENTED_POOL:
        return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_INVALID_SHADER_NV:
        return "VK_ERROR_INVALID_SHADER_NV";
    default:
        return std::format("VkResult {}", std::to_underlying(result));
    }
}

[[nodiscard]] auto errorOf(VkResult result) noexcept -> RhiError {
    switch (result) {
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return RhiError::OutOfHostMemory;
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return RhiError::OutOfDeviceMemory;
    case VK_ERROR_DEVICE_LOST:
        return RhiError::DeviceLost;
    case VK_ERROR_SURFACE_LOST_KHR:
        return RhiError::SurfaceLost;
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return RhiError::SurfaceUnavailable;
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return RhiError::BackendUnavailable;
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return RhiError::UnsupportedFormat;
    case VK_ERROR_INVALID_SHADER_NV:
        return RhiError::InvalidShader;
    default:
        return RhiError::DriverRefused;
    }
}

/// The failure of the Vulkan function `call`, which returned `result`.
[[nodiscard]] auto vulkanFailure(VkResult result, std::string_view call,
                                 std::source_location where = std::source_location::current())
    -> std::unexpected<Failure<RhiError>> {
    return fail(errorOf(result), std::format("{}: {}", call, resultName(result)), where);
}

[[nodiscard]] auto vulkanFormatOf(Format format) noexcept -> VkFormat {
    switch (format) {
    case Format::Rgba8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::Bgra8Unorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::Rgba8Srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::Bgra8Srgb:
        return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::Depth32Float:
        return VK_FORMAT_D32_SFLOAT;
    case Format::Undefined:
        break;
    }
    return VK_FORMAT_UNDEFINED;
}

/// The Format of `format`; Undefined for a format the RHI does not name.
[[nodiscard]] auto formatOf(VkFormat format) noexcept -> Format {
    for (const Format candidate : {Format::Rgba8Unorm, Format::Bgra8Unorm, Format::Rgba8Srgb,
                                   Format::Bgra8Srgb, Format::Depth32Float}) {
        if (vulkanFormatOf(candidate) == format) {
            return candidate;
        }
    }
    return Format::Undefined;
}

/// What a texture state means to a barrier: the stages and accesses that use it, and its layout.
struct StateAccess {
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

[[nodiscard]] auto accessOf(TextureState state) noexcept -> StateAccess {
    switch (state) {
    case TextureState::Undefined:
        return {};
    case TextureState::RenderTarget:
        return {.stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    case TextureState::DepthTarget:
        return {.stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL};
    case TextureState::ShaderRead:
        return {.stages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case TextureState::CopySource:
        return {.stages = VK_PIPELINE_STAGE_2_COPY_BIT,
                .access = VK_ACCESS_2_TRANSFER_READ_BIT,
                .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    case TextureState::CopyDestination:
        return {.stages = VK_PIPELINE_STAGE_2_COPY_BIT,
                .access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    case TextureState::Present:
        // The presentation engine waits on the frame's semaphore, which covers every stage.
        return {.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    }
    return {};
}

[[nodiscard]] auto loadOpOf(LoadOp op) noexcept -> VkAttachmentLoadOp {
    switch (op) {
    case LoadOp::Load:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case LoadOp::Clear:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadOp::DontCare:
        break;
    }
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

[[nodiscard]] auto storeOpOf(StoreOp op) noexcept -> VkAttachmentStoreOp {
    return op == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

/// What a buffer state means to a barrier: the stages and accesses that use it.
[[nodiscard]] auto accessOf(BufferState state) noexcept -> StateAccess {
    constexpr VkPipelineStageFlags2 kShaders =
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    switch (state) {
    case BufferState::Undefined:
        return {};
    case BufferState::CopySource:
        return {.stages = VK_PIPELINE_STAGE_2_COPY_BIT, .access = VK_ACCESS_2_TRANSFER_READ_BIT};
    case BufferState::CopyDestination:
        return {.stages = VK_PIPELINE_STAGE_2_COPY_BIT, .access = VK_ACCESS_2_TRANSFER_WRITE_BIT};
    case BufferState::Vertex:
        return {.stages = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
                .access = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT};
    case BufferState::Index:
        return {.stages = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, .access = VK_ACCESS_2_INDEX_READ_BIT};
    case BufferState::Uniform:
        return {.stages = kShaders, .access = VK_ACCESS_2_UNIFORM_READ_BIT};
    case BufferState::ShaderRead:
        return {.stages = kShaders, .access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT};
    case BufferState::HostRead:
        return {.stages = VK_PIPELINE_STAGE_2_HOST_BIT, .access = VK_ACCESS_2_HOST_READ_BIT};
    }
    return {};
}

[[nodiscard]] auto bufferUsageOf(Flags<BufferUsage> usage) noexcept -> VkBufferUsageFlags {
    constexpr std::array kBits{
        std::pair{BufferUsage::Vertex, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT},
        std::pair{BufferUsage::Index, VK_BUFFER_USAGE_INDEX_BUFFER_BIT},
        std::pair{BufferUsage::Uniform, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT},
        std::pair{BufferUsage::Storage, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT},
        std::pair{BufferUsage::CopySource, VK_BUFFER_USAGE_TRANSFER_SRC_BIT},
        std::pair{BufferUsage::CopyDestination, VK_BUFFER_USAGE_TRANSFER_DST_BIT},
    };
    VkBufferUsageFlags flags = 0;
    for (const auto& [use, bit] : kBits) {
        if (usage.test(use)) {
            flags |= static_cast<VkBufferUsageFlags>(bit);
        }
    }
    return flags;
}

[[nodiscard]] auto imageUsageOf(Flags<TextureUsage> usage) noexcept -> VkImageUsageFlags {
    constexpr std::array kBits{
        std::pair{TextureUsage::Sampled, VK_IMAGE_USAGE_SAMPLED_BIT},
        std::pair{TextureUsage::RenderTarget, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT},
        std::pair{TextureUsage::DepthTarget, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT},
        std::pair{TextureUsage::CopySource, VK_IMAGE_USAGE_TRANSFER_SRC_BIT},
        std::pair{TextureUsage::CopyDestination, VK_IMAGE_USAGE_TRANSFER_DST_BIT},
    };
    VkImageUsageFlags flags = 0;
    for (const auto& [use, bit] : kBits) {
        if (usage.test(use)) {
            flags |= static_cast<VkImageUsageFlags>(bit);
        }
    }
    return flags;
}

[[nodiscard]] auto aspectOf(Format format) noexcept -> VkImageAspectFlags {
    return format == Format::Depth32Float ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

[[nodiscard]] auto filterOf(Filter filter) noexcept -> VkFilter {
    return filter == Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

[[nodiscard]] auto mipmapModeOf(Filter filter) noexcept -> VkSamplerMipmapMode {
    return filter == Filter::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

[[nodiscard]] auto addressModeOf(AddressMode mode) noexcept -> VkSamplerAddressMode {
    return mode == AddressMode::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                                       : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

[[nodiscard]] auto descriptorTypeOf(BindingType type) noexcept -> VkDescriptorType {
    switch (type) {
    case BindingType::UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case BindingType::StorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case BindingType::SampledTexture:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case BindingType::Sampler:
        break;
    }
    return VK_DESCRIPTOR_TYPE_SAMPLER;
}

[[nodiscard]] auto stageFlagsOf(Flags<ShaderStage> stages) noexcept -> VkShaderStageFlags {
    VkShaderStageFlags flags = 0;
    if (stages.test(ShaderStage::Vertex)) {
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (stages.test(ShaderStage::Fragment)) {
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    return flags;
}

[[nodiscard]] auto vertexFormatOf(VertexFormat format) noexcept -> VkFormat {
    switch (format) {
    case VertexFormat::Float2:
        return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::Uint8x4:
        return VK_FORMAT_R8G8B8A8_UINT;
    case VertexFormat::Unorm8x4:
        break;
    }
    return VK_FORMAT_R8G8B8A8_UNORM;
}

[[nodiscard]] auto cullModeOf(CullMode mode) noexcept -> VkCullModeFlags {
    switch (mode) {
    case CullMode::None:
        return VK_CULL_MODE_NONE;
    case CullMode::Front:
        return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back:
        break;
    }
    return VK_CULL_MODE_BACK_BIT;
}

[[nodiscard]] auto compareOpOf(CompareOp op) noexcept -> VkCompareOp {
    switch (op) {
    case CompareOp::Never:
        return VK_COMPARE_OP_NEVER;
    case CompareOp::Less:
        return VK_COMPARE_OP_LESS;
    case CompareOp::Equal:
        return VK_COMPARE_OP_EQUAL;
    case CompareOp::LessOrEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater:
        return VK_COMPARE_OP_GREATER;
    case CompareOp::NotEqual:
        return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::GreaterOrEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always:
        break;
    }
    return VK_COMPARE_OP_ALWAYS;
}

[[nodiscard]] auto blendFactorOf(BlendFactor factor) noexcept -> VkBlendFactor {
    switch (factor) {
    case BlendFactor::Zero:
        return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One:
        return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SourceAlpha:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::OneMinusSourceAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::SourceColor:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::DestinationColor:
        break;
    }
    return VK_BLEND_FACTOR_DST_COLOR;
}

/// The handle of a Vulkan object as the debug utilities name it.
template <class Object>
[[nodiscard]] auto objectHandleOf(Object object) noexcept -> std::uint64_t {
    static_assert(sizeof(Object) == sizeof(std::uint64_t),
                  "a 64-bit build: every Vulkan handle is a pointer");
    return std::bit_cast<std::uint64_t>(object);
}

[[nodiscard]] auto presentModeOf(PresentMode mode) noexcept -> VkPresentModeKHR {
    switch (mode) {
    case PresentMode::Mailbox:
        return VK_PRESENT_MODE_MAILBOX_KHR;
    case PresentMode::Immediate:
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    case PresentMode::Fifo:
        break;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

[[nodiscard]] auto adapterKindOf(VkPhysicalDeviceType type) noexcept -> AdapterKind {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return AdapterKind::Discrete;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return AdapterKind::Integrated;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return AdapterKind::Virtual;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return AdapterKind::Cpu;
    default:
        return AdapterKind::Other;
    }
}

/// Which adapter wins: the higher rank.
[[nodiscard]] auto rankOf(AdapterKind kind) noexcept -> int {
    switch (kind) {
    case AdapterKind::Discrete:
        return 4;
    case AdapterKind::Integrated:
        return 3;
    case AdapterKind::Virtual:
        return 2;
    case AdapterKind::Cpu:
        return 1;
    case AdapterKind::Other:
        break;
    }
    return 0;
}

/// The text of a fixed-size name field of the API: up to its first zero.
[[nodiscard]] auto textOf(const char* name, std::size_t size) -> std::string_view {
    const std::string_view field{name, size};
    return field.substr(0, field.find('\0'));
}

[[nodiscard]] auto hasName(std::span<const VkExtensionProperties> extensions, std::string_view name) -> bool {
    return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) {
        return textOf(extension.extensionName, VK_MAX_EXTENSION_NAME_SIZE) == name;
    });
}

/// Calls a Vulkan enumeration twice: for the count, then for the values.
template <class Value, class Enumerate>
[[nodiscard]] auto enumerate(Enumerate&& call) -> Result<std::vector<Value>, RhiError> {
    std::uint32_t count = 0;
    if (const VkResult result = call(&count, nullptr); result != VK_SUCCESS) {
        return vulkanFailure(result, "enumerating");
    }
    std::vector<Value> values(count);
    if (const VkResult result = call(&count, values.data());
        result != VK_SUCCESS && result != VK_INCOMPLETE) {
        return vulkanFailure(result, "enumerating");
    }
    values.resize(count);
    return values;
}

/// The instance extensions that make a surface for `window`.
[[nodiscard]] auto surfaceExtensionOf(const platform::NativeWindow& window) noexcept -> std::string_view {
    return std::visit(
        [](const auto& native) -> std::string_view {
            using Native = std::decay_t<decltype(native)>;
            if constexpr (std::same_as<Native, platform::Win32Window>) {
                return "VK_KHR_win32_surface";
            } else if constexpr (std::same_as<Native, platform::XlibWindow>) {
                return "VK_KHR_xlib_surface";
            } else {
                return "VK_KHR_wayland_surface";
            }
        },
        window);
}

class VulkanDevice;

struct VulkanBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    std::uint64_t size = 0;
    std::span<std::byte> mapped{}; ///< Upload and Readback buffers only.
};

/// A texture the device knows: its image and the view passes draw to and shaders sample.
struct VulkanTexture {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE; ///< None for a swapchain's image, which the swapchain owns.
    Format format = Format::Undefined;
    Extent2d extent;
    std::uint32_t mipLevels = 1;
    std::uint32_t arrayLayers = 1;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
};

struct VulkanSampler {
    VkSampler sampler = VK_NULL_HANDLE;
};

struct VulkanShader {
    VkShaderModule module = VK_NULL_HANDLE;
};

struct VulkanBindGroupLayout {
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    std::vector<BindingLayout> bindings;
};

struct VulkanBindGroup {
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE; ///< The pool it came from, which takes it back.
};

struct VulkanPipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

/// A destroyed resource the GPU may still use, kept until the frame `afterValue` finishes.
struct RetiredResource {
    using Resource = std::variant<VulkanBuffer, VulkanTexture, VulkanSampler, VulkanShader,
                                  VulkanBindGroupLayout, VulkanBindGroup, VulkanPipeline>;

    std::uint64_t afterValue = 0;
    Resource resource;
};

class VulkanCommandList final : public CommandList {
public:
    explicit VulkanCommandList(VulkanDevice& device) noexcept : m_device{&device} {}

    void startRecording(VkCommandBuffer commands) noexcept {
        m_commands = commands;
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    void transition(TextureHandle texture, TextureState before, TextureState after) override;
    void transition(BufferHandle buffer, BufferState before, BufferState after) override;
    void copyBuffer(BufferHandle source, std::uint64_t sourceOffset, BufferHandle destination,
                    std::uint64_t destinationOffset, std::uint64_t size) override;
    void copyBufferToTexture(const TextureCopy& copy) override;
    void copyTextureToBuffer(const TextureCopy& copy) override;
    void beginRendering(const RenderingDesc& desc) override;
    void endRendering() override;
    void setPipeline(RenderPipelineHandle pipeline) override;
    void setBindGroup(std::uint32_t index, BindGroupHandle group) override;
    void setVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset) override;
    void setIndexBuffer(BufferHandle buffer, std::uint64_t offset, IndexFormat format) override;
    void setViewport(const Rect& area) override;
    void setScissor(const Rect& area) override;
    void draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex,
              std::uint32_t firstInstance) override;
    void drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex,
                     std::int32_t vertexOffset, std::uint32_t firstInstance) override;

private:
    /// The region of a copy between `copy`'s buffer and texture.
    [[nodiscard]] auto copyRegionOf(const TextureCopy& copy) -> VkBufferImageCopy;

    VulkanDevice* m_device;
    VkCommandBuffer m_commands = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE; ///< The layout of the pipeline set last.
};

/// What one frame in flight owns.
struct Frame {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkSemaphore imageAcquired = VK_NULL_HANDLE; ///< Signaled when its swapchain image is ready to draw.
    std::uint64_t submitted = 0;                ///< The timeline value of its last submission.
    bool hasAcquiredImage = false;
};

class VulkanSwapchain;

class VulkanDevice final : public Device {
public:
    VulkanDevice() = default;
    ~VulkanDevice() override;

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice(VulkanDevice&&) = delete;
    auto operator=(const VulkanDevice&) -> VulkanDevice& = delete;
    auto operator=(VulkanDevice&&) -> VulkanDevice& = delete;

    [[nodiscard]] static auto create(const DeviceDesc& desc)
        -> Result<std::unique_ptr<VulkanDevice>, RhiError>;

    [[nodiscard]] auto adapter() const noexcept -> const AdapterInfo& override { return m_adapter; }
    [[nodiscard]] auto shaderFormat() const noexcept -> ShaderFormat override { return ShaderFormat::Spirv; }
    [[nodiscard]] auto createBuffer(const BufferDesc& desc) -> Result<BufferHandle, RhiError> override;
    [[nodiscard]] auto createTexture(const TextureDesc& desc) -> Result<TextureHandle, RhiError> override;
    [[nodiscard]] auto createSampler(const SamplerDesc& desc) -> Result<SamplerHandle, RhiError> override;
    [[nodiscard]] auto createShader(const ShaderDesc& desc) -> Result<ShaderHandle, RhiError> override;
    [[nodiscard]] auto createBindGroupLayout(const BindGroupLayoutDesc& desc)
        -> Result<BindGroupLayoutHandle, RhiError> override;
    [[nodiscard]] auto createBindGroup(const BindGroupDesc& desc)
        -> Result<BindGroupHandle, RhiError> override;
    [[nodiscard]] auto createRenderPipeline(const RenderPipelineDesc& desc)
        -> Result<RenderPipelineHandle, RhiError> override;
    void destroy(BufferHandle buffer) override;
    void destroy(TextureHandle texture) override;
    void destroy(SamplerHandle sampler) override;
    void destroy(ShaderHandle shader) override;
    void destroy(BindGroupLayoutHandle layout) override;
    void destroy(BindGroupHandle group) override;
    void destroy(RenderPipelineHandle pipeline) override;
    [[nodiscard]] auto mappedBytes(BufferHandle buffer) -> std::span<std::byte> override;
    [[nodiscard]] auto createSwapchain(const SwapchainDesc& desc)
        -> Result<std::unique_ptr<Swapchain>, RhiError> override;
    [[nodiscard]] auto beginFrame() -> Result<std::reference_wrapper<CommandList>, RhiError> override;
    [[nodiscard]] auto submitFrame() -> Result<void, RhiError> override;
    [[nodiscard]] auto submitFrame(Swapchain& presentTo) -> Result<void, RhiError> override;
    void waitIdle() noexcept override;
    [[nodiscard]] auto validationErrors() const noexcept -> std::uint64_t override {
        return m_validationErrors.load();
    }

    [[nodiscard]] auto physicalDevice() const noexcept -> VkPhysicalDevice { return m_physicalDevice; }
    [[nodiscard]] auto handle() const noexcept -> VkDevice { return m_device; }
    [[nodiscard]] auto surface() const noexcept -> VkSurfaceKHR { return m_surface; }
    [[nodiscard]] auto queue() const noexcept -> VkQueue { return m_queue; }
    [[nodiscard]] auto textures() noexcept -> HandlePool<TextureTag, VulkanTexture>& { return m_textures; }
    [[nodiscard]] auto buffers() noexcept -> HandlePool<BufferTag, VulkanBuffer>& { return m_buffers; }
    [[nodiscard]] auto bindGroups() noexcept -> HandlePool<BindGroupTag, VulkanBindGroup>& {
        return m_bindGroups;
    }
    [[nodiscard]] auto pipelines() noexcept -> HandlePool<RenderPipelineTag, VulkanPipeline>& {
        return m_pipelines;
    }
    [[nodiscard]] auto isRecording() const noexcept -> bool { return m_isRecording; }
    [[nodiscard]] auto currentFrame() noexcept -> Frame& { return m_frames[m_frameIndex]; }

    void swapchainOpened() noexcept { ++m_openSwapchains; }
    void swapchainClosed() noexcept { --m_openSwapchains; }

    void countValidationError() noexcept { ++m_validationErrors; }

private:
    [[nodiscard]] auto createInstance(const DeviceDesc& desc) -> Result<void, RhiError>;
    [[nodiscard]] auto createSurface(const platform::NativeWindow& window) -> Result<void, RhiError>;
    [[nodiscard]] auto chooseAdapter() -> Result<void, RhiError>;
    [[nodiscard]] auto createLogicalDevice() -> Result<void, RhiError>;
    [[nodiscard]] auto createFrames(std::uint32_t count) -> Result<void, RhiError>;
    [[nodiscard]] auto createAllocator() -> Result<void, RhiError>;
    [[nodiscard]] auto submit(VulkanSwapchain* presentTo) -> Result<void, RhiError>;

    /// Names `object` for the debug tools, when the validation layers listen.
    void name(VkObjectType type, std::uint64_t object, std::string_view label) const;
    /// A descriptor set of `layout` from a pool with room, adding a pool when every pool is full.
    [[nodiscard]] auto allocateDescriptorSet(VkDescriptorSetLayout layout)
        -> Result<VulkanBindGroup, RhiError>;
    /// Keeps `resource` until the GPU has finished every frame that may use it.
    void retire(RetiredResource::Resource resource);
    /// Destroys the retired resources the GPU has finished with.
    void destroyFinished() noexcept;
    void destroyNow(RetiredResource::Resource& resource) noexcept;

    bool m_ownsDeviceSlot = false;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    std::uint32_t m_queueFamily = 0;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkSemaphore m_timeline = VK_NULL_HANDLE; ///< Counts the submitted frames the GPU has finished.
    std::uint64_t m_timelineValue = 0;       ///< The value of the last submission.
    std::vector<Frame> m_frames;
    std::size_t m_frameIndex = 0;
    bool m_isRecording = false;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> m_descriptorPools; ///< The last one has room, or the next set adds one.
    bool m_hasAnisotropy = false;
    float m_maxAnisotropy = 1.0F;
    HandlePool<BufferTag, VulkanBuffer> m_buffers;
    HandlePool<TextureTag, VulkanTexture> m_textures;
    HandlePool<SamplerTag, VulkanSampler> m_samplers;
    HandlePool<ShaderTag, VulkanShader> m_shaders;
    HandlePool<BindGroupLayoutTag, VulkanBindGroupLayout> m_bindGroupLayouts;
    HandlePool<BindGroupTag, VulkanBindGroup> m_bindGroups;
    HandlePool<RenderPipelineTag, VulkanPipeline> m_pipelines;
    std::vector<RetiredResource> m_retired;
    VulkanCommandList m_commandList{*this};
    AdapterInfo m_adapter;
    int m_openSwapchains = 0;
    std::atomic<std::uint64_t> m_validationErrors{0};
};

class VulkanSwapchain final : public Swapchain {
public:
    explicit VulkanSwapchain(VulkanDevice& device) noexcept : m_device{&device} { device.swapchainOpened(); }
    ~VulkanSwapchain() override;

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain(VulkanSwapchain&&) = delete;
    auto operator=(const VulkanSwapchain&) -> VulkanSwapchain& = delete;
    auto operator=(VulkanSwapchain&&) -> VulkanSwapchain& = delete;

    [[nodiscard]] auto acquire() -> Result<std::optional<SwapchainImage>, RhiError> override;
    [[nodiscard]] auto resize(Extent2d size) -> Result<void, RhiError> override;

    [[nodiscard]] auto device() const noexcept -> const VulkanDevice* { return m_device; }
    [[nodiscard]] auto build(const SwapchainDesc& desc) -> Result<void, RhiError>;

    /// The image acquired this frame, which the frame's submission presents; forgets it.
    [[nodiscard]] auto takeAcquired() noexcept -> std::optional<std::uint32_t> {
        return std::exchange(m_acquired, std::nullopt);
    }
    [[nodiscard]] auto renderFinished(std::uint32_t image) const noexcept -> VkSemaphore {
        return m_renderFinished[image];
    }
    [[nodiscard]] auto present(std::uint32_t image) -> Result<void, RhiError>;

private:
    [[nodiscard]] auto rebuild() -> Result<void, RhiError>;
    void destroyImages() noexcept;

    VulkanDevice* m_device;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    std::vector<TextureHandle> m_textures;
    std::vector<VkSemaphore> m_renderFinished; ///< One per image: what its presentation waits for.
    Format m_format = Format::Undefined;
    Extent2d m_extent;
    Extent2d m_requestedSize;
    PresentMode m_presentMode = PresentMode::Fifo;
    bool m_isOutOfDate = false;
    std::optional<std::uint32_t> m_acquired;
};

VKAPI_ATTR auto VKAPI_CALL onValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                               VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                               const VkDebugUtilsMessengerCallbackDataEXT* data, void* device)
    -> VkBool32 {
    const std::string_view message = data != nullptr && data->pMessage != nullptr ? data->pMessage : "";
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        static_cast<VulkanDevice*>(device)->countValidationError();
        log::error(kLog, "Vulkan validation: {}", message);
    } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
        log::warning(kLog, "Vulkan validation: {}", message);
    } else {
        log::debug(kLog, "Vulkan validation: {}", message);
    }
    return VK_FALSE; // the call goes on, as without validation
}

// --- VulkanCommandList ---------------------------------------------------------------------------

void VulkanCommandList::transition(TextureHandle texture, TextureState before, TextureState after) {
    expects(after != TextureState::Undefined, "CommandList::transition: a texture cannot become Undefined");
    const VulkanTexture& target = m_device->textures().get(texture);
    const StateAccess from = accessOf(before);
    const StateAccess to = accessOf(after);
    const VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = from.stages,
        .srcAccessMask = from.access,
        .dstStageMask = to.stages,
        .dstAccessMask = to.access,
        .oldLayout = from.layout,
        .newLayout = to.layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = target.image,
        .subresourceRange = {.aspectMask = target.aspect,
                             .baseMipLevel = 0,
                             .levelCount = VK_REMAINING_MIP_LEVELS,
                             .baseArrayLayer = 0,
                             .layerCount = VK_REMAINING_ARRAY_LAYERS},
    };
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = nullptr,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = nullptr,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(m_commands, &dependency);
}

void VulkanCommandList::beginRendering(const RenderingDesc& desc) {
    expects(desc.colors.size() <= kMaxColorAttachments,
            "CommandList::beginRendering: at most 8 color attachments");
    std::array<VkRenderingAttachmentInfo, kMaxColorAttachments> attachments{};
    for (std::size_t index = 0; index < desc.colors.size(); ++index) {
        const ColorAttachment& color = desc.colors[index];
        VkRenderingAttachmentInfo& attachment = attachments.at(index);
        attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attachment.imageView = m_device->textures().get(color.texture).view;
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.resolveMode = VK_RESOLVE_MODE_NONE;
        attachment.loadOp = loadOpOf(color.load);
        attachment.storeOp = storeOpOf(color.store);
        attachment.clearValue.color.float32[0] = color.clear.red;
        attachment.clearValue.color.float32[1] = color.clear.green;
        attachment.clearValue.color.float32[2] = color.clear.blue;
        attachment.clearValue.color.float32[3] = color.clear.alpha;
    }
    VkRenderingAttachmentInfo depth{};
    if (desc.depth) {
        depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView = m_device->textures().get(desc.depth->texture).view;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth.resolveMode = VK_RESOLVE_MODE_NONE;
        depth.loadOp = loadOpOf(desc.depth->load);
        depth.storeOp = storeOpOf(desc.depth->store);
        depth.clearValue.depthStencil.depth = desc.depth->clear;
    }
    const VkRenderingInfo rendering{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderArea = {.offset = {.x = 0, .y = 0},
                       .extent = {.width = desc.area.width, .height = desc.area.height}},
        .layerCount = 1,
        .viewMask = 0,
        .colorAttachmentCount = narrow<std::uint32_t>(desc.colors.size()),
        .pColorAttachments = attachments.data(),
        .pDepthAttachment = desc.depth ? &depth : nullptr,
        .pStencilAttachment = nullptr,
    };
    vkCmdBeginRendering(m_commands, &rendering);
    const Rect whole{.x = 0, .y = 0, .size = desc.area};
    setViewport(whole);
    setScissor(whole);
}

void VulkanCommandList::endRendering() {
    vkCmdEndRendering(m_commands);
}

void VulkanCommandList::transition(BufferHandle buffer, BufferState before, BufferState after) {
    expects(after != BufferState::Undefined, "CommandList::transition: a buffer cannot become Undefined");
    const StateAccess from = accessOf(before);
    const StateAccess to = accessOf(after);
    const VkBufferMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = from.stages,
        .srcAccessMask = from.access,
        .dstStageMask = to.stages,
        .dstAccessMask = to.access,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = m_device->buffers().get(buffer).buffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = nullptr,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &barrier,
        .imageMemoryBarrierCount = 0,
        .pImageMemoryBarriers = nullptr,
    };
    vkCmdPipelineBarrier2(m_commands, &dependency);
}

void VulkanCommandList::copyBuffer(BufferHandle source, std::uint64_t sourceOffset, BufferHandle destination,
                                   std::uint64_t destinationOffset, std::uint64_t size) {
    const VkBufferCopy region{.srcOffset = sourceOffset, .dstOffset = destinationOffset, .size = size};
    vkCmdCopyBuffer(m_commands, m_device->buffers().get(source).buffer,
                    m_device->buffers().get(destination).buffer, 1, &region);
}

auto VulkanCommandList::copyRegionOf(const TextureCopy& copy) -> VkBufferImageCopy {
    const VulkanTexture& texture = m_device->textures().get(copy.texture);
    const std::uint32_t texelBytes = bytesPerTexel(texture.format);
    expects(copy.bytesPerRow % texelBytes == 0, "CommandList: a row of a texture copy is whole texels");
    expects(copy.mipLevel < texture.mipLevels && copy.arrayLayer < texture.arrayLayers,
            "CommandList: a texture copy names one of the texture's mip levels and layers");
    return VkBufferImageCopy{
        .bufferOffset = copy.bufferOffset,
        .bufferRowLength = copy.bytesPerRow / texelBytes, // 0: packed rows
        .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = texture.aspect,
                             .mipLevel = copy.mipLevel,
                             .baseArrayLayer = copy.arrayLayer,
                             .layerCount = 1},
        .imageOffset = {.x = 0, .y = 0, .z = 0},
        .imageExtent = {.width = copy.extent.width, .height = copy.extent.height, .depth = 1},
    };
}

void VulkanCommandList::copyBufferToTexture(const TextureCopy& copy) {
    const VkBufferImageCopy region = copyRegionOf(copy);
    vkCmdCopyBufferToImage(m_commands, m_device->buffers().get(copy.buffer).buffer,
                           m_device->textures().get(copy.texture).image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);
}

void VulkanCommandList::copyTextureToBuffer(const TextureCopy& copy) {
    const VkBufferImageCopy region = copyRegionOf(copy);
    vkCmdCopyImageToBuffer(m_commands, m_device->textures().get(copy.texture).image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_device->buffers().get(copy.buffer).buffer,
                           1, &region);
}

void VulkanCommandList::setPipeline(RenderPipelineHandle pipeline) {
    const VulkanPipeline& bound = m_device->pipelines().get(pipeline);
    vkCmdBindPipeline(m_commands, VK_PIPELINE_BIND_POINT_GRAPHICS, bound.pipeline);
    m_pipelineLayout = bound.layout;
}

void VulkanCommandList::setBindGroup(std::uint32_t index, BindGroupHandle group) {
    expects(m_pipelineLayout != VK_NULL_HANDLE, "CommandList::setBindGroup: after setPipeline");
    const VkDescriptorSet set = m_device->bindGroups().get(group).set;
    vkCmdBindDescriptorSets(m_commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, index, 1, &set, 0,
                            nullptr);
}

void VulkanCommandList::setVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset) {
    const VkBuffer bound = m_device->buffers().get(buffer).buffer;
    vkCmdBindVertexBuffers(m_commands, slot, 1, &bound, &offset);
}

void VulkanCommandList::setIndexBuffer(BufferHandle buffer, std::uint64_t offset, IndexFormat format) {
    vkCmdBindIndexBuffer(m_commands, m_device->buffers().get(buffer).buffer, offset,
                         format == IndexFormat::Uint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}

void VulkanCommandList::setViewport(const Rect& area) {
    // A negative height turns Vulkan's clip space, y down, into the RHI's, y up (Vulkan 1.1).
    const VkViewport viewport{.x = static_cast<float>(area.x),
                              .y = static_cast<float>(area.y) + static_cast<float>(area.size.height),
                              .width = static_cast<float>(area.size.width),
                              .height = -static_cast<float>(area.size.height),
                              .minDepth = 0.0F,
                              .maxDepth = 1.0F};
    vkCmdSetViewport(m_commands, 0, 1, &viewport);
}

void VulkanCommandList::setScissor(const Rect& area) {
    const VkRect2D scissor{.offset = {.x = area.x, .y = area.y},
                           .extent = {.width = area.size.width, .height = area.size.height}};
    vkCmdSetScissor(m_commands, 0, 1, &scissor);
}

void VulkanCommandList::draw(std::uint32_t vertexCount, std::uint32_t instanceCount,
                             std::uint32_t firstVertex, std::uint32_t firstInstance) {
    vkCmdDraw(m_commands, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandList::drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount,
                                    std::uint32_t firstIndex, std::int32_t vertexOffset,
                                    std::uint32_t firstInstance) {
    vkCmdDrawIndexed(m_commands, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

// --- VulkanDevice --------------------------------------------------------------------------------

auto VulkanDevice::create(const DeviceDesc& desc) -> Result<std::unique_ptr<VulkanDevice>, RhiError> {
    expects(desc.framesInFlight >= 2 && desc.framesInFlight <= kMaxFramesInFlight,
            "vulkan::createDevice: 2 or 3 frames in flight");
    expects(!isDeviceOpen.exchange(true), "vulkan::createDevice: one device at a time");
    auto device = std::make_unique<VulkanDevice>();
    device->m_ownsDeviceSlot = true;

    if (auto instance = device->createInstance(desc); !instance) {
        return std::unexpected{std::move(instance.error())};
    }
    if (desc.window) {
        if (auto surface = device->createSurface(*desc.window); !surface) {
            return std::unexpected{std::move(surface.error())};
        }
    }
    if (auto chosen = device->chooseAdapter(); !chosen) {
        return std::unexpected{std::move(chosen.error())};
    }
    if (auto logical = device->createLogicalDevice(); !logical) {
        return std::unexpected{std::move(logical.error())};
    }
    if (auto allocator = device->createAllocator(); !allocator) {
        return std::unexpected{std::move(allocator.error())};
    }
    if (auto frames = device->createFrames(desc.framesInFlight); !frames) {
        return std::unexpected{std::move(frames.error())};
    }
    log::info(kLog, "Vulkan device: {} ({}), {}; {} frames in flight; validation {}", device->m_adapter.name,
              enumName(device->m_adapter.kind), device->m_adapter.driver, desc.framesInFlight,
              device->m_messenger != VK_NULL_HANDLE ? "on" : "off");
    return device;
}

auto VulkanDevice::createInstance(const DeviceDesc& desc) -> Result<void, RhiError> {
    if (const VkResult loaded = volkInitialize(); loaded != VK_SUCCESS) {
        return fail(RhiError::BackendUnavailable,
                    "no Vulkan loader: no driver installs one (vulkan-1.dll, libvulkan.so.1)");
    }
    if (const std::uint32_t version = volkGetInstanceVersion(); version < kRequiredApiVersion) {
        return fail(RhiError::BackendUnavailable,
                    std::format("the Vulkan loader offers {}.{}; 1.3 is required",
                                VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version)));
    }

    auto available =
        enumerate<VkExtensionProperties>([](std::uint32_t* count, VkExtensionProperties* values) {
            return vkEnumerateInstanceExtensionProperties(nullptr, count, values);
        });
    if (!available) {
        return std::unexpected{std::move(available.error())};
    }
    std::vector<const char*> extensions;
    if (desc.window) {
        for (const std::string_view needed :
             {std::string_view{"VK_KHR_surface"}, surfaceExtensionOf(*desc.window)}) {
            if (!hasName(*available, needed)) {
                return fail(RhiError::SurfaceUnavailable, std::format("the Vulkan loader has no {}", needed));
            }
            extensions.push_back(needed.data()); // the literals above are zero-terminated
        }
    }

    std::vector<const char*> layers;
    bool hasValidationFeatures = false;
    if (desc.isValidationEnabled) {
        auto installed = enumerate<VkLayerProperties>([](std::uint32_t* count, VkLayerProperties* values) {
            return vkEnumerateInstanceLayerProperties(count, values);
        });
        if (!installed) {
            return std::unexpected{std::move(installed.error())};
        }
        const bool hasLayer = std::ranges::any_of(*installed, [](const VkLayerProperties& layer) {
            return textOf(layer.layerName, VK_MAX_EXTENSION_NAME_SIZE) == kValidationLayer;
        });
        if (hasLayer && hasName(*available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            layers.push_back(kValidationLayer.data());
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            auto layerExtensions =
                enumerate<VkExtensionProperties>([](std::uint32_t* count, VkExtensionProperties* values) {
                    return vkEnumerateInstanceExtensionProperties(kValidationLayer.data(), count, values);
                });
            hasValidationFeatures =
                layerExtensions && hasName(*layerExtensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
            if (hasValidationFeatures) {
                extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
            }
        } else {
            log::warning(kLog, "Vulkan validation was asked for, but {} is not installed", kValidationLayer);
        }
    }

    const std::string applicationName{desc.applicationName};
    const VkApplicationInfo application{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = applicationName.c_str(),
        .applicationVersion = 0,
        .pEngineName = "Next Engine",
        .engineVersion = 0,
        .apiVersion = kRequiredApiVersion,
    };
    VkDebugUtilsMessengerCreateInfoEXT messenger{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = &onValidationMessage,
        .pUserData = this,
    };
    constexpr std::array kValidationFeatures{VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT};
    const VkValidationFeaturesEXT validationFeatures{
        .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .pNext = nullptr,
        .enabledValidationFeatureCount = narrow<std::uint32_t>(kValidationFeatures.size()),
        .pEnabledValidationFeatures = kValidationFeatures.data(),
        .disabledValidationFeatureCount = 0,
        .pDisabledValidationFeatures = nullptr,
    };
    if (hasValidationFeatures) {
        messenger.pNext = &validationFeatures;
    }
    const VkInstanceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = layers.empty() ? nullptr : &messenger, // also reports what instance creation says
        .flags = 0,
        .pApplicationInfo = &application,
        .enabledLayerCount = narrow<std::uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = narrow<std::uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    if (const VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance); result != VK_SUCCESS) {
        return vulkanFailure(result, "vkCreateInstance");
    }
    volkLoadInstanceOnly(m_instance);
    if (!layers.empty()) {
        messenger.pNext = nullptr;
        if (const VkResult result =
                vkCreateDebugUtilsMessengerEXT(m_instance, &messenger, nullptr, &m_messenger);
            result != VK_SUCCESS) {
            return vulkanFailure(result, "vkCreateDebugUtilsMessengerEXT");
        }
    }
    return {};
}

auto VulkanDevice::createSurface(const platform::NativeWindow& window) -> Result<void, RhiError> {
    const auto created = std::visit(
        [this]([[maybe_unused]] const auto& native) -> VkResult {
            using Native = std::decay_t<decltype(native)>;
            if constexpr (std::same_as<Native, platform::Win32Window>) {
#if defined(_WIN32)
                const auto create =
                    reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>( // NOLINT: the API's own cast
                        vkGetInstanceProcAddr(m_instance, "vkCreateWin32SurfaceKHR"));
                const VkWin32SurfaceCreateInfoKHR info{.sType =
                                                           VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
                                                       .pNext = nullptr,
                                                       .flags = 0,
                                                       .hinstance = static_cast<HINSTANCE>(native.instance),
                                                       .hwnd = static_cast<HWND>(native.window)};
                return create == nullptr ? VK_ERROR_EXTENSION_NOT_PRESENT
                                         : create(m_instance, &info, nullptr, &m_surface);
#else
                return VK_ERROR_EXTENSION_NOT_PRESENT; // a Win32 window exists only on Windows
#endif
            } else if constexpr (std::same_as<Native, platform::XlibWindow>) {
#if defined(_WIN32)
                return VK_ERROR_EXTENSION_NOT_PRESENT;
#else
                const auto create =
                    reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>( // NOLINT: the API's own cast
                        vkGetInstanceProcAddr(m_instance, "vkCreateXlibSurfaceKHR"));
                const VkXlibSurfaceCreateInfoKHR info{.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
                                                      .pNext = nullptr,
                                                      .flags = 0,
                                                      .dpy = static_cast<Display*>(native.display),
                                                      .window = static_cast<Window>(native.window)};
                return create == nullptr ? VK_ERROR_EXTENSION_NOT_PRESENT
                                         : create(m_instance, &info, nullptr, &m_surface);
#endif
            } else {
#if defined(_WIN32)
                return VK_ERROR_EXTENSION_NOT_PRESENT;
#else
                const auto create =
                    reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>( // NOLINT: the API's own cast
                        vkGetInstanceProcAddr(m_instance, "vkCreateWaylandSurfaceKHR"));
                const VkWaylandSurfaceCreateInfoKHR info{
                    .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
                    .pNext = nullptr,
                    .flags = 0,
                    .display = static_cast<wl_display*>(native.display),
                    .surface = static_cast<wl_surface*>(native.surface)};
                return create == nullptr ? VK_ERROR_EXTENSION_NOT_PRESENT
                                         : create(m_instance, &info, nullptr, &m_surface);
#endif
            }
        },
        window);
    if (created != VK_SUCCESS) {
        return fail(
            RhiError::SurfaceUnavailable,
            std::format("creating the {} surface: {}", surfaceExtensionOf(window), resultName(created)));
    }
    return {};
}

auto VulkanDevice::chooseAdapter() -> Result<void, RhiError> {
    auto adapters = enumerate<VkPhysicalDevice>([this](std::uint32_t* count, VkPhysicalDevice* values) {
        return vkEnumeratePhysicalDevices(m_instance, count, values);
    });
    if (!adapters) {
        return std::unexpected{std::move(adapters.error())};
    }
    std::vector<std::string> refusals;
    int bestRank = -1;
    for (VkPhysicalDevice candidate : *adapters) {
        VkPhysicalDeviceVulkan12Properties properties12{};
        properties12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &properties12;
        vkGetPhysicalDeviceProperties2(candidate, &properties);
        const std::string_view name =
            textOf(properties.properties.deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);

        VkPhysicalDeviceVulkan11Features features11{};
        features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.pNext = &features11;
        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.pNext = &features12;
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &features13;
        vkGetPhysicalDeviceFeatures2(candidate, &features);

        const std::uint32_t version = properties.properties.apiVersion;
        if (version < kRequiredApiVersion) {
            refusals.push_back(std::format("{}: Vulkan {}.{}", name, VK_API_VERSION_MAJOR(version),
                                           VK_API_VERSION_MINOR(version)));
            continue;
        }
        if (features13.dynamicRendering == VK_FALSE || features13.synchronization2 == VK_FALSE ||
            features12.timelineSemaphore == VK_FALSE || features11.shaderDrawParameters == VK_FALSE) {
            refusals.push_back(std::format(
                "{}: no dynamic rendering, synchronization2, timeline semaphores or shader draw parameters",
                name));
            continue;
        }
        if (m_surface != VK_NULL_HANDLE) {
            auto extensions = enumerate<VkExtensionProperties>(
                [candidate](std::uint32_t* count, VkExtensionProperties* values) {
                    return vkEnumerateDeviceExtensionProperties(candidate, nullptr, count, values);
                });
            if (!extensions || !hasName(*extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
                refusals.push_back(std::format("{}: no swapchain", name));
                continue;
            }
        }

        std::uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        std::optional<std::uint32_t> family;
        for (std::uint32_t index = 0; index < familyCount && !family; ++index) {
            if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
                continue;
            }
            VkBool32 canPresent = VK_TRUE;
            if (m_surface != VK_NULL_HANDLE && vkGetPhysicalDeviceSurfaceSupportKHR(
                                                   candidate, index, m_surface, &canPresent) != VK_SUCCESS) {
                canPresent = VK_FALSE;
            }
            if (canPresent == VK_TRUE) {
                family = index;
            }
        }
        if (!family) {
            refusals.push_back(
                std::format("{}: no graphics queue{}", name,
                            m_surface != VK_NULL_HANDLE ? " that presents to the window" : ""));
            continue;
        }

        const AdapterKind kind = adapterKindOf(properties.properties.deviceType);
        if (rankOf(kind) > bestRank) {
            bestRank = rankOf(kind);
            m_physicalDevice = candidate;
            m_queueFamily = *family;
            m_hasAnisotropy = features.features.samplerAnisotropy == VK_TRUE;
            m_maxAnisotropy = properties.properties.limits.maxSamplerAnisotropy;
            m_adapter = AdapterInfo{
                .name = std::string{name},
                .kind = kind,
                .driver = std::format("{} {}", textOf(properties12.driverName, VK_MAX_DRIVER_NAME_SIZE),
                                      textOf(properties12.driverInfo, VK_MAX_DRIVER_INFO_SIZE)),
            };
        }
    }
    if (m_physicalDevice == VK_NULL_HANDLE) {
        return fail(RhiError::NoSuitableAdapter,
                    refusals.empty() ? std::string{"no GPU offers Vulkan"}
                                     : refusals | std::views::join_with(std::string_view{"; "}) |
                                           std::ranges::to<std::string>());
    }
    return {};
}

auto VulkanDevice::createLogicalDevice() -> Result<void, RhiError> {
    constexpr float kPriority = 1.0F;
    const VkDeviceQueueCreateInfo queueInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = m_queueFamily,
        .queueCount = 1,
        .pQueuePriorities = &kPriority,
    };
    // Slang reads SV_VertexID as DirectX does, without the base vertex: shader draw parameters.
    VkPhysicalDeviceVulkan11Features features11{};
    features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    features11.shaderDrawParameters = VK_TRUE;
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.pNext = &features11;
    features12.timelineSemaphore = VK_TRUE;
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.pNext = &features12;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features13;
    features.features.samplerAnisotropy = m_hasAnisotropy ? VK_TRUE : VK_FALSE;
    const std::array swapchainExtension{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const bool hasSurface = m_surface != VK_NULL_HANDLE;
    const VkDeviceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = hasSurface ? narrow<std::uint32_t>(swapchainExtension.size()) : 0U,
        .ppEnabledExtensionNames = hasSurface ? swapchainExtension.data() : nullptr,
        .pEnabledFeatures = nullptr,
    };
    if (const VkResult result = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device);
        result != VK_SUCCESS) {
        return vulkanFailure(result, "vkCreateDevice");
    }
    volkLoadDevice(m_device);
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);

    VkSemaphoreTypeCreateInfo timelineType{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .pNext = nullptr,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = 0,
    };
    const VkSemaphoreCreateInfo timelineInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &timelineType, .flags = 0};
    if (const VkResult result = vkCreateSemaphore(m_device, &timelineInfo, nullptr, &m_timeline);
        result != VK_SUCCESS) {
        return vulkanFailure(result, "vkCreateSemaphore");
    }
    return {};
}

auto VulkanDevice::createFrames(std::uint32_t count) -> Result<void, RhiError> {
    m_frames.resize(count);
    for (Frame& frame : m_frames) {
        const VkCommandPoolCreateInfo poolInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                               .pNext = nullptr,
                                               .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                                               .queueFamilyIndex = m_queueFamily};
        if (const VkResult result = vkCreateCommandPool(m_device, &poolInfo, nullptr, &frame.pool);
            result != VK_SUCCESS) {
            return vulkanFailure(result, "vkCreateCommandPool");
        }
        const VkCommandBufferAllocateInfo allocateInfo{.sType =
                                                           VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                                       .pNext = nullptr,
                                                       .commandPool = frame.pool,
                                                       .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                                       .commandBufferCount = 1};
        if (const VkResult result = vkAllocateCommandBuffers(m_device, &allocateInfo, &frame.commands);
            result != VK_SUCCESS) {
            return vulkanFailure(result, "vkAllocateCommandBuffers");
        }
        const VkSemaphoreCreateInfo semaphoreInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};
        if (const VkResult result =
                vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &frame.imageAcquired);
            result != VK_SUCCESS) {
            return vulkanFailure(result, "vkCreateSemaphore");
        }
    }
    m_frameIndex = m_frames.size() - 1; // the first frame is frame 0
    return {};
}

VulkanDevice::~VulkanDevice() {
    expects(m_openSwapchains == 0, "rhi::Device: a swapchain outlives its device");
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
        for (RetiredResource& retired : m_retired) {
            destroyNow(retired.resource);
        }
        // What was never destroyed goes with the device.
        const auto destroyAll = [this](auto& pool) {
            for (auto& resource : pool.drain()) {
                RetiredResource::Resource held{std::move(resource)};
                destroyNow(held);
            }
        };
        destroyAll(m_pipelines);
        destroyAll(m_bindGroups);
        destroyAll(m_bindGroupLayouts);
        destroyAll(m_shaders);
        destroyAll(m_samplers);
        destroyAll(m_textures);
        destroyAll(m_buffers);
        for (VkDescriptorPool pool : m_descriptorPools) {
            vkDestroyDescriptorPool(m_device, pool, nullptr);
        }
        if (m_allocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(m_allocator);
        }
        for (const Frame& frame : m_frames) {
            vkDestroySemaphore(m_device, frame.imageAcquired, nullptr);
            vkDestroyCommandPool(m_device, frame.pool, nullptr);
        }
        vkDestroySemaphore(m_device, m_timeline, nullptr);
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
    if (m_messenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr);
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
    }
    if (m_ownsDeviceSlot) {
        isDeviceOpen.store(false);
    }
}

auto VulkanDevice::createSwapchain(const SwapchainDesc& desc)
    -> Result<std::unique_ptr<Swapchain>, RhiError> {
    if (m_surface == VK_NULL_HANDLE) {
        return fail(RhiError::SurfaceUnavailable, "the device was created without a window");
    }
    auto swapchain = std::make_unique<VulkanSwapchain>(*this);
    if (auto built = swapchain->build(desc); !built) {
        return std::unexpected{std::move(built.error())};
    }
    return swapchain;
}

auto VulkanDevice::beginFrame() -> Result<std::reference_wrapper<CommandList>, RhiError> {
    expects(!m_isRecording, "rhi::Device::beginFrame: the previous frame was not submitted");
    m_frameIndex = (m_frameIndex + 1) % m_frames.size();
    Frame& frame = m_frames[m_frameIndex];
    if (frame.submitted != 0) {
        const VkSemaphoreWaitInfo wait{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                                       .pNext = nullptr,
                                       .flags = 0,
                                       .semaphoreCount = 1,
                                       .pSemaphores = &m_timeline,
                                       .pValues = &frame.submitted};
        if (const VkResult result = vkWaitSemaphores(m_device, &wait, kNoTimeout); result != VK_SUCCESS) {
            return vulkanFailure(result, "vkWaitSemaphores");
        }
    }
    destroyFinished();
    if (const VkResult result = vkResetCommandPool(m_device, frame.pool, 0); result != VK_SUCCESS) {
        return vulkanFailure(result, "vkResetCommandPool");
    }
    const VkCommandBufferBeginInfo beginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                             .pNext = nullptr,
                                             .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                                             .pInheritanceInfo = nullptr};
    if (const VkResult result = vkBeginCommandBuffer(frame.commands, &beginInfo); result != VK_SUCCESS) {
        return vulkanFailure(result, "vkBeginCommandBuffer");
    }
    m_commandList.startRecording(frame.commands);
    m_isRecording = true;
    return std::ref<CommandList>(m_commandList);
}

auto VulkanDevice::submitFrame() -> Result<void, RhiError> {
    expects(!currentFrame().hasAcquiredImage,
            "rhi::Device::submitFrame: the frame acquired a swapchain image, which it must present");
    return submit(nullptr);
}

auto VulkanDevice::submitFrame(Swapchain& presentTo) -> Result<void, RhiError> {
    auto& swapchain = static_cast<VulkanSwapchain&>(presentTo); // the only swapchain this device makes
    expects(swapchain.device() == this, "rhi::Device::submitFrame: the swapchain is another device's");
    return submit(&swapchain);
}

auto VulkanDevice::submit(VulkanSwapchain* presentTo) -> Result<void, RhiError> {
    expects(m_isRecording, "rhi::Device::submitFrame: no frame was begun");
    Frame& frame = currentFrame();
    m_isRecording = false;
    if (const VkResult result = vkEndCommandBuffer(frame.commands); result != VK_SUCCESS) {
        return vulkanFailure(result, "vkEndCommandBuffer");
    }
    const std::optional<std::uint32_t> image =
        presentTo != nullptr ? presentTo->takeAcquired() : std::nullopt;
    frame.hasAcquiredImage = false;
    frame.submitted = ++m_timelineValue;

    const VkSemaphoreSubmitInfo acquired{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .pNext = nullptr,
        .semaphore = frame.imageAcquired,
        .value = 0,
        // Every command waits: a transition from Undefined runs no stage of its own.
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .deviceIndex = 0};
    std::array signals{
        VkSemaphoreSubmitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                              .pNext = nullptr,
                              .semaphore = m_timeline,
                              .value = frame.submitted,
                              .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                              .deviceIndex = 0},
        VkSemaphoreSubmitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                              .pNext = nullptr,
                              .semaphore = image ? presentTo->renderFinished(*image) : VK_NULL_HANDLE,
                              .value = 0,
                              .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                              .deviceIndex = 0},
    };
    const VkCommandBufferSubmitInfo commands{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                                             .pNext = nullptr,
                                             .commandBuffer = frame.commands,
                                             .deviceMask = 0};
    const VkSubmitInfo2 submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                                   .pNext = nullptr,
                                   .flags = 0,
                                   .waitSemaphoreInfoCount = image ? 1U : 0U,
                                   .pWaitSemaphoreInfos = &acquired,
                                   .commandBufferInfoCount = 1,
                                   .pCommandBufferInfos = &commands,
                                   .signalSemaphoreInfoCount = image ? 2U : 1U,
                                   .pSignalSemaphoreInfos = signals.data()};
    if (const VkResult result = vkQueueSubmit2(m_queue, 1, &submitInfo, VK_NULL_HANDLE);
        result != VK_SUCCESS) {
        return vulkanFailure(result, "vkQueueSubmit2");
    }
    if (image) {
        return presentTo->present(*image);
    }
    return {};
}

void VulkanDevice::waitIdle() noexcept {
    vkDeviceWaitIdle(m_device);
}

// --- VulkanDevice: resources ---------------------------------------------------------------------

auto VulkanDevice::createAllocator() -> Result<void, RhiError> {
    VmaAllocatorCreateInfo info{};
    info.physicalDevice = m_physicalDevice;
    info.device = m_device;
    info.instance = m_instance;
    info.vulkanApiVersion = kRequiredApiVersion;
    VmaVulkanFunctions functions{};
    if (const VkResult result = vmaImportVulkanFunctionsFromVolk(&info, &functions); result != VK_SUCCESS) {
        return vulkanFailure(result, "vmaImportVulkanFunctionsFromVolk");
    }
    info.pVulkanFunctions = &functions;
    if (const VkResult result = vmaCreateAllocator(&info, &m_allocator); result != VK_SUCCESS) {
        return vulkanFailure(result, "vmaCreateAllocator");
    }
    return {};
}

void VulkanDevice::name(VkObjectType type, std::uint64_t object, std::string_view label) const {
    if (m_messenger == VK_NULL_HANDLE) {
        return; // the debug utilities are on with the validation layers
    }
    const std::string text{label};
    const VkDebugUtilsObjectNameInfoEXT info{.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                                             .pNext = nullptr,
                                             .objectType = type,
                                             .objectHandle = object,
                                             .pObjectName = text.c_str()};
    vkSetDebugUtilsObjectNameEXT(m_device, &info);
}

auto VulkanDevice::allocateDescriptorSet(VkDescriptorSetLayout layout) -> Result<VulkanBindGroup, RhiError> {
    const auto allocateFrom = [this, layout](VkDescriptorPool pool, VkDescriptorSet* set) {
        const VkDescriptorSetAllocateInfo info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                               .pNext = nullptr,
                                               .descriptorPool = pool,
                                               .descriptorSetCount = 1,
                                               .pSetLayouts = &layout};
        return vkAllocateDescriptorSets(m_device, &info, set);
    };
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!m_descriptorPools.empty()) {
        const VkResult result = allocateFrom(m_descriptorPools.back(), &set);
        if (result == VK_SUCCESS) {
            return VulkanBindGroup{.set = set, .pool = m_descriptorPools.back()};
        }
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) {
            return vulkanFailure(result, "vkAllocateDescriptorSets");
        }
    }
    // Every pool is full: one more, with room for kBindGroupsPerPool groups of a few bindings each.
    const std::array sizes{
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                             .descriptorCount = kBindGroupsPerPool * 2},
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                             .descriptorCount = kBindGroupsPerPool},
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                             .descriptorCount = kBindGroupsPerPool * 4},
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = kBindGroupsPerPool * 2},
    };
    const VkDescriptorPoolCreateInfo poolInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                              .pNext = nullptr,
                                              .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                                              .maxSets = kBindGroupsPerPool,
                                              .poolSizeCount = narrow<std::uint32_t>(sizes.size()),
                                              .pPoolSizes = sizes.data()};
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (const VkResult result = vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &pool);
        result != VK_SUCCESS) {
        return vulkanFailure(result, "vkCreateDescriptorPool");
    }
    m_descriptorPools.push_back(pool);
    if (const VkResult result = allocateFrom(pool, &set); result != VK_SUCCESS) {
        return vulkanFailure(result, "vkAllocateDescriptorSets");
    }
    return VulkanBindGroup{.set = set, .pool = pool};
}

void VulkanDevice::retire(RetiredResource::Resource resource) {
    // The frame being recorded may use it; otherwise the last one submitted may.
    const std::uint64_t afterValue = m_isRecording ? m_timelineValue + 1 : m_timelineValue;
    m_retired.push_back(RetiredResource{.afterValue = afterValue, .resource = std::move(resource)});
}

void VulkanDevice::destroyFinished() noexcept {
    std::uint64_t finished = 0;
    if (vkGetSemaphoreCounterValue(m_device, m_timeline, &finished) != VK_SUCCESS) {
        return; // kept until a later frame can tell
    }
    std::erase_if(m_retired, [this, finished](RetiredResource& retired) {
        if (retired.afterValue > finished) {
            return false;
        }
        destroyNow(retired.resource);
        return true;
    });
}

void VulkanDevice::destroyNow(RetiredResource::Resource& resource) noexcept {
    std::visit(
        [this](auto& held) {
            using Held = std::decay_t<decltype(held)>;
            if constexpr (std::same_as<Held, VulkanBuffer>) {
                vmaDestroyBuffer(m_allocator, held.buffer, held.allocation);
            } else if constexpr (std::same_as<Held, VulkanTexture>) {
                vkDestroyImageView(m_device, held.view, nullptr);
                if (held.allocation != VK_NULL_HANDLE) { // a swapchain's image is the swapchain's
                    vmaDestroyImage(m_allocator, held.image, held.allocation);
                }
            } else if constexpr (std::same_as<Held, VulkanSampler>) {
                vkDestroySampler(m_device, held.sampler, nullptr);
            } else if constexpr (std::same_as<Held, VulkanShader>) {
                vkDestroyShaderModule(m_device, held.module, nullptr);
            } else if constexpr (std::same_as<Held, VulkanBindGroupLayout>) {
                vkDestroyDescriptorSetLayout(m_device, held.layout, nullptr);
            } else if constexpr (std::same_as<Held, VulkanBindGroup>) {
                vkFreeDescriptorSets(m_device, held.pool, 1, &held.set);
            } else {
                vkDestroyPipeline(m_device, held.pipeline, nullptr);
                vkDestroyPipelineLayout(m_device, held.layout, nullptr);
            }
        },
        resource);
}

auto VulkanDevice::createBuffer(const BufferDesc& desc) -> Result<BufferHandle, RhiError> {
    expects(!desc.label.empty() && desc.size > 0, "rhi::Device::createBuffer: a label and a size");
    const VkBufferCreateInfo info{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                  .pNext = nullptr,
                                  .flags = 0,
                                  .size = desc.size,
                                  .usage = bufferUsageOf(desc.usage),
                                  .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                  .queueFamilyIndexCount = 0,
                                  .pQueueFamilyIndices = nullptr};
    VmaAllocationCreateInfo allocation{};
    switch (desc.location) {
    case MemoryLocation::Device:
        allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;
    case MemoryLocation::Upload:
        allocation.usage = VMA_MEMORY_USAGE_AUTO;
        allocation.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocation.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    case MemoryLocation::Readback:
        allocation.usage = VMA_MEMORY_USAGE_AUTO;
        allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocation.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    }
    VulkanBuffer buffer{.size = desc.size};
    VmaAllocationInfo allocated{};
    if (const VkResult result =
            vmaCreateBuffer(m_allocator, &info, &allocation, &buffer.buffer, &buffer.allocation, &allocated);
        result != VK_SUCCESS) {
        return vulkanFailure(result, std::format("vmaCreateBuffer({:?})", desc.label));
    }
    if (allocated.pMappedData != nullptr) {
        buffer.mapped =
            std::span{static_cast<std::byte*>(allocated.pMappedData), narrow<std::size_t>(desc.size)};
    }
    name(VK_OBJECT_TYPE_BUFFER, objectHandleOf(buffer.buffer), desc.label);
    return m_buffers.insert(buffer);
}

auto VulkanDevice::createTexture(const TextureDesc& desc) -> Result<TextureHandle, RhiError> {
    expects(!desc.label.empty() && desc.extent.width > 0 && desc.extent.height > 0 && desc.mipLevels > 0 &&
                desc.arrayLayers > 0,
            "rhi::Device::createTexture: a label, a size, a mip level and a layer");
    expects(desc.arrayLayers == 1 ||
                !(desc.usage.test(TextureUsage::RenderTarget) || desc.usage.test(TextureUsage::DepthTarget)),
            "rhi::Device::createTexture: a render or depth target has one layer");
    const VkFormat format = vulkanFormatOf(desc.format);
    const VkImageUsageFlags usage = imageUsageOf(desc.usage);
    VkImageFormatProperties supported{};
    if (format == VK_FORMAT_UNDEFINED ||
        vkGetPhysicalDeviceImageFormatProperties(m_physicalDevice, format, VK_IMAGE_TYPE_2D,
                                                 VK_IMAGE_TILING_OPTIMAL, usage, 0,
                                                 &supported) != VK_SUCCESS ||
        desc.extent.width > supported.maxExtent.width || desc.extent.height > supported.maxExtent.height ||
        desc.mipLevels > supported.maxMipLevels || desc.arrayLayers > supported.maxArrayLayers) {
        return fail(RhiError::UnsupportedFormat,
                    std::format("{:?}: {} at {}x{} with {} mip levels and {} layers for its usage",
                                desc.label, enumName(desc.format), desc.extent.width, desc.extent.height,
                                desc.mipLevels, desc.arrayLayers));
    }
    const VkImageCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {.width = desc.extent.width, .height = desc.extent.height, .depth = 1},
        .mipLevels = desc.mipLevels,
        .arrayLayers = desc.arrayLayers,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VulkanTexture texture{.format = desc.format,
                          .extent = desc.extent,
                          .mipLevels = desc.mipLevels,
                          .arrayLayers = desc.arrayLayers,
                          .aspect = aspectOf(desc.format)};
    if (const VkResult result =
            vmaCreateImage(m_allocator, &info, &allocation, &texture.image, &texture.allocation, nullptr);
        result != VK_SUCCESS) {
        return vulkanFailure(result, std::format("vmaCreateImage({:?})", desc.label));
    }
    const VkImageViewCreateInfo viewInfo{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                         .pNext = nullptr,
                                         .flags = 0,
                                         .image = texture.image,
                                         .viewType = desc.arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                                                          : VK_IMAGE_VIEW_TYPE_2D,
                                         .format = format,
                                         .components = {},
                                         .subresourceRange = {.aspectMask = texture.aspect,
                                                              .baseMipLevel = 0,
                                                              .levelCount = desc.mipLevels,
                                                              .baseArrayLayer = 0,
                                                              .layerCount = desc.arrayLayers}};
    if (const VkResult result = vkCreateImageView(m_device, &viewInfo, nullptr, &texture.view);
        result != VK_SUCCESS) {
        vmaDestroyImage(m_allocator, texture.image, texture.allocation);
        return vulkanFailure(result, std::format("vkCreateImageView({:?})", desc.label));
    }
    name(VK_OBJECT_TYPE_IMAGE, objectHandleOf(texture.image), desc.label);
    return m_textures.insert(texture);
}

auto VulkanDevice::createSampler(const SamplerDesc& desc) -> Result<SamplerHandle, RhiError> {
    const bool isAnisotropic = m_hasAnisotropy && desc.maxAnisotropy > 1.0F;
    const VkSamplerCreateInfo info{.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                   .pNext = nullptr,
                                   .flags = 0,
                                   .magFilter = filterOf(desc.magnify),
                                   .minFilter = filterOf(desc.minify),
                                   .mipmapMode = mipmapModeOf(desc.mipmap),
                                   .addressModeU = addressModeOf(desc.address),
                                   .addressModeV = addressModeOf(desc.address),
                                   .addressModeW = addressModeOf(desc.address),
                                   .mipLodBias = 0.0F,
                                   .anisotropyEnable = isAnisotropic ? VK_TRUE : VK_FALSE,
                                   .maxAnisotropy =
                                       isAnisotropic ? std::min(desc.maxAnisotropy, m_maxAnisotropy) : 1.0F,
                                   .compareEnable = VK_FALSE,
                                   .compareOp = VK_COMPARE_OP_ALWAYS,
                                   .minLod = 0.0F,
                                   .maxLod = VK_LOD_CLAMP_NONE,
                                   .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
                                   .unnormalizedCoordinates = VK_FALSE};
    VulkanSampler sampler;
    if (const VkResult result = vkCreateSampler(m_device, &info, nullptr, &sampler.sampler);
        result != VK_SUCCESS) {
        return vulkanFailure(result, std::format("vkCreateSampler({:?})", desc.label));
    }
    name(VK_OBJECT_TYPE_SAMPLER, objectHandleOf(sampler.sampler), desc.label);
    return m_samplers.insert(sampler);
}

auto VulkanDevice::createShader(const ShaderDesc& desc) -> Result<ShaderHandle, RhiError> {
    if (desc.code.size() < sizeof(std::uint32_t) || desc.code.size() % sizeof(std::uint32_t) != 0 ||
        loadLittleEndian<std::uint32_t>(desc.code) != kSpirvMagic) {
        return fail(RhiError::InvalidShader,
                    std::format("{:?}: {} bytes that are not a SPIR-V module", desc.label, desc.code.size()));
    }
    // The driver reads whole words: copied to storage aligned for them.
    std::vector<std::uint32_t> words(desc.code.size() / sizeof(std::uint32_t));
    std::memcpy(words.data(), desc.code.data(), desc.code.size());
    const VkShaderModuleCreateInfo info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                        .pNext = nullptr,
                                        .flags = 0,
                                        .codeSize = desc.code.size(),
                                        .pCode = words.data()};
    VulkanShader shader;
    if (const VkResult result = vkCreateShaderModule(m_device, &info, nullptr, &shader.module);
        result != VK_SUCCESS) {
        return vulkanFailure(result, std::format("vkCreateShaderModule({:?})", desc.label));
    }
    name(VK_OBJECT_TYPE_SHADER_MODULE, objectHandleOf(shader.module), desc.label);
    return m_shaders.insert(shader);
}

auto VulkanDevice::createBindGroupLayout(const BindGroupLayoutDesc& desc)
    -> Result<BindGroupLayoutHandle, RhiError> {
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(desc.bindings.size());
    for (const BindingLayout& binding : desc.bindings) {
        bindings.push_back(VkDescriptorSetLayoutBinding{.binding = binding.binding,
                                                        .descriptorType = descriptorTypeOf(binding.type),
                                                        .descriptorCount = 1,
                                                        .stageFlags = stageFlagsOf(binding.stages),
                                                        .pImmutableSamplers = nullptr});
    }
    const VkDescriptorSetLayoutCreateInfo info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                               .pNext = nullptr,
                                               .flags = 0,
                                               .bindingCount = narrow<std::uint32_t>(bindings.size()),
                                               .pBindings = bindings.data()};
    VulkanBindGroupLayout layout{.bindings =
                                     std::vector<BindingLayout>{desc.bindings.begin(), desc.bindings.end()}};
    if (const VkResult result = vkCreateDescriptorSetLayout(m_device, &info, nullptr, &layout.layout);
        result != VK_SUCCESS) {
        return vulkanFailure(result, std::format("vkCreateDescriptorSetLayout({:?})", desc.label));
    }
    name(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, objectHandleOf(layout.layout), desc.label);
    return m_bindGroupLayouts.insert(std::move(layout));
}

auto VulkanDevice::createBindGroup(const BindGroupDesc& desc) -> Result<BindGroupHandle, RhiError> {
    const VulkanBindGroupLayout& layout = m_bindGroupLayouts.get(desc.layout);
    expects(desc.bindings.size() == layout.bindings.size(),
            "rhi::Device::createBindGroup: one resource per binding");
    auto group = allocateDescriptorSet(layout.layout);
    if (!group) {
        return std::unexpected{std::move(group.error())};
    }
    // Reserved: the writes point into these, which must not move.
    std::vector<VkDescriptorBufferInfo> buffers;
    std::vector<VkDescriptorImageInfo> images;
    std::vector<VkWriteDescriptorSet> writes;
    buffers.reserve(desc.bindings.size());
    images.reserve(desc.bindings.size());
    writes.reserve(desc.bindings.size());
    for (const Binding& binding : desc.bindings) {
        const auto declared = std::ranges::find(layout.bindings, binding.binding, &BindingLayout::binding);
        expects(declared != layout.bindings.end(), "rhi::Device::createBindGroup: a binding its layout has");
        VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                   .pNext = nullptr,
                                   .dstSet = group->set,
                                   .dstBinding = binding.binding,
                                   .dstArrayElement = 0,
                                   .descriptorCount = 1,
                                   .descriptorType = descriptorTypeOf(declared->type),
                                   .pImageInfo = nullptr,
                                   .pBufferInfo = nullptr,
                                   .pTexelBufferView = nullptr};
        if (const auto* range = std::get_if<BufferBinding>(&binding.resource)) {
            expects(declared->type == BindingType::UniformBuffer ||
                        declared->type == BindingType::StorageBuffer,
                    "rhi::Device::createBindGroup: a buffer for a buffer binding");
            buffers.push_back(
                VkDescriptorBufferInfo{.buffer = m_buffers.get(range->buffer).buffer,
                                       .offset = range->offset,
                                       .range = range->size == 0 ? VK_WHOLE_SIZE : range->size});
            write.pBufferInfo = &buffers.back();
        } else if (const auto* texture = std::get_if<TextureHandle>(&binding.resource)) {
            expects(declared->type == BindingType::SampledTexture,
                    "rhi::Device::createBindGroup: a texture for a texture binding");
            images.push_back(VkDescriptorImageInfo{.sampler = VK_NULL_HANDLE,
                                                   .imageView = m_textures.get(*texture).view,
                                                   .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            write.pImageInfo = &images.back();
        } else {
            expects(declared->type == BindingType::Sampler,
                    "rhi::Device::createBindGroup: a sampler for a sampler binding");
            images.push_back(VkDescriptorImageInfo{
                .sampler = m_samplers.get(std::get<SamplerHandle>(binding.resource)).sampler,
                .imageView = VK_NULL_HANDLE,
                .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED});
            write.pImageInfo = &images.back();
        }
        writes.push_back(write);
    }
    vkUpdateDescriptorSets(m_device, narrow<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    name(VK_OBJECT_TYPE_DESCRIPTOR_SET, objectHandleOf(group->set), desc.label);
    return m_bindGroups.insert(*group);
}

auto VulkanDevice::createRenderPipeline(const RenderPipelineDesc& desc)
    -> Result<RenderPipelineHandle, RhiError> {
    expects(desc.colorTargets.size() <= kMaxColorAttachments,
            "rhi::Device::createRenderPipeline: at most 8 targets");
    std::vector<VkDescriptorSetLayout> setLayouts;
    for (const BindGroupLayoutHandle layout : desc.bindGroupLayouts) {
        setLayouts.push_back(m_bindGroupLayouts.get(layout).layout);
    }
    const VkPipelineLayoutCreateInfo layoutInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                .pNext = nullptr,
                                                .flags = 0,
                                                .setLayoutCount = narrow<std::uint32_t>(setLayouts.size()),
                                                .pSetLayouts = setLayouts.data(),
                                                .pushConstantRangeCount = 0,
                                                .pPushConstantRanges = nullptr};
    VulkanPipeline pipeline;
    if (const VkResult result = vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &pipeline.layout);
        result != VK_SUCCESS) {
        return vulkanFailure(result, std::format("vkCreatePipelineLayout({:?})", desc.label));
    }

    const std::string vertexEntry{desc.vertex.entryPoint};
    const std::string fragmentEntry{desc.fragment.entryPoint};
    const std::array stages{
        VkPipelineShaderStageCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                        .pNext = nullptr,
                                        .flags = 0,
                                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                                        .module = m_shaders.get(desc.vertex.shader).module,
                                        .pName = vertexEntry.c_str(),
                                        .pSpecializationInfo = nullptr},
        VkPipelineShaderStageCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                        .pNext = nullptr,
                                        .flags = 0,
                                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                                        .module = m_shaders.get(desc.fragment.shader).module,
                                        .pName = fragmentEntry.c_str(),
                                        .pSpecializationInfo = nullptr},
    };

    std::vector<VkVertexInputBindingDescription> vertexBindings;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    for (std::uint32_t slot = 0; slot < desc.vertexBuffers.size(); ++slot) {
        const VertexBufferLayout& layout = desc.vertexBuffers[slot];
        vertexBindings.push_back(VkVertexInputBindingDescription{
            .binding = slot, .stride = layout.stride, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX});
        for (const VertexAttribute& attribute : layout.attributes) {
            vertexAttributes.push_back(
                VkVertexInputAttributeDescription{.location = attribute.location,
                                                  .binding = slot,
                                                  .format = vertexFormatOf(attribute.format),
                                                  .offset = attribute.offset});
        }
    }
    const VkPipelineVertexInputStateCreateInfo vertexInput{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = narrow<std::uint32_t>(vertexBindings.size()),
        .pVertexBindingDescriptions = vertexBindings.data(),
        .vertexAttributeDescriptionCount = narrow<std::uint32_t>(vertexAttributes.size()),
        .pVertexAttributeDescriptions = vertexAttributes.data()};
    const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = desc.topology == PrimitiveTopology::TriangleList ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
                                                                     : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
        .primitiveRestartEnable = VK_FALSE};
    const VkPipelineViewportStateCreateInfo viewport{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = nullptr, // dynamic
        .scissorCount = 1,
        .pScissors = nullptr};
    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = cullModeOf(desc.cullMode),
        .frontFace = desc.frontFace == FrontFace::CounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                                                   : VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0F,
        .depthBiasClamp = 0.0F,
        .depthBiasSlopeFactor = 0.0F,
        .lineWidth = 1.0F};
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 0.0F,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE};
    const VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthTestEnable = desc.depth ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = desc.depth && desc.depth->isWriteEnabled ? VK_TRUE : VK_FALSE,
        .depthCompareOp = desc.depth ? compareOpOf(desc.depth->compare) : VK_COMPARE_OP_ALWAYS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {},
        .back = {},
        .minDepthBounds = 0.0F,
        .maxDepthBounds = 1.0F};
    std::vector<VkPipelineColorBlendAttachmentState> blends;
    std::vector<VkFormat> colorFormats;
    for (const ColorTarget& target : desc.colorTargets) {
        colorFormats.push_back(vulkanFormatOf(target.format));
        const BlendState blend = target.blend.value_or(BlendState{});
        blends.push_back(VkPipelineColorBlendAttachmentState{
            .blendEnable = target.blend ? VK_TRUE : VK_FALSE,
            .srcColorBlendFactor = blendFactorOf(blend.sourceColor),
            .dstColorBlendFactor = blendFactorOf(blend.destinationColor),
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = blendFactorOf(blend.sourceAlpha),
            .dstAlphaBlendFactor = blendFactorOf(blend.destinationAlpha),
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT});
    }
    const VkPipelineColorBlendStateCreateInfo colorBlend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = narrow<std::uint32_t>(blends.size()),
        .pAttachments = blends.data(),
        .blendConstants = {0.0F, 0.0F, 0.0F, 0.0F}};
    constexpr std::array kDynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .dynamicStateCount = narrow<std::uint32_t>(kDynamicStates.size()),
        .pDynamicStates = kDynamicStates.data()};
    const VkPipelineRenderingCreateInfo rendering{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = nullptr,
        .viewMask = 0,
        .colorAttachmentCount = narrow<std::uint32_t>(colorFormats.size()),
        .pColorAttachmentFormats = colorFormats.data(),
        .depthAttachmentFormat = desc.depth ? vulkanFormatOf(desc.depth->format) : VK_FORMAT_UNDEFINED,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED};
    const VkGraphicsPipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                            .pNext = &rendering,
                                            .flags = 0,
                                            .stageCount = narrow<std::uint32_t>(stages.size()),
                                            .pStages = stages.data(),
                                            .pVertexInputState = &vertexInput,
                                            .pInputAssemblyState = &inputAssembly,
                                            .pTessellationState = nullptr,
                                            .pViewportState = &viewport,
                                            .pRasterizationState = &rasterization,
                                            .pMultisampleState = &multisample,
                                            .pDepthStencilState = &depthStencil,
                                            .pColorBlendState = &colorBlend,
                                            .pDynamicState = &dynamic,
                                            .layout = pipeline.layout,
                                            .renderPass = VK_NULL_HANDLE,
                                            .subpass = 0,
                                            .basePipelineHandle = VK_NULL_HANDLE,
                                            .basePipelineIndex = -1};
    if (const VkResult result =
            vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline.pipeline);
        result != VK_SUCCESS) {
        vkDestroyPipelineLayout(m_device, pipeline.layout, nullptr);
        return vulkanFailure(result, std::format("vkCreateGraphicsPipelines({:?})", desc.label));
    }
    name(VK_OBJECT_TYPE_PIPELINE, objectHandleOf(pipeline.pipeline), desc.label);
    return m_pipelines.insert(pipeline);
}

void VulkanDevice::destroy(BufferHandle buffer) {
    if (auto removed = m_buffers.remove(buffer)) {
        retire(std::move(*removed));
    }
}

void VulkanDevice::destroy(TextureHandle texture) {
    if (auto removed = m_textures.remove(texture)) {
        expects(removed->allocation != VK_NULL_HANDLE,
                "rhi::Device::destroy: a swapchain's image is the swapchain's");
        retire(std::move(*removed));
    }
}

void VulkanDevice::destroy(SamplerHandle sampler) {
    if (auto removed = m_samplers.remove(sampler)) {
        retire(std::move(*removed));
    }
}

void VulkanDevice::destroy(ShaderHandle shader) {
    if (auto removed = m_shaders.remove(shader)) {
        retire(std::move(*removed));
    }
}

void VulkanDevice::destroy(BindGroupLayoutHandle layout) {
    if (auto removed = m_bindGroupLayouts.remove(layout)) {
        retire(std::move(*removed));
    }
}

void VulkanDevice::destroy(BindGroupHandle group) {
    if (auto removed = m_bindGroups.remove(group)) {
        retire(std::move(*removed));
    }
}

void VulkanDevice::destroy(RenderPipelineHandle pipeline) {
    if (auto removed = m_pipelines.remove(pipeline)) {
        retire(std::move(*removed));
    }
}

auto VulkanDevice::mappedBytes(BufferHandle buffer) -> std::span<std::byte> {
    const std::span<std::byte> mapped = m_buffers.get(buffer).mapped;
    expects(!mapped.empty(), "rhi::Device::mappedBytes: an Upload or Readback buffer");
    return mapped;
}

// --- VulkanSwapchain -----------------------------------------------------------------------------

auto VulkanSwapchain::build(const SwapchainDesc& desc) -> Result<void, RhiError> {
    m_requestedSize = desc.size;
    m_presentMode = desc.presentMode;
    return rebuild();
}

auto VulkanSwapchain::rebuild() -> Result<void, RhiError> {
    const VkPhysicalDevice physical = m_device->physicalDevice();
    const VkSurfaceKHR surface = m_device->surface();
    const VkDevice device = m_device->handle();
    m_device->waitIdle(); // the old images may still be drawn or shown
    destroyImages();
    m_isOutOfDate = false;

    VkSurfaceCapabilitiesKHR capabilities{};
    if (const VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities);
        result != VK_SUCCESS) {
        return vulkanFailure(result, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    }
    // A current extent of 0xFFFFFFFF lets the swapchain choose; otherwise the surface decides.
    const bool isChosenBySwapchain =
        capabilities.currentExtent.width == std::numeric_limits<std::uint32_t>::max();
    m_extent = isChosenBySwapchain
                   ? Extent2d{.width = std::clamp(m_requestedSize.width, capabilities.minImageExtent.width,
                                                  capabilities.maxImageExtent.width),
                              .height = std::clamp(m_requestedSize.height, capabilities.minImageExtent.height,
                                                   capabilities.maxImageExtent.height)}
                   : Extent2d{.width = capabilities.currentExtent.width,
                              .height = capabilities.currentExtent.height};
    if (m_extent.width == 0 || m_extent.height == 0) {
        // Minimized: no swapchain until the window is restored and resized.
        if (m_swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, std::exchange(m_swapchain, VK_NULL_HANDLE), nullptr);
        }
        return {};
    }

    auto formats =
        enumerate<VkSurfaceFormatKHR>([physical, surface](std::uint32_t* count, VkSurfaceFormatKHR* values) {
            return vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, count, values);
        });
    auto modes =
        enumerate<VkPresentModeKHR>([physical, surface](std::uint32_t* count, VkPresentModeKHR* values) {
            return vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, count, values);
        });
    if (!formats || !modes) {
        return std::unexpected{!formats ? std::move(formats.error()) : std::move(modes.error())};
    }
    // UNORM, not sRGB: the gamma pass writes the GoldSrc curves' values as they are (docs/05 §11).
    const auto surfaceFormat = std::ranges::find_if(*formats, [](const VkSurfaceFormatKHR& candidate) {
        return (candidate.format == VK_FORMAT_B8G8R8A8_UNORM ||
                candidate.format == VK_FORMAT_R8G8B8A8_UNORM) &&
               candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (surfaceFormat == formats->end()) {
        return fail(RhiError::DriverRefused, "the surface has no 8-bit UNORM format in the sRGB color space");
    }
    m_format = formatOf(surfaceFormat->format);
    const VkPresentModeKHR wanted = presentModeOf(m_presentMode);
    const VkPresentModeKHR mode = std::ranges::contains(*modes, wanted) ? wanted : VK_PRESENT_MODE_FIFO_KHR;
    if (mode != wanted) {
        log::warning(kLog, "the surface cannot present in {} mode: presenting in Fifo",
                     enumName(m_presentMode));
    }
    std::uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount != 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }
    const VkCompositeAlphaFlagBitsKHR compositeAlpha =
        (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0
            ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
            : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

    const VkSwapchainKHR old = m_swapchain;
    const VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat->format,
        .imageColorSpace = surfaceFormat->colorSpace,
        .imageExtent = {.width = m_extent.width, .height = m_extent.height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = compositeAlpha,
        .presentMode = mode,
        .clipped = VK_TRUE,
        .oldSwapchain = old,
    };
    const VkResult created = vkCreateSwapchainKHR(device, &createInfo, nullptr, &m_swapchain);
    if (old != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, old, nullptr);
    }
    if (created != VK_SUCCESS) {
        m_swapchain = VK_NULL_HANDLE;
        return vulkanFailure(created, "vkCreateSwapchainKHR");
    }

    auto images = enumerate<VkImage>([device, this](std::uint32_t* count, VkImage* values) {
        return vkGetSwapchainImagesKHR(device, m_swapchain, count, values);
    });
    if (!images) {
        return std::unexpected{std::move(images.error())};
    }
    for (VkImage image : *images) {
        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = surfaceFormat->format,
            .components = {},
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1},
        };
        VkImageView view = VK_NULL_HANDLE;
        if (const VkResult result = vkCreateImageView(device, &viewInfo, nullptr, &view);
            result != VK_SUCCESS) {
            return vulkanFailure(result, "vkCreateImageView");
        }
        m_textures.push_back(m_device->textures().insert(
            VulkanTexture{.image = image, .view = view, .format = m_format, .extent = m_extent}));
        const VkSemaphoreCreateInfo semaphoreInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};
        VkSemaphore semaphore = VK_NULL_HANDLE;
        if (const VkResult result = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore);
            result != VK_SUCCESS) {
            return vulkanFailure(result, "vkCreateSemaphore");
        }
        m_renderFinished.push_back(semaphore);
    }
    log::debug(kLog, "swapchain: {} images of {}x{}, {}", m_textures.size(), m_extent.width, m_extent.height,
               enumName(m_format));
    return {};
}

void VulkanSwapchain::destroyImages() noexcept {
    const VkDevice device = m_device->handle();
    for (const TextureHandle texture : m_textures) {
        if (auto removed = m_device->textures().remove(texture)) {
            vkDestroyImageView(device, removed->view, nullptr); // the image is the swapchain's
        }
    }
    for (VkSemaphore semaphore : m_renderFinished) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    m_textures.clear();
    m_renderFinished.clear();
}

VulkanSwapchain::~VulkanSwapchain() {
    m_device->waitIdle();
    destroyImages();
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device->handle(), m_swapchain, nullptr);
    }
    m_device->swapchainClosed();
}

auto VulkanSwapchain::acquire() -> Result<std::optional<SwapchainImage>, RhiError> {
    expects(m_device->isRecording(), "rhi::Swapchain::acquire: between Device::beginFrame and submitFrame");
    expects(!m_acquired, "rhi::Swapchain::acquire: one image per frame");
    if (m_isOutOfDate) {
        if (auto rebuilt = rebuild(); !rebuilt) {
            return std::unexpected{std::move(rebuilt.error())};
        }
    }
    if (m_swapchain == VK_NULL_HANDLE) {
        return std::nullopt; // minimized
    }
    Frame& frame = m_device->currentFrame();
    std::uint32_t index = 0;
    const VkResult result = vkAcquireNextImageKHR(m_device->handle(), m_swapchain, kNoTimeout,
                                                  frame.imageAcquired, VK_NULL_HANDLE, &index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        m_isOutOfDate = true; // rebuilt at the next acquire
        return std::nullopt;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        return vulkanFailure(result, "vkAcquireNextImageKHR");
    }
    m_isOutOfDate = result == VK_SUBOPTIMAL_KHR; // still usable this frame
    m_acquired = index;
    frame.hasAcquiredImage = true;
    return SwapchainImage{.texture = m_textures[index], .size = m_extent, .format = m_format};
}

auto VulkanSwapchain::present(std::uint32_t image) -> Result<void, RhiError> {
    const VkSemaphore waitFor = m_renderFinished[image];
    const VkPresentInfoKHR presentInfo{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                       .pNext = nullptr,
                                       .waitSemaphoreCount = 1,
                                       .pWaitSemaphores = &waitFor,
                                       .swapchainCount = 1,
                                       .pSwapchains = &m_swapchain,
                                       .pImageIndices = &image,
                                       .pResults = nullptr};
    const VkResult result = vkQueuePresentKHR(m_device->queue(), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        m_isOutOfDate = true;
        return {};
    }
    if (result != VK_SUCCESS) {
        return vulkanFailure(result, "vkQueuePresentKHR");
    }
    return {};
}

auto VulkanSwapchain::resize(Extent2d size) -> Result<void, RhiError> {
    expects(!m_acquired, "rhi::Swapchain::resize: not while a frame holds its image");
    m_requestedSize = size;
    return rebuild();
}

} // namespace

auto createDevice(const DeviceDesc& desc) -> Result<std::unique_ptr<Device>, RhiError> {
    auto device = VulkanDevice::create(desc);
    if (!device) {
        return std::unexpected{std::move(device.error())};
    }
    return std::unique_ptr<Device>{std::move(*device)};
}

} // namespace nx::rhi::vulkan
