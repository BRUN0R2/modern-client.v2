// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

#include "vulkanApi.hpp"

module nx.rhi.vulkan;

import std;
import nx.core;
import nx.jobs;
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
/// The descriptors of each type a pool has room for, for each of its groups at most.
constexpr std::uint32_t kUniformBuffersPerGroup = 2;
constexpr std::uint32_t kStorageBuffersPerGroup = 1;
constexpr std::uint32_t kSampledImagesPerGroup = 4;
constexpr std::uint32_t kSamplersPerGroup = 2;

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
    case Format::Rg11b10Ufloat:
        return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case Format::Rgba16Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::Undefined:
        break;
    }
    return VK_FORMAT_UNDEFINED;
}

/// The Format of `format`; Undefined for a format the RHI does not name.
[[nodiscard]] auto formatOf(VkFormat format) noexcept -> Format {
    for (const Format candidate :
         {Format::Rgba8Unorm, Format::Bgra8Unorm, Format::Rgba8Srgb, Format::Bgra8Srgb, Format::Depth32Float,
          Format::Rg11b10Ufloat, Format::Rgba16Float}) {
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
    case VertexFormat::Float:
        return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Float2:
        return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::Uint8x4:
        return VK_FORMAT_R8G8B8A8_UINT;
    case VertexFormat::Uint32:
        return VK_FORMAT_R32_UINT;
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
    case BlendFactor::SecondSourceColor:
        return VK_BLEND_FACTOR_SRC1_COLOR;
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

/// The kinds of adapter, the one that wins first; any other kind comes after them all.
constexpr std::array kAdapterPreference{AdapterKind::Discrete, AdapterKind::Integrated, AdapterKind::Virtual,
                                        AdapterKind::Cpu};

/// Which adapter wins: the higher rank.
[[nodiscard]] auto rankOf(AdapterKind kind) noexcept -> std::size_t {
    return narrow<std::size_t>(
        std::ranges::distance(std::ranges::find(kAdapterPreference, kind), kAdapterPreference.end()));
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

// --- Pipeline states -----------------------------------------------------------------------------

/// The stage of `module` that runs `entryPoint`, which must outlive the pipeline's creation.
[[nodiscard]] auto stageOf(VkShaderStageFlagBits stage, VkShaderModule module,
                           const std::string& entryPoint) noexcept -> VkPipelineShaderStageCreateInfo {
    return VkPipelineShaderStageCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                           .pNext = nullptr,
                                           .flags = 0,
                                           .stage = stage,
                                           .module = module,
                                           .pName = entryPoint.c_str(),
                                           .pSpecializationInfo = nullptr};
}

/// The vertex buffers of a pipeline and their attributes, as Vulkan describes them.
struct VertexInput {
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;

    /// The state that points into this, which must outlive it.
    [[nodiscard]] auto state() const noexcept -> VkPipelineVertexInputStateCreateInfo {
        return VkPipelineVertexInputStateCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = narrow<std::uint32_t>(bindings.size()),
            .pVertexBindingDescriptions = bindings.data(),
            .vertexAttributeDescriptionCount = narrow<std::uint32_t>(attributes.size()),
            .pVertexAttributeDescriptions = attributes.data()};
    }
};

/// The vertex input of `buffers`, each bound at its slot, a vertex at a time.
[[nodiscard]] auto vertexInputOf(std::span<const VertexBufferLayout> buffers) -> VertexInput {
    VertexInput input;
    for (std::uint32_t slot = 0; slot < buffers.size(); ++slot) {
        const VertexBufferLayout& layout = buffers[slot];
        input.bindings.push_back(VkVertexInputBindingDescription{
            .binding = slot, .stride = layout.stride, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX});
        for (const VertexAttribute& attribute : layout.attributes) {
            input.attributes.push_back(
                VkVertexInputAttributeDescription{.location = attribute.location,
                                                  .binding = slot,
                                                  .format = vertexFormatOf(attribute.format),
                                                  .offset = attribute.offset});
        }
    }
    return input;
}

[[nodiscard]] auto inputAssemblyOf(PrimitiveTopology topology) noexcept
    -> VkPipelineInputAssemblyStateCreateInfo {
    VkPrimitiveTopology vulkanTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    switch (topology) {
    case PrimitiveTopology::TriangleList:
        vulkanTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        break;
    case PrimitiveTopology::TriangleStrip:
        vulkanTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        break;
    case PrimitiveTopology::LineList:
        vulkanTopology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        break;
    }
    return VkPipelineInputAssemblyStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = vulkanTopology,
        .primitiveRestartEnable = VK_FALSE};
}

/// Filled triangles, culled and facing as `desc` says, their depth biased as its depth state says.
[[nodiscard]] auto rasterizationOf(const RenderPipelineDesc& desc) noexcept
    -> VkPipelineRasterizationStateCreateInfo {
    const std::optional<DepthBias> bias = desc.depth ? desc.depth->bias : std::nullopt;
    return VkPipelineRasterizationStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = cullModeOf(desc.cullMode),
        .frontFace = desc.frontFace == FrontFace::CounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                                                   : VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = bias ? VK_TRUE : VK_FALSE,
        .depthBiasConstantFactor = bias ? bias->constant : 0.0F,
        .depthBiasClamp = 0.0F,
        .depthBiasSlopeFactor = bias ? bias->slope : 0.0F,
        .lineWidth = 1.0F};
}

/// The depth test of `depth`; none without one. No stencil.
[[nodiscard]] auto depthStencilOf(const std::optional<DepthState>& depth) noexcept
    -> VkPipelineDepthStencilStateCreateInfo {
    return VkPipelineDepthStencilStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthTestEnable = depth ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = depth && depth->isWriteEnabled ? VK_TRUE : VK_FALSE,
        .depthCompareOp = depth ? compareOpOf(depth->compare) : VK_COMPARE_OP_ALWAYS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {},
        .back = {},
        .minDepthBounds = 0.0F,
        .maxDepthBounds = 1.0F};
}

/// How a color target blends: as its BlendState, adding; or not at all. Every channel is written.
[[nodiscard]] auto colorBlendOf(const ColorTarget& target) noexcept -> VkPipelineColorBlendAttachmentState {
    const BlendState blend = target.blend.value_or(BlendState{});
    return VkPipelineColorBlendAttachmentState{.blendEnable = target.blend ? VK_TRUE : VK_FALSE,
                                               .srcColorBlendFactor = blendFactorOf(blend.sourceColor),
                                               .dstColorBlendFactor = blendFactorOf(blend.destinationColor),
                                               .colorBlendOp = VK_BLEND_OP_ADD,
                                               .srcAlphaBlendFactor = blendFactorOf(blend.sourceAlpha),
                                               .dstAlphaBlendFactor = blendFactorOf(blend.destinationAlpha),
                                               .alphaBlendOp = VK_BLEND_OP_ADD,
                                               .colorWriteMask =
                                                   VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
}

/// One viewport and one scissor, set while recording (kViewportAndScissorState).
constexpr VkPipelineViewportStateCreateInfo kDynamicViewportState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .viewportCount = 1,
    .pViewports = nullptr,
    .scissorCount = 1,
    .pScissors = nullptr};

/// One sample a pixel.
constexpr VkPipelineMultisampleStateCreateInfo kSingleSampleState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    .sampleShadingEnable = VK_FALSE,
    .minSampleShading = 0.0F,
    .pSampleMask = nullptr,
    .alphaToCoverageEnable = VK_FALSE,
    .alphaToOneEnable = VK_FALSE};

constexpr std::array kDynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

/// The viewport and the scissor, set while recording.
constexpr VkPipelineDynamicStateCreateInfo kViewportAndScissorState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .dynamicStateCount = narrow<std::uint32_t>(kDynamicStates.size()),
    .pDynamicStates = kDynamicStates.data()};

// --- Adapters -------------------------------------------------------------------------------------

/// What an adapter offers, as Vulkan reports it: its properties and the features the engine
/// requires. Its structures chain to one another, so it is neither copied nor moved.
class AdapterProbe {
public:
    explicit AdapterProbe(VkPhysicalDevice adapter) noexcept {
        m_properties12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
        m_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        m_properties.pNext = &m_properties12;
        vkGetPhysicalDeviceProperties2(adapter, &m_properties);
        m_features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        m_features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        m_features12.pNext = &m_features11;
        m_features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        m_features13.pNext = &m_features12;
        m_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        m_features.pNext = &m_features13;
        vkGetPhysicalDeviceFeatures2(adapter, &m_features);
    }
    AdapterProbe(const AdapterProbe&) = delete;
    AdapterProbe(AdapterProbe&&) = delete;
    auto operator=(const AdapterProbe&) -> AdapterProbe& = delete;
    auto operator=(AdapterProbe&&) -> AdapterProbe& = delete;
    ~AdapterProbe() = default;

    [[nodiscard]] auto name() const noexcept -> std::string_view {
        return textOf(m_properties.properties.deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);
    }
    [[nodiscard]] auto kind() const noexcept -> AdapterKind {
        return adapterKindOf(m_properties.properties.deviceType);
    }

    /// What the adapter lacks of what the engine requires: Vulkan 1.3 with dynamic rendering,
    /// synchronization2, timeline semaphores, shader draw parameters, cube arrays and dual-source
    /// blending (the last two every Direct3D 11 card has); nullopt when nothing.
    [[nodiscard]] auto missingRequirement() const -> std::optional<std::string> {
        if (const std::uint32_t version = m_properties.properties.apiVersion; version < kRequiredApiVersion) {
            return std::format("Vulkan {}.{}", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version));
        }
        if (m_features13.dynamicRendering == VK_FALSE || m_features13.synchronization2 == VK_FALSE ||
            m_features12.timelineSemaphore == VK_FALSE || m_features11.shaderDrawParameters == VK_FALSE ||
            m_features.features.imageCubeArray == VK_FALSE || m_features.features.dualSrcBlend == VK_FALSE) {
            return "no dynamic rendering, synchronization2, timeline semaphores, shader draw parameters, "
                   "cube arrays or dual-source blending";
        }
        return std::nullopt;
    }

    [[nodiscard]] auto hasAnisotropy() const noexcept -> bool {
        return m_features.features.samplerAnisotropy == VK_TRUE;
    }
    [[nodiscard]] auto maxAnisotropy() const noexcept -> float {
        return m_properties.properties.limits.maxSamplerAnisotropy;
    }

    [[nodiscard]] auto info() const -> AdapterInfo {
        return AdapterInfo{
            .name = std::string{name()},
            .kind = kind(),
            .driver = std::format("{} {}", textOf(m_properties12.driverName, VK_MAX_DRIVER_NAME_SIZE),
                                  textOf(m_properties12.driverInfo, VK_MAX_DRIVER_INFO_SIZE)),
        };
    }

private:
    VkPhysicalDeviceVulkan12Properties m_properties12{};
    VkPhysicalDeviceProperties2 m_properties{};
    VkPhysicalDeviceVulkan11Features m_features11{};
    VkPhysicalDeviceVulkan12Features m_features12{};
    VkPhysicalDeviceVulkan13Features m_features13{};
    VkPhysicalDeviceFeatures2 m_features{};
};

/// Whether `adapter` can make a swapchain. Errors: those of the enumeration.
[[nodiscard]] auto hasSwapchainExtension(VkPhysicalDevice adapter) -> Result<bool, RhiError> {
    auto extensions =
        enumerate<VkExtensionProperties>([adapter](std::uint32_t* count, VkExtensionProperties* values) {
            return vkEnumerateDeviceExtensionProperties(adapter, nullptr, count, values);
        });
    if (!extensions) {
        return std::unexpected{std::move(extensions.error())};
    }
    return hasName(*extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
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
    /// A render target of more than one mip level or layer: the view of each mip level of each layer,
    /// which a pass draws to, at mipLevel * arrayLayers + arrayLayer. None otherwise.
    std::vector<VkImageView> attachmentViews{};
    VmaAllocation allocation = VK_NULL_HANDLE; ///< None for the window's image.
    Format format = Format::Undefined;
    Extent2d extent;
    std::uint32_t mipLevels = 1;
    std::uint32_t arrayLayers = 1;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    /// What its last transition recorded left it in: what a transition from Undefined waits for.
    TextureState lastState = TextureState::Undefined;
    /// The window's next image (Swapchain::acquire), which no image or view backs: the submission
    /// thread draws the frame's pass of it on the image it acquires (docs/adr/0029-submission-thread.md).
    bool isWindowImage = false;
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

/// The bind groups and the vertex buffer slots whose bindings a command list remembers: as many bind
/// groups as every Vulkan device can bind (maxBoundDescriptorSets is at least 4), and as many slots.
/// A binding beyond them is recorded every time.
constexpr std::size_t kRememberedBindGroups = 4;
constexpr std::size_t kRememberedVertexSlots = 4;

/// A vertex or index buffer bound, and where its data starts.
struct BoundBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    std::uint64_t offset = 0;

    [[nodiscard]] auto operator==(const BoundBuffer&) const noexcept -> bool = default;
};

/// What a command buffer has bound as its commands left it, to record no command that would bind the
/// same again: the driver does a binding's work whether or not it changes anything (docs/05-rendering.md,
/// section 12). Vulkan keeps all of it from one render pass to the next of a command buffer.
struct BoundState {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE; ///< The bound pipeline's.
    /// The layout `groups` were bound with. A group bound with another layout may disturb those bound
    /// with this one, so that none of them is known any more.
    VkPipelineLayout groupsLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kRememberedBindGroups> groups{};
    std::array<BoundBuffer, kRememberedVertexSlots> vertices{};
    BoundBuffer indices;
    VkIndexType indexType = VK_INDEX_TYPE_UINT32;
    std::optional<Rect> viewport;
    std::optional<Rect> scissor;
};

/// The render pass a frame draws to the window's image in, as the submission thread begins it on the
/// image it acquires: its commands are recorded, its image is not (docs/adr/0029-submission-thread.md).
struct WindowPass {
    VkCommandBuffer commands = VK_NULL_HANDLE; ///< Secondary: what the pass draws.
    VkRenderingAttachmentInfo color{};         ///< All but the view of the window's image.
    VkRenderingAttachmentInfo depth{};
    bool hasDepth = false;
    VkExtent2D area{};
};

/// Where a frame's commands go, around its pass that draws to the window's image.
enum class WindowPassState : std::uint8_t {
    Before,  ///< Before that pass, or with none: to the frame's primary command buffer.
    Drawing, ///< In it: to its secondary command buffer.
    Lost,    ///< In it, its command buffer refused to begin: nowhere, and the frame fails.
    Done,    ///< After it: nothing more, the window's transition to Present aside.
};

class VulkanCommandList final : public CommandList {
public:
    explicit VulkanCommandList(VulkanDevice& device) noexcept : m_device{&device} {}

    /// Records into `frameCommands`, which a new recording leaves with nothing bound; the frame's pass
    /// that draws to the window's image, if it has one, into the secondary command buffer
    /// `windowCommands`.
    void startRecording(VkCommandBuffer frameCommands, VkCommandBuffer windowCommands) noexcept {
        m_commands = frameCommands;
        m_windowCommands = windowCommands;
        m_windowPassState = WindowPassState::Before;
        m_windowPass = {};
        m_failure.reset();
        m_bound = {};
        m_imageBarriers.clear();
        m_bufferBarriers.clear();
    }

    /// Records what is left queued and ends the window pass's command buffer, before the frame's own
    /// ends. Returns the window's pass the frame drew, none without one. Errors: OutOfHostMemory,
    /// OutOfDeviceMemory (beginning or ending the window pass's command buffer).
    [[nodiscard]] auto finishRecording() -> Result<std::optional<WindowPass>, RhiError>;

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
    /// Records the barriers queued by the transitions since the last command, all in one dependency.
    void recordBarriers();
    /// The window's pass is the frame's last: nothing is recorded after it.
    void expectsWindowPassNotDone() const;
    /// The command buffer the frame's commands go to now; none in a lost window's pass.
    [[nodiscard]] auto commands() const -> VkCommandBuffer;
    /// Starts recording `desc`, whose only color attachment is the window's image, into the window
    /// pass's command buffer.
    void beginWindowPass(const RenderingDesc& desc);

    VulkanDevice* m_device;
    VkCommandBuffer m_commands = VK_NULL_HANDLE;
    VkCommandBuffer m_windowCommands = VK_NULL_HANDLE;
    WindowPassState m_windowPassState = WindowPassState::Before;
    WindowPass m_windowPass;
    std::optional<Failure<RhiError>> m_failure; ///< Why the window's pass was lost.
    BoundState m_bound;
    /// The transitions' barriers, queued until a command needs them: one call records those in a row.
    /// The barriers of a dependency are unordered among themselves, so a second one of the same image
    /// or buffer records those queued first.
    std::vector<VkImageMemoryBarrier2> m_imageBarriers;
    std::vector<VkBufferMemoryBarrier2> m_bufferBarriers;
};

/// What one frame in flight owns.
struct Frame {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;       ///< Primary: all but the window's pass.
    VkCommandBuffer windowCommands = VK_NULL_HANDLE; ///< Secondary: the window's pass.
    std::uint64_t submitted = 0;                     ///< The timeline value of its last submission.
    bool hasAcquiredImage = false;                   ///< Swapchain::acquire gave it the window's image.
};

/// The command buffer the submission thread draws a frame's window pass in, one per frame in flight:
/// that thread's alone.
struct PresentCommands {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
};

class VulkanSwapchain;

/// A frame handed to the submission thread (Device::submitFrame), in the order frames are handed.
struct HandedFrame {
    VkCommandBuffer commands = VK_NULL_HANDLE;
    std::uint64_t timelineValue = 0; ///< What the frame signals once the GPU has run it.
    std::size_t slot = 0;            ///< Its frame in flight.
    /// The swapchain whose next image the window's pass draws on, then shows; none for a frame that
    /// draws nothing to a window.
    VulkanSwapchain* presentTo = nullptr;
    WindowPass windowPass{};
};

/// The frames handed to the submission thread and not taken yet: at most one, since a frame begins
/// only once the one before is taken (Device::beginFrame).
constexpr std::size_t kHandOffCapacity = 2;

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
    [[nodiscard]] auto pipelineCache() const -> Result<std::vector<std::byte>, RhiError> override;

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

    /// Waits until the submission thread has finished every frame handed to it: then, until another
    /// frame is handed, the render thread alone uses the queue and the swapchain.
    void drain() noexcept;
    /// The first failure of the submission thread, which every frame after it shares.
    [[nodiscard]] auto submissionFailure() const -> Result<void, RhiError>;
    /// Waits on `semaphore`, signaled or about to be, with nothing else: it is then free. From a drained
    /// render thread only. Errors: those of vkQueueSubmit2.
    [[nodiscard]] auto consume(VkSemaphore semaphore) -> Result<void, RhiError>;

    void swapchainOpened() noexcept { ++m_openSwapchains; }
    void swapchainClosed() noexcept { --m_openSwapchains; }

    void countValidationError() noexcept { ++m_validationErrors; }

protected:
    [[nodiscard]] auto makeBuffer(const BufferDesc& desc) -> Result<BufferHandle, RhiError> override;
    [[nodiscard]] auto makeTexture(const TextureDesc& desc) -> Result<TextureHandle, RhiError> override;
    [[nodiscard]] auto makeSampler(const SamplerDesc& desc) -> Result<SamplerHandle, RhiError> override;
    [[nodiscard]] auto makeShader(const ShaderDesc& desc) -> Result<ShaderHandle, RhiError> override;
    [[nodiscard]] auto makeBindGroupLayout(const BindGroupLayoutDesc& desc)
        -> Result<BindGroupLayoutHandle, RhiError> override;
    [[nodiscard]] auto makeBindGroup(const BindGroupDesc& desc) -> Result<BindGroupHandle, RhiError> override;
    [[nodiscard]] auto makeRenderPipeline(const RenderPipelineDesc& desc)
        -> Result<RenderPipelineHandle, RhiError> override;
    void destroy(BufferHandle buffer) override;
    void destroy(TextureHandle texture) override;
    void destroy(SamplerHandle sampler) override;
    void destroy(ShaderHandle shader) override;
    void destroy(BindGroupLayoutHandle layout) override;
    void destroy(BindGroupHandle group) override;
    void destroy(RenderPipelineHandle pipeline) override;

private:
    [[nodiscard]] auto createInstance(const DeviceDesc& desc) -> Result<void, RhiError>;
    [[nodiscard]] auto createSurface(const platform::NativeWindow& window) -> Result<void, RhiError>;
    [[nodiscard]] auto chooseAdapter() -> Result<void, RhiError>;
    /// The first queue family of `candidate` that draws and, with a surface, presents to it; nullopt
    /// when none does. Errors: those of the surface query.
    [[nodiscard]] auto graphicsFamilyOf(VkPhysicalDevice candidate) const
        -> Result<std::optional<std::uint32_t>, RhiError>;
    [[nodiscard]] auto createLogicalDevice() -> Result<void, RhiError>;
    [[nodiscard]] auto createFrames(std::uint32_t count) -> Result<void, RhiError>;
    [[nodiscard]] auto createAllocator() -> Result<void, RhiError>;
    /// The pipeline cache, holding `learned` when this GPU and driver wrote it (fitsThisDevice).
    [[nodiscard]] auto createPipelineCache(std::span<const std::byte> learned) -> Result<void, RhiError>;
    /// Whether `learned` is a pipeline cache this GPU and driver wrote: its header's version, vendor,
    /// device and cache UUID theirs.
    [[nodiscard]] auto fitsThisDevice(std::span<const std::byte> learned) const noexcept -> bool;
    /// Hands the frame to the submission thread, to show on `presentTo` when it has one.
    [[nodiscard]] auto submit(VulkanSwapchain* presentTo) -> Result<void, RhiError>;

    // The submission thread (docs/adr/0029-submission-thread.md): it takes the frames handed to it in
    // order, submits them, and draws each one's window pass on the image it acquires, then shows it.
    void runSubmissions(const std::stop_token& stop);
    /// Submits `frame`, and shows it when it draws to a window. Errors: those of vkQueueSubmit2,
    /// vkAcquireNextImageKHR and vkQueuePresentKHR, and of recording the window's pass.
    [[nodiscard]] auto submitHanded(const HandedFrame& frame) -> Result<void, RhiError>;
    /// Waits until the submission thread has taken every frame handed to it.
    void waitUntilTaken() noexcept;

    /// The layout of the bind groups of `desc`.
    [[nodiscard]] auto createPipelineLayout(const RenderPipelineDesc& desc)
        -> Result<VkPipelineLayout, RhiError>;

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
    VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
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

    // Between the render thread and the submission thread: the frames handed over, counts that only
    // grow, and the first failure. A count changes after what it counts, so that a thread that reads
    // it sees that too.
    jobs::SpscQueue<HandedFrame, kHandOffCapacity> m_handOff;
    std::uint64_t m_handedFrames = 0;        ///< The render thread's own.
    std::atomic<std::uint64_t> m_wakeups{0}; ///< A frame handed, or the thread asked to stop.
    std::atomic<std::uint64_t> m_takenFrames{0};
    std::atomic<std::uint64_t> m_finishedFrames{0};
    std::atomic<bool> m_hasFailed{false};
    std::optional<Failure<RhiError>> m_submissionFailure; ///< Written once, before m_hasFailed.
    std::vector<PresentCommands> m_presentCommands;       ///< One per frame in flight.
    platform::Thread m_submission;                        ///< Last: started once the rest is made.
};

// --- Swapchains -----------------------------------------------------------------------------------

/// The size of a swapchain's images: the surface's, or `requested` held to its limits when the
/// surface lets the swapchain choose (a current extent of 0xFFFFFFFF).
[[nodiscard]] auto extentFor(const VkSurfaceCapabilitiesKHR& capabilities, Extent2d requested) noexcept
    -> Extent2d {
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        return Extent2d{.width = capabilities.currentExtent.width,
                        .height = capabilities.currentExtent.height};
    }
    return Extent2d{.width = std::clamp(requested.width, capabilities.minImageExtent.width,
                                        capabilities.maxImageExtent.width),
                    .height = std::clamp(requested.height, capabilities.minImageExtent.height,
                                         capabilities.maxImageExtent.height)};
}

/// How many images a swapchain asks for: one more than the fewest the surface takes, so that a frame
/// need not wait for the presentation of the one before; no more than the surface allows.
[[nodiscard]] auto imageCountFor(const VkSurfaceCapabilitiesKHR& capabilities) noexcept -> std::uint32_t {
    constexpr std::uint32_t kExtraImages = 1;
    const std::uint32_t count = capabilities.minImageCount + kExtraImages;
    return capabilities.maxImageCount != 0 ? std::min(count, capabilities.maxImageCount) : count;
}

/// The semaphores a swapchain's acquires signal in turn: one more than the most frames in flight, so
/// that the one an acquire takes was last waited on by a frame the render thread has seen finish
/// before it began the frame being drawn.
constexpr std::size_t kAcquireSemaphores = kMaxFramesInFlight + 1;

/// An image of the swapchain acquired and not shown yet, and the semaphore its acquire signals.
struct HeldImage {
    std::uint32_t index = 0;
    VkSemaphore acquired = VK_NULL_HANDLE;
};

/// The format a swapchain draws in, and how it presents.
struct SurfaceUse {
    VkSurfaceFormatKHR format{};
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
};

/// An 8-bit UNORM format of the sRGB color space, which `surface` must have: the gamma pass writes
/// the GoldSrc curves' values as they are (docs/05 section 11); and `wanted`, else Fifo, which every
/// surface has, with a warning. Errors: DriverRefused (no such format), and those of the queries.
[[nodiscard]] auto chooseSurfaceUse(VkPhysicalDevice physical, VkSurfaceKHR surface, PresentMode wanted)
    -> Result<SurfaceUse, RhiError> {
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
    const auto format = std::ranges::find_if(*formats, [](const VkSurfaceFormatKHR& candidate) {
        return (candidate.format == VK_FORMAT_B8G8R8A8_UNORM ||
                candidate.format == VK_FORMAT_R8G8B8A8_UNORM) &&
               candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (format == formats->end()) {
        return fail(RhiError::DriverRefused, "the surface has no 8-bit UNORM format in the sRGB color space");
    }
    SurfaceUse use{.format = *format, .mode = presentModeOf(wanted)};
    if (!std::ranges::contains(*modes, use.mode)) {
        log::warning(kLog, "the surface cannot present in {} mode: presenting in Fifo", enumName(wanted));
        use.mode = VK_PRESENT_MODE_FIFO_KHR;
    }
    return use;
}

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

    // The submission thread's (docs/adr/0029-submission-thread.md); the render thread changes what
    // they use only once that thread is drained.

    /// The image to draw a frame's window pass on: the one acquired ahead, or one acquired now; none
    /// while the swapchain is out of date or minimized. Errors: those of vkAcquireNextImageKHR.
    [[nodiscard]] auto takeImage() -> Result<std::optional<HeldImage>, RhiError>;
    /// Holds again `image`, taken and never drawn on: for the next frame, or to be let go of.
    void giveBack(HeldImage image) noexcept;
    /// Acquires the next image, for the frame after the one just shown; none while out of date.
    /// Errors: those of vkAcquireNextImageKHR.
    [[nodiscard]] auto acquireAhead() -> Result<void, RhiError>;
    [[nodiscard]] auto image(std::uint32_t index) const noexcept -> VkImage { return m_images[index]; }
    [[nodiscard]] auto view(std::uint32_t index) const noexcept -> VkImageView { return m_views[index]; }
    [[nodiscard]] auto renderFinished(std::uint32_t index) const noexcept -> VkSemaphore {
        return m_renderFinished[index];
    }
    /// Shows image `index` once its semaphore is signaled; an out-of-date swapchain is marked.
    /// Errors: those of vkQueuePresentKHR.
    [[nodiscard]] auto present(std::uint32_t index) -> Result<void, RhiError>;

private:
    [[nodiscard]] auto rebuild() -> Result<void, RhiError>;
    /// The images of the swapchain just made, in `format`: a view and a semaphore for each.
    [[nodiscard]] auto createImages(VkFormat format) -> Result<void, RhiError>;
    void destroyImages() noexcept;
    /// Lets go of the image acquired ahead, if any, which will never be shown: its semaphore is waited
    /// on, so that none is destroyed or reused with a signal pending. Once the submission thread is
    /// drained.
    void releaseHeldImage() noexcept;
    /// The next image, acquired now; none while out of date or minimized. On the submission thread.
    [[nodiscard]] auto acquireNow() -> Result<std::optional<HeldImage>, RhiError>;

    VulkanDevice* m_device;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_images;             ///< The swapchain's own.
    std::vector<VkImageView> m_views;          ///< One per image, which the window's pass draws to.
    std::vector<VkSemaphore> m_renderFinished; ///< One per image: what its presentation waits for.
    /// What each acquire signals once its image is ready, taken in turn: one is used again only once
    /// the frame that waited on it has finished.
    std::array<VkSemaphore, kAcquireSemaphores> m_acquired{};
    std::size_t m_nextAcquired = 0;
    std::optional<HeldImage> m_held; ///< Acquired ahead, for the next frame that draws to the window.
    TextureHandle m_window;          ///< What a frame draws to the window's image through.
    Format m_format = Format::Undefined;
    Extent2d m_extent;
    Extent2d m_requestedSize;
    PresentMode m_presentMode = PresentMode::Fifo;
    std::atomic<bool> m_isOutOfDate{false}; ///< Rebuilt at the render thread's next acquire.
};

VKAPI_ATTR auto VKAPI_CALL onValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                               VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                               const VkDebugUtilsMessengerCallbackDataEXT* data, void* device)
    -> VkBool32 {
    const std::string_view message = data != nullptr && data->pMessage != nullptr ? data->pMessage : "";
    // The messenger hears warnings and errors only.
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        static_cast<VulkanDevice*>(device)->countValidationError();
        log::error(kLog, "Vulkan validation: {}", message);
    } else {
        log::warning(kLog, "Vulkan validation: {}", message);
    }
    return VK_FALSE; // the call goes on, as without validation
}

// --- The instance ---------------------------------------------------------------------------------

/// Starts the Vulkan loader, which must offer version 1.3. Errors: BackendUnavailable.
[[nodiscard]] auto startLoader() -> Result<void, RhiError> {
    if (const VkResult loaded = volkInitialize(); loaded != VK_SUCCESS) {
        return fail(RhiError::BackendUnavailable,
                    "no Vulkan loader: no driver installs one (vulkan-1.dll, libvulkan.so.1)");
    }
    if (const std::uint32_t version = volkGetInstanceVersion(); version < kRequiredApiVersion) {
        return fail(RhiError::BackendUnavailable,
                    std::format("the Vulkan loader offers {}.{}; 1.3 is required",
                                VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version)));
    }
    return {};
}

/// The instance extensions that present to `window`, which `available` must have.
/// Errors: SurfaceUnavailable.
[[nodiscard]] auto surfaceExtensionsFor(const platform::NativeWindow& window,
                                        std::span<const VkExtensionProperties> available)
    -> Result<std::vector<const char*>, RhiError> {
    std::vector<const char*> extensions;
    for (const std::string_view needed : {std::string_view{"VK_KHR_surface"}, surfaceExtensionOf(window)}) {
        if (!hasName(available, needed)) {
            return fail(RhiError::SurfaceUnavailable, std::format("the Vulkan loader has no {}", needed));
        }
        extensions.push_back(needed.data()); // the literals above are zero-terminated
    }
    return extensions;
}

/// What an instance asks for to be validated: the layer, the extension of its messenger and, when
/// the layer has it, the extension of its features (synchronization validation).
struct Validation {
    std::vector<const char*> layers;
    std::vector<const char*> extensions;
    bool hasFeatures = false;
};

/// The requested validation of the Khronos layer and its messenger. Errors: ValidationUnavailable
/// when either is absent; those of the loader's enumerations.
[[nodiscard]] auto validationOf(std::span<const VkExtensionProperties> available)
    -> Result<Validation, RhiError> {
    auto installed = enumerate<VkLayerProperties>([](std::uint32_t* count, VkLayerProperties* values) {
        return vkEnumerateInstanceLayerProperties(count, values);
    });
    if (!installed) {
        return std::unexpected{std::move(installed.error())};
    }
    const bool hasLayer = std::ranges::any_of(*installed, [](const VkLayerProperties& layer) {
        return textOf(layer.layerName, VK_MAX_EXTENSION_NAME_SIZE) == kValidationLayer;
    });
    if (!hasLayer) {
        return fail(RhiError::ValidationUnavailable,
                    std::format("requested Vulkan validation requires {}", kValidationLayer));
    }
    if (!hasName(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        return fail(RhiError::ValidationUnavailable,
                    "requested Vulkan validation requires VK_EXT_debug_utils");
    }
    auto layerExtensions =
        enumerate<VkExtensionProperties>([](std::uint32_t* count, VkExtensionProperties* values) {
            return vkEnumerateInstanceExtensionProperties(kValidationLayer.data(), count, values);
        });
    if (!layerExtensions) {
        return std::unexpected{std::move(layerExtensions.error())};
    }
    Validation validation{.layers = {kValidationLayer.data()},
                          .extensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME}};
    validation.hasFeatures = hasName(*layerExtensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
    if (validation.hasFeatures) {
        validation.extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
    }
    return validation;
}

constexpr std::array kValidationFeatures{VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT};

/// Synchronization validation, beside the layer's default checks.
constexpr VkValidationFeaturesEXT kSynchronizationValidation{
    .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
    .pNext = nullptr,
    .enabledValidationFeatureCount = narrow<std::uint32_t>(kValidationFeatures.size()),
    .pEnabledValidationFeatures = kValidationFeatures.data(),
    .disabledValidationFeatureCount = 0,
    .pDisabledValidationFeatures = nullptr,
};

/// A messenger that hands the warnings and errors of validation to onValidationMessage, for `device`.
[[nodiscard]] auto messengerInfoFor(VulkanDevice& device) noexcept -> VkDebugUtilsMessengerCreateInfoEXT {
    return VkDebugUtilsMessengerCreateInfoEXT{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = &onValidationMessage,
        .pUserData = &device,
    };
}

// --- VulkanCommandList ---------------------------------------------------------------------------

void VulkanCommandList::expectsWindowPassNotDone() const {
    expects(m_windowPassState != WindowPassState::Done,
            "CommandList: the pass that draws to the window's image is the frame's last");
}

auto VulkanCommandList::commands() const -> VkCommandBuffer {
    expectsWindowPassNotDone();
    return m_commands;
}

void VulkanCommandList::transition(TextureHandle texture, TextureState before, TextureState after) {
    expects(after != TextureState::Undefined, "CommandList::transition: a texture cannot become Undefined");
    VulkanTexture& target = m_device->textures().get(texture);
    if (target.isWindowImage) {
        // The submission thread readies the image it acquires for the window's pass, then to be shown.
        const bool isBeforePass = before == TextureState::Undefined && after == TextureState::RenderTarget &&
                                  m_windowPassState == WindowPassState::Before;
        const bool isAfterPass = before == TextureState::RenderTarget && after == TextureState::Present &&
                                 m_windowPassState == WindowPassState::Done;
        expects(isBeforePass || isAfterPass,
                "CommandList::transition: the window's image goes from Undefined "
                "to RenderTarget before its pass, and to Present after it");
        return;
    }
    expectsWindowPassNotDone();
    // From Undefined the old content is gone, but its last use, by a frame before maybe, still comes
    // first: no frame waits for the one before as a whole.
    const StateAccess last = accessOf(before == TextureState::Undefined ? target.lastState : before);
    const StateAccess from{.stages = last.stages, .access = last.access, .layout = accessOf(before).layout};
    const StateAccess to = accessOf(after);
    target.lastState = after;
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
    if (std::ranges::contains(m_imageBarriers, target.image, &VkImageMemoryBarrier2::image)) {
        recordBarriers();
    }
    m_imageBarriers.push_back(barrier);
}

void VulkanCommandList::recordBarriers() {
    if (m_imageBarriers.empty() && m_bufferBarriers.empty()) {
        return;
    }
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = nullptr,
        .bufferMemoryBarrierCount = narrow<std::uint32_t>(m_bufferBarriers.size()),
        .pBufferMemoryBarriers = m_bufferBarriers.data(),
        .imageMemoryBarrierCount = narrow<std::uint32_t>(m_imageBarriers.size()),
        .pImageMemoryBarriers = m_imageBarriers.data(),
    };
    if (const VkCommandBuffer recording = commands(); recording != VK_NULL_HANDLE) { // none: a lost pass
        vkCmdPipelineBarrier2(recording, &dependency);
    }
    m_imageBarriers.clear();
    m_bufferBarriers.clear();
}

/// The view a pass draws `color` to, of `texture`: its own, or that of the mip level of the layer.
[[nodiscard]] auto attachmentViewOf(const VulkanTexture& texture, const ColorAttachment& color)
    -> VkImageView {
    expects(color.mipLevel < texture.mipLevels && color.arrayLayer < texture.arrayLayers,
            "CommandList::beginRendering: a mip level and a layer of the attachment");
    if (texture.attachmentViews.empty()) {
        return texture.view;
    }
    return texture.attachmentViews[(std::size_t{color.mipLevel} * texture.arrayLayers) + color.arrayLayer];
}

/// What a pass does with `color`, drawn through `view`.
[[nodiscard]] auto renderingAttachmentOf(const ColorAttachment& color, VkImageView view) noexcept
    -> VkRenderingAttachmentInfo {
    VkRenderingAttachmentInfo attachment{};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.resolveMode = VK_RESOLVE_MODE_NONE;
    attachment.loadOp = loadOpOf(color.load);
    attachment.storeOp = storeOpOf(color.store);
    attachment.clearValue.color.float32[0] = color.clear.red;
    attachment.clearValue.color.float32[1] = color.clear.green;
    attachment.clearValue.color.float32[2] = color.clear.blue;
    attachment.clearValue.color.float32[3] = color.clear.alpha;
    return attachment;
}

/// What a pass does with `depth`, drawn through `view`.
[[nodiscard]] auto renderingAttachmentOf(const DepthAttachment& depth, VkImageView view) noexcept
    -> VkRenderingAttachmentInfo {
    VkRenderingAttachmentInfo attachment{};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    attachment.resolveMode = VK_RESOLVE_MODE_NONE;
    attachment.loadOp = loadOpOf(depth.load);
    attachment.storeOp = storeOpOf(depth.store);
    attachment.clearValue.depthStencil.depth = depth.clear;
    return attachment;
}

void VulkanCommandList::beginRendering(const RenderingDesc& desc) {
    expects(desc.colors.size() <= kMaxColorAttachments,
            "CommandList::beginRendering: at most 8 color attachments");
    if (std::ranges::any_of(desc.colors, [this](const ColorAttachment& color) {
            return m_device->textures().get(color.texture).isWindowImage;
        })) {
        beginWindowPass(desc);
        return;
    }
    recordBarriers();
    std::array<VkRenderingAttachmentInfo, kMaxColorAttachments> attachments{};
    for (std::size_t index = 0; index < desc.colors.size(); ++index) {
        const ColorAttachment& color = desc.colors[index];
        attachments.at(index) =
            renderingAttachmentOf(color, attachmentViewOf(m_device->textures().get(color.texture), color));
    }
    VkRenderingAttachmentInfo depth{};
    if (desc.depth) {
        depth = renderingAttachmentOf(*desc.depth, m_device->textures().get(desc.depth->texture).view);
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
    vkCmdBeginRendering(commands(), &rendering);
    const Rect whole{.x = 0, .y = 0, .size = desc.area};
    setViewport(whole);
    setScissor(whole);
}

void VulkanCommandList::beginWindowPass(const RenderingDesc& desc) {
    expects(desc.colors.size() == 1,
            "CommandList::beginRendering: the window's image is its pass's only color attachment");
    expects(m_windowPassState == WindowPassState::Before,
            "CommandList::beginRendering: the window's image is drawn in one pass a frame");
    expects(m_device->currentFrame().hasAcquiredImage,
            "CommandList::beginRendering: the window's image of the frame that acquired it");
    const ColorAttachment& color = desc.colors.front();
    const VulkanTexture& window = m_device->textures().get(color.texture);
    expects(color.mipLevel == 0 && color.arrayLayer == 0 && desc.area.width <= window.extent.width &&
                desc.area.height <= window.extent.height,
            "CommandList::beginRendering: a pass within the window's image");
    recordBarriers(); // the commands before the pass, in the frame's own command buffer
    m_windowPass = WindowPass{.commands = m_windowCommands,
                              .color = renderingAttachmentOf(color, VK_NULL_HANDLE),
                              .area = {.width = desc.area.width, .height = desc.area.height}};
    const VkFormat colorFormat = vulkanFormatOf(window.format);
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    if (desc.depth) {
        const VulkanTexture& depth = m_device->textures().get(desc.depth->texture);
        depthFormat = vulkanFormatOf(depth.format);
        m_windowPass.depth = renderingAttachmentOf(*desc.depth, depth.view);
        m_windowPass.hasDepth = true;
    }
    // What the pass the submission thread begins will have: its attachments' formats.
    const VkCommandBufferInheritanceRenderingInfo rendering{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
        .depthAttachmentFormat = depthFormat,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkCommandBufferInheritanceInfo inheritance{.sType =
                                                         VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
                                                     .pNext = &rendering,
                                                     .renderPass = VK_NULL_HANDLE,
                                                     .subpass = 0,
                                                     .framebuffer = VK_NULL_HANDLE,
                                                     .occlusionQueryEnable = VK_FALSE,
                                                     .queryFlags = 0,
                                                     .pipelineStatistics = 0};
    const VkCommandBufferBeginInfo beginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                             .pNext = nullptr,
                                             .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT |
                                                      VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
                                             .pInheritanceInfo = &inheritance};
    m_bound = {}; // a secondary command buffer inherits nothing bound
    if (const VkResult result = vkBeginCommandBuffer(m_windowCommands, &beginInfo); result != VK_SUCCESS) {
        // Recording has no way to fail: the pass's commands go nowhere, and the frame's submission fails.
        m_failure = vulkanFailure(result, "vkBeginCommandBuffer").error();
        m_windowPassState = WindowPassState::Lost;
        m_commands = VK_NULL_HANDLE;
        return;
    }
    m_windowPassState = WindowPassState::Drawing;
    m_commands = m_windowCommands;
    const Rect whole{.x = 0, .y = 0, .size = desc.area};
    setViewport(whole);
    setScissor(whole);
}

void VulkanCommandList::endRendering() {
    recordBarriers();
    if (m_windowPassState == WindowPassState::Drawing || m_windowPassState == WindowPassState::Lost) {
        // The submission thread ends the pass, on the window's image.
        m_windowPassState = WindowPassState::Done;
        m_commands = VK_NULL_HANDLE;
        return;
    }
    vkCmdEndRendering(commands());
}

auto VulkanCommandList::finishRecording() -> Result<std::optional<WindowPass>, RhiError> {
    recordBarriers();
    expects(m_windowPassState == WindowPassState::Before || m_windowPassState == WindowPassState::Done,
            "rhi::Device::submitFrame: the window's pass has ended");
    if (m_failure) {
        return std::unexpected{std::move(*m_failure)};
    }
    if (m_windowPassState == WindowPassState::Before) {
        return std::nullopt;
    }
    if (const VkResult result = vkEndCommandBuffer(m_windowCommands); result != VK_SUCCESS) {
        return vulkanFailure(result, "vkEndCommandBuffer");
    }
    return m_windowPass;
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
    if (std::ranges::contains(m_bufferBarriers, barrier.buffer, &VkBufferMemoryBarrier2::buffer)) {
        recordBarriers();
    }
    m_bufferBarriers.push_back(barrier);
}

void VulkanCommandList::copyBuffer(BufferHandle source, std::uint64_t sourceOffset, BufferHandle destination,
                                   std::uint64_t destinationOffset, std::uint64_t size) {
    recordBarriers();
    const VkBufferCopy region{.srcOffset = sourceOffset, .dstOffset = destinationOffset, .size = size};
    vkCmdCopyBuffer(commands(), m_device->buffers().get(source).buffer,
                    m_device->buffers().get(destination).buffer, 1, &region);
}

auto VulkanCommandList::copyRegionOf(const TextureCopy& copy) -> VkBufferImageCopy {
    const VulkanTexture& texture = m_device->textures().get(copy.texture);
    expects(!texture.isWindowImage, "CommandList: the window's image is drawn to, never copied");
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
    recordBarriers();
    const VkBufferImageCopy region = copyRegionOf(copy);
    vkCmdCopyBufferToImage(commands(), m_device->buffers().get(copy.buffer).buffer,
                           m_device->textures().get(copy.texture).image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);
}

void VulkanCommandList::copyTextureToBuffer(const TextureCopy& copy) {
    recordBarriers();
    const VkBufferImageCopy region = copyRegionOf(copy);
    vkCmdCopyImageToBuffer(commands(), m_device->textures().get(copy.texture).image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_device->buffers().get(copy.buffer).buffer,
                           1, &region);
}

void VulkanCommandList::setPipeline(RenderPipelineHandle pipeline) {
    const VkCommandBuffer recording = commands();
    if (recording == VK_NULL_HANDLE) {
        return; // a lost window's pass
    }
    const VulkanPipeline& bound = m_device->pipelines().get(pipeline);
    if (bound.pipeline == m_bound.pipeline) {
        return;
    }
    vkCmdBindPipeline(recording, VK_PIPELINE_BIND_POINT_GRAPHICS, bound.pipeline);
    m_bound.pipeline = bound.pipeline;
    m_bound.layout = bound.layout;
}

void VulkanCommandList::setBindGroup(std::uint32_t index, BindGroupHandle group) {
    const VkCommandBuffer recording = commands();
    if (recording == VK_NULL_HANDLE) {
        return; // a lost window's pass
    }
    expects(m_bound.layout != VK_NULL_HANDLE, "CommandList::setBindGroup: after setPipeline");
    const VkDescriptorSet set = m_device->bindGroups().get(group).set;
    if (m_bound.groupsLayout != m_bound.layout) {
        m_bound.groups = {};
        m_bound.groupsLayout = m_bound.layout;
    }
    if (index < m_bound.groups.size()) {
        if (m_bound.groups.at(index) == set) {
            return;
        }
        m_bound.groups.at(index) = set;
    }
    vkCmdBindDescriptorSets(recording, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bound.layout, index, 1, &set, 0,
                            nullptr);
}

void VulkanCommandList::setVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset) {
    const VkCommandBuffer recording = commands();
    if (recording == VK_NULL_HANDLE) {
        return; // a lost window's pass
    }
    const BoundBuffer bound{.buffer = m_device->buffers().get(buffer).buffer, .offset = offset};
    if (slot < m_bound.vertices.size()) {
        if (m_bound.vertices.at(slot) == bound) {
            return;
        }
        m_bound.vertices.at(slot) = bound;
    }
    vkCmdBindVertexBuffers(recording, slot, 1, &bound.buffer, &bound.offset);
}

void VulkanCommandList::setIndexBuffer(BufferHandle buffer, std::uint64_t offset, IndexFormat format) {
    const VkCommandBuffer recording = commands();
    if (recording == VK_NULL_HANDLE) {
        return; // a lost window's pass
    }
    const BoundBuffer bound{.buffer = m_device->buffers().get(buffer).buffer, .offset = offset};
    const VkIndexType type = format == IndexFormat::Uint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    if (m_bound.indices == bound && m_bound.indexType == type) {
        return;
    }
    vkCmdBindIndexBuffer(recording, bound.buffer, bound.offset, type);
    m_bound.indices = bound;
    m_bound.indexType = type;
}

void VulkanCommandList::setViewport(const Rect& area) {
    const VkCommandBuffer recording = commands();
    if (recording == VK_NULL_HANDLE) {
        return; // a lost window's pass
    }
    if (m_bound.viewport == area) {
        return;
    }
    m_bound.viewport = area;
    // A negative height turns Vulkan's clip space, y down, into the RHI's, y up (Vulkan 1.1).
    const VkViewport viewport{.x = static_cast<float>(area.x),
                              .y = static_cast<float>(area.y) + static_cast<float>(area.size.height),
                              .width = static_cast<float>(area.size.width),
                              .height = -static_cast<float>(area.size.height),
                              .minDepth = 0.0F,
                              .maxDepth = 1.0F};
    vkCmdSetViewport(recording, 0, 1, &viewport);
}

void VulkanCommandList::setScissor(const Rect& area) {
    const VkCommandBuffer recording = commands();
    if (recording == VK_NULL_HANDLE) {
        return; // a lost window's pass
    }
    if (m_bound.scissor == area) {
        return;
    }
    m_bound.scissor = area;
    const VkRect2D scissor{.offset = {.x = area.x, .y = area.y},
                           .extent = {.width = area.size.width, .height = area.size.height}};
    vkCmdSetScissor(recording, 0, 1, &scissor);
}

void VulkanCommandList::draw(std::uint32_t vertexCount, std::uint32_t instanceCount,
                             std::uint32_t firstVertex, std::uint32_t firstInstance) {
    recordBarriers();
    const VkCommandBuffer recording = commands();
    if (recording == VK_NULL_HANDLE) {
        return; // a lost window's pass
    }
    vkCmdDraw(recording, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandList::drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount,
                                    std::uint32_t firstIndex, std::int32_t vertexOffset,
                                    std::uint32_t firstInstance) {
    recordBarriers();
    const VkCommandBuffer recording = commands();
    if (recording == VK_NULL_HANDLE) {
        return; // a lost window's pass
    }
    vkCmdDrawIndexed(recording, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

// --- VulkanDevice --------------------------------------------------------------------------------

auto VulkanDevice::create(const DeviceDesc& desc) -> Result<std::unique_ptr<VulkanDevice>, RhiError> {
    expects(desc.framesInFlight >= kMinFramesInFlight && desc.framesInFlight <= kMaxFramesInFlight,
            "vulkan::createDevice: 2 or 3 frames in flight");
    auto device = std::make_unique<VulkanDevice>();

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
    if (auto cache = device->createPipelineCache(desc.pipelineCache); !cache) {
        return std::unexpected{std::move(cache.error())};
    }
    if (auto frames = device->createFrames(desc.framesInFlight); !frames) {
        return std::unexpected{std::move(frames.error())};
    }
    device->m_submission =
        platform::Thread{"rhiSubmission", [submitting = device.get()](std::stop_token stop) {
                             submitting->runSubmissions(stop);
                         }};
    log::info(kLog, "Vulkan device: {} ({}), {}; {} frames in flight; validation {}", device->m_adapter.name,
              enumName(device->m_adapter.kind), device->m_adapter.driver, desc.framesInFlight,
              device->m_messenger != VK_NULL_HANDLE ? "on" : "off");
    return device;
}

auto VulkanDevice::fitsThisDevice(std::span<const std::byte> learned) const noexcept -> bool {
    VkPipelineCacheHeaderVersionOne header{};
    if (learned.size() < sizeof(header)) {
        return false;
    }
    std::memcpy(&header, learned.data(), sizeof(header));
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    return header.headerSize >= sizeof(header) && header.headerSize <= learned.size() &&
           header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
           header.vendorID == properties.vendorID && header.deviceID == properties.deviceID &&
           std::ranges::equal(header.pipelineCacheUUID, properties.pipelineCacheUUID);
}

auto VulkanDevice::createPipelineCache(std::span<const std::byte> learned) -> Result<void, RhiError> {
    const bool isKept = fitsThisDevice(learned);
    if (!learned.empty() && !isKept) {
        log::info(kLog, "the pipeline cache is another GPU's or driver's: the pipelines are built anew");
    }
    const VkPipelineCacheCreateInfo info{.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
                                         .pNext = nullptr,
                                         .flags = 0,
                                         .initialDataSize = isKept ? learned.size() : 0,
                                         .pInitialData = isKept ? learned.data() : nullptr};
    if (const VkResult result = vkCreatePipelineCache(m_device, &info, nullptr, &m_pipelineCache);
        result != VK_SUCCESS) {
        return vulkanFailure(result, "vkCreatePipelineCache");
    }
    return {};
}

auto VulkanDevice::pipelineCache() const -> Result<std::vector<std::byte>, RhiError> {
    std::size_t size = 0;
    if (const VkResult result = vkGetPipelineCacheData(m_device, m_pipelineCache, &size, nullptr);
        result != VK_SUCCESS) {
        return vulkanFailure(result, "vkGetPipelineCacheData");
    }
    std::vector<std::byte> learned(size);
    if (const VkResult result = vkGetPipelineCacheData(m_device, m_pipelineCache, &size, learned.data());
        result != VK_SUCCESS) {
        return vulkanFailure(result, "vkGetPipelineCacheData");
    }
    learned.resize(size);
    return learned;
}

auto VulkanDevice::createInstance(const DeviceDesc& desc) -> Result<void, RhiError> {
    if (auto started = startLoader(); !started) {
        return started;
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
        auto surface = surfaceExtensionsFor(*desc.window, *available);
        if (!surface) {
            return std::unexpected{std::move(surface.error())};
        }
        extensions = std::move(*surface);
    }
    Validation validation;
    if (desc.isValidationEnabled) {
        auto found = validationOf(*available);
        if (!found) {
            return std::unexpected{std::move(found.error())};
        }
        validation = std::move(*found);
    }
    extensions.insert(extensions.end(), validation.extensions.begin(), validation.extensions.end());

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
    VkDebugUtilsMessengerCreateInfoEXT messenger = messengerInfoFor(*this);
    if (validation.hasFeatures) {
        messenger.pNext = &kSynchronizationValidation;
    }
    const VkInstanceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = validation.layers.empty() ? nullptr : &messenger, // also reports what instance creation says
        .flags = 0,
        .pApplicationInfo = &application,
        .enabledLayerCount = narrow<std::uint32_t>(validation.layers.size()),
        .ppEnabledLayerNames = validation.layers.data(),
        .enabledExtensionCount = narrow<std::uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    if (const VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance); result != VK_SUCCESS) {
        return vulkanFailure(result, "vkCreateInstance");
    }
    // Every function, the device's too, through the loader's dispatch: right for any device.
    volkLoadInstance(m_instance);
    if (!validation.layers.empty()) {
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
    std::optional<std::size_t> bestRank;
    for (VkPhysicalDevice candidate : *adapters) {
        const AdapterProbe probe{candidate};
        if (const std::optional<std::string> missing = probe.missingRequirement()) {
            refusals.push_back(std::format("{}: {}", probe.name(), *missing));
            continue;
        }
        if (m_surface != VK_NULL_HANDLE) {
            auto hasSwapchain = hasSwapchainExtension(candidate);
            if (!hasSwapchain) {
                return std::unexpected{std::move(hasSwapchain.error())};
            }
            if (!*hasSwapchain) {
                refusals.push_back(std::format("{}: no swapchain", probe.name()));
                continue;
            }
        }
        auto family = graphicsFamilyOf(candidate);
        if (!family) {
            return std::unexpected{std::move(family.error())};
        }
        if (!*family) {
            refusals.push_back(
                std::format("{}: no graphics queue{}", probe.name(),
                            m_surface != VK_NULL_HANDLE ? " that presents to the window" : ""));
            continue;
        }
        if (!bestRank || rankOf(probe.kind()) > *bestRank) {
            bestRank = rankOf(probe.kind());
            m_physicalDevice = candidate;
            m_queueFamily = **family;
            m_hasAnisotropy = probe.hasAnisotropy();
            m_maxAnisotropy = probe.maxAnisotropy();
            m_adapter = probe.info();
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

auto VulkanDevice::graphicsFamilyOf(VkPhysicalDevice candidate) const
    -> Result<std::optional<std::uint32_t>, RhiError> {
    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
    for (std::uint32_t index = 0; index < familyCount; ++index) {
        if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
            continue;
        }
        VkBool32 canPresent = VK_TRUE;
        if (m_surface != VK_NULL_HANDLE) {
            if (const VkResult result =
                    vkGetPhysicalDeviceSurfaceSupportKHR(candidate, index, m_surface, &canPresent);
                result != VK_SUCCESS) {
                return vulkanFailure(result, "vkGetPhysicalDeviceSurfaceSupportKHR");
            }
        }
        if (canPresent == VK_TRUE) {
            return index;
        }
    }
    return std::nullopt;
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
    features.features.imageCubeArray = VK_TRUE;
    features.features.dualSrcBlend = VK_TRUE;
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

/// A command pool of `device` for queue family `family`, whose command buffers are recorded once each
/// time it is reset. Errors: those of vkCreateCommandPool.
[[nodiscard]] auto createCommandPool(VkDevice device, std::uint32_t family)
    -> Result<VkCommandPool, RhiError> {
    const VkCommandPoolCreateInfo poolInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                           .pNext = nullptr,
                                           .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                                           .queueFamilyIndex = family};
    VkCommandPool pool = VK_NULL_HANDLE;
    if (const VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &pool);
        result != VK_SUCCESS) {
        return vulkanFailure(result, "vkCreateCommandPool");
    }
    return pool;
}

/// A command buffer of `level` from `pool`. Errors: those of vkAllocateCommandBuffers.
[[nodiscard]] auto allocateCommandBuffer(VkDevice device, VkCommandPool pool, VkCommandBufferLevel level)
    -> Result<VkCommandBuffer, RhiError> {
    const VkCommandBufferAllocateInfo allocateInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                                   .pNext = nullptr,
                                                   .commandPool = pool,
                                                   .level = level,
                                                   .commandBufferCount = 1};
    VkCommandBuffer commands = VK_NULL_HANDLE;
    if (const VkResult result = vkAllocateCommandBuffers(device, &allocateInfo, &commands);
        result != VK_SUCCESS) {
        return vulkanFailure(result, "vkAllocateCommandBuffers");
    }
    return commands;
}

auto VulkanDevice::createFrames(std::uint32_t count) -> Result<void, RhiError> {
    m_frames.resize(count);
    m_presentCommands.resize(count);
    for (std::size_t slot = 0; slot < count; ++slot) {
        Frame& frame = m_frames[slot];
        PresentCommands& present = m_presentCommands[slot];
        auto framePool = createCommandPool(m_device, m_queueFamily);
        if (!framePool) {
            return std::unexpected{std::move(framePool.error())};
        }
        frame.pool = *framePool;
        auto presentPool = createCommandPool(m_device, m_queueFamily);
        if (!presentPool) {
            return std::unexpected{std::move(presentPool.error())};
        }
        present.pool = *presentPool;
        auto commands = allocateCommandBuffer(m_device, frame.pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        auto windowCommands = allocateCommandBuffer(m_device, frame.pool, VK_COMMAND_BUFFER_LEVEL_SECONDARY);
        auto presentCommands = allocateCommandBuffer(m_device, present.pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        for (const auto* allocated : {&commands, &windowCommands, &presentCommands}) {
            if (!*allocated) {
                return std::unexpected{allocated->error()};
            }
        }
        frame.commands = *commands;
        frame.windowCommands = *windowCommands;
        present.commands = *presentCommands;
    }
    m_frameIndex = m_frames.size() - 1; // the first frame is frame 0
    return {};
}

VulkanDevice::~VulkanDevice() {
    expects(m_openSwapchains == 0, "rhi::Device: a swapchain outlives its device");
    drain();
    m_submission.requestStop();
    m_wakeups.fetch_add(1, std::memory_order_release);
    m_wakeups.notify_one();
    m_submission = {}; // ends it
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
            vkDestroyCommandPool(m_device, frame.pool, nullptr);
        }
        for (const PresentCommands& present : m_presentCommands) {
            vkDestroyCommandPool(m_device, present.pool, nullptr);
        }
        vkDestroySemaphore(m_device, m_timeline, nullptr);
        vkDestroyPipelineCache(m_device, m_pipelineCache, nullptr);
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
    // No frame waits behind another to be submitted: a frame of latency (docs/adr/0029).
    waitUntilTaken();
    if (auto failed = submissionFailure(); !failed) {
        return std::unexpected{std::move(failed.error())}; // its frames will not finish: no waiting for them
    }
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
    m_commandList.startRecording(frame.commands, frame.windowCommands);
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
    frame.hasAcquiredImage = false;
    auto windowPass = m_commandList.finishRecording();
    if (!windowPass) {
        return std::unexpected{std::move(windowPass.error())};
    }
    if (const VkResult result = vkEndCommandBuffer(frame.commands); result != VK_SUCCESS) {
        return vulkanFailure(result, "vkEndCommandBuffer");
    }
    frame.submitted = ++m_timelineValue;
    const bool isShown = windowPass->has_value();
    const auto handed = m_handOff.push(HandedFrame{.commands = frame.commands,
                                                   .timelineValue = frame.submitted,
                                                   .slot = m_frameIndex,
                                                   .presentTo = isShown ? presentTo : nullptr,
                                                   .windowPass = windowPass->value_or(WindowPass{})});
    expects(handed.has_value(), "rhi::Device::submitFrame: a frame is begun once the one before is taken");
    ++m_handedFrames;
    m_wakeups.fetch_add(1, std::memory_order_release);
    m_wakeups.notify_one();
    return submissionFailure(); // an earlier frame's
}

void VulkanDevice::waitIdle() noexcept {
    drain();
    vkDeviceWaitIdle(m_device);
}

void VulkanDevice::waitUntilTaken() noexcept {
    for (std::uint64_t taken = m_takenFrames.load(std::memory_order_acquire); taken != m_handedFrames;
         taken = m_takenFrames.load(std::memory_order_acquire)) {
        m_takenFrames.wait(taken, std::memory_order_acquire);
    }
}

void VulkanDevice::drain() noexcept {
    for (std::uint64_t finished = m_finishedFrames.load(std::memory_order_acquire);
         finished != m_handedFrames; finished = m_finishedFrames.load(std::memory_order_acquire)) {
        m_finishedFrames.wait(finished, std::memory_order_acquire);
    }
}

auto VulkanDevice::submissionFailure() const -> Result<void, RhiError> {
    if (m_hasFailed.load(std::memory_order_acquire)) {
        return std::unexpected{*m_submissionFailure};
    }
    return {};
}

auto VulkanDevice::consume(VkSemaphore semaphore) -> Result<void, RhiError> {
    const VkSemaphoreSubmitInfo wait{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                     .pNext = nullptr,
                                     .semaphore = semaphore,
                                     .value = 0,
                                     .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                     .deviceIndex = 0};
    const VkSubmitInfo2 submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                                   .pNext = nullptr,
                                   .flags = 0,
                                   .waitSemaphoreInfoCount = 1,
                                   .pWaitSemaphoreInfos = &wait,
                                   .commandBufferInfoCount = 0,
                                   .pCommandBufferInfos = nullptr,
                                   .signalSemaphoreInfoCount = 0,
                                   .pSignalSemaphoreInfos = nullptr};
    if (const VkResult result = vkQueueSubmit2(m_queue, 1, &submitInfo, VK_NULL_HANDLE);
        result != VK_SUCCESS) {
        return vulkanFailure(result, "vkQueueSubmit2");
    }
    return {};
}

// --- VulkanDevice: the submission thread ---------------------------------------------------------

void VulkanDevice::runSubmissions(const std::stop_token& stop) {
    for (;;) {
        // Read before looking for a frame, so that a frame handed after the look changes it.
        const std::uint64_t wakeups = m_wakeups.load(std::memory_order_acquire);
        if (std::optional<HandedFrame> frame = m_handOff.pop()) {
            m_takenFrames.fetch_add(1, std::memory_order_release);
            m_takenFrames.notify_all();
            // After a failure the frames are dropped: the render thread learns of it and stops.
            if (!m_hasFailed.load(std::memory_order_relaxed)) { // this thread's own to write
                if (auto submitted = submitHanded(*frame); !submitted) {
                    m_submissionFailure = std::move(submitted.error());
                    m_hasFailed.store(true, std::memory_order_release);
                }
            }
            m_finishedFrames.fetch_add(1, std::memory_order_release);
            m_finishedFrames.notify_all();
            continue;
        }
        if (stop.stop_requested()) {
            return;
        }
        m_wakeups.wait(wakeups, std::memory_order_acquire);
    }
}

/// Records in `commands` the window's pass `pass` drawn on `image`, seen through `view`: the image made
/// ready to draw once acquired, the pass's own commands run inside a render pass on it, then the image
/// made ready to be shown. Errors: those of vkBeginCommandBuffer and vkEndCommandBuffer.
[[nodiscard]] auto recordWindowPass(VkCommandBuffer commands, const WindowPass& pass, VkImage image,
                                    VkImageView view) -> Result<void, RhiError> {
    const VkCommandBufferBeginInfo beginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                             .pNext = nullptr,
                                             .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                                             .pInheritanceInfo = nullptr};
    if (const VkResult result = vkBeginCommandBuffer(commands, &beginInfo); result != VK_SUCCESS) {
        return vulkanFailure(result, "vkBeginCommandBuffer");
    }
    const VkImageSubresourceRange whole{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                        .baseMipLevel = 0,
                                        .levelCount = 1,
                                        .baseArrayLayer = 0,
                                        .layerCount = 1};
    const auto barrierOf = [image, &whole](VkPipelineStageFlags2 sourceStages, VkAccessFlags2 sourceAccess,
                                           VkPipelineStageFlags2 targetStages, VkAccessFlags2 targetAccess,
                                           VkImageLayout from, VkImageLayout to) {
        return VkImageMemoryBarrier2{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                     .pNext = nullptr,
                                     .srcStageMask = sourceStages,
                                     .srcAccessMask = sourceAccess,
                                     .dstStageMask = targetStages,
                                     .dstAccessMask = targetAccess,
                                     .oldLayout = from,
                                     .newLayout = to,
                                     .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                     .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                     .image = image,
                                     .subresourceRange = whole};
    };
    const auto recordBarrier = [commands](const VkImageMemoryBarrier2& barrier) {
        const VkDependencyInfo dependency{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                          .pNext = nullptr,
                                          .dependencyFlags = 0,
                                          .memoryBarrierCount = 0,
                                          .pMemoryBarriers = nullptr,
                                          .bufferMemoryBarrierCount = 0,
                                          .pBufferMemoryBarriers = nullptr,
                                          .imageMemoryBarrierCount = 1,
                                          .pImageMemoryBarriers = &barrier};
        vkCmdPipelineBarrier2(commands, &dependency);
    };
    // After the acquire's semaphore, which the batch waits on at the color attachments' stage.
    recordBarrier(barrierOf(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
    VkRenderingAttachmentInfo color = pass.color;
    color.imageView = view;
    const VkRenderingInfo rendering{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT,
        .renderArea = {.offset = {.x = 0, .y = 0}, .extent = pass.area},
        .layerCount = 1,
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
        .pDepthAttachment = pass.hasDepth ? &pass.depth : nullptr,
        .pStencilAttachment = nullptr,
    };
    vkCmdBeginRendering(commands, &rendering);
    vkCmdExecuteCommands(commands, 1, &pass.commands);
    vkCmdEndRendering(commands);
    // The presentation waits on the image's semaphore, which covers every stage.
    recordBarrier(barrierOf(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE,
                            VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR));
    if (const VkResult result = vkEndCommandBuffer(commands); result != VK_SUCCESS) {
        return vulkanFailure(result, "vkEndCommandBuffer");
    }
    return {};
}

auto VulkanDevice::submitHanded(const HandedFrame& frame) -> Result<void, RhiError> {
    std::optional<HeldImage> image;
    if (frame.presentTo != nullptr) {
        auto taken = frame.presentTo->takeImage();
        if (!taken) {
            return std::unexpected{std::move(taken.error())};
        }
        image = *taken; // none: out of date or minimized, the frame not shown
    }
    const PresentCommands& present = m_presentCommands[frame.slot];
    if (image) {
        // The frame in flight's last use of them has finished: the render thread saw it before it
        // began this frame.
        if (const VkResult result = vkResetCommandPool(m_device, present.pool, 0); result != VK_SUCCESS) {
            return vulkanFailure(result, "vkResetCommandPool");
        }
        if (auto recorded =
                recordWindowPass(present.commands, frame.windowPass, frame.presentTo->image(image->index),
                                 frame.presentTo->view(image->index));
            !recorded) {
            frame.presentTo->giveBack(*image);
            return recorded;
        }
    }
    // The frame's own commands wait for nothing; the window's pass waits for its image, then signals
    // what the presentation waits for; the last batch signals the frame's end.
    const std::array frameCommands{
        VkCommandBufferSubmitInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                                  .pNext = nullptr,
                                  .commandBuffer = frame.commands,
                                  .deviceMask = 0},
        VkCommandBufferSubmitInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                                  .pNext = nullptr,
                                  .commandBuffer = present.commands,
                                  .deviceMask = 0}};
    const VkSemaphoreSubmitInfo acquired{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                         .pNext = nullptr,
                                         .semaphore = image ? image->acquired : VK_NULL_HANDLE,
                                         .value = 0,
                                         .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                         .deviceIndex = 0};
    const std::array signals{
        VkSemaphoreSubmitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                              .pNext = nullptr,
                              .semaphore = m_timeline,
                              .value = frame.timelineValue,
                              .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                              .deviceIndex = 0},
        VkSemaphoreSubmitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                              .pNext = nullptr,
                              .semaphore =
                                  image ? frame.presentTo->renderFinished(image->index) : VK_NULL_HANDLE,
                              .value = 0,
                              .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                              .deviceIndex = 0},
    };
    const std::array batches{
        VkSubmitInfo2{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                      .pNext = nullptr,
                      .flags = 0,
                      .waitSemaphoreInfoCount = 0,
                      .pWaitSemaphoreInfos = nullptr,
                      .commandBufferInfoCount = 1,
                      .pCommandBufferInfos = &frameCommands[0],
                      .signalSemaphoreInfoCount = image ? 0U : 1U,
                      .pSignalSemaphoreInfos = signals.data()},
        VkSubmitInfo2{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                      .pNext = nullptr,
                      .flags = 0,
                      .waitSemaphoreInfoCount = 1,
                      .pWaitSemaphoreInfos = &acquired,
                      .commandBufferInfoCount = 1,
                      .pCommandBufferInfos = &frameCommands[1],
                      .signalSemaphoreInfoCount = narrow<std::uint32_t>(signals.size()),
                      .pSignalSemaphoreInfos = signals.data()},
    };
    if (const VkResult result = vkQueueSubmit2(m_queue, image ? narrow<std::uint32_t>(batches.size()) : 1U,
                                               batches.data(), VK_NULL_HANDLE);
        result != VK_SUCCESS) {
        if (image) {
            frame.presentTo->giveBack(*image); // nothing waited on its semaphore
        }
        return vulkanFailure(result, "vkQueueSubmit2");
    }
    if (!image) {
        return {};
    }
    if (auto shown = frame.presentTo->present(image->index); !shown) {
        return shown;
    }
    // Waiting here for the display, before the next frame is taken, so that the render thread reads that
    // frame's input once the display is ready for it.
    return frame.presentTo->acquireAhead();
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
                             .descriptorCount = kBindGroupsPerPool * kUniformBuffersPerGroup},
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                             .descriptorCount = kBindGroupsPerPool * kStorageBuffersPerGroup},
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                             .descriptorCount = kBindGroupsPerPool * kSampledImagesPerGroup},
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLER,
                             .descriptorCount = kBindGroupsPerPool * kSamplersPerGroup},
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
                for (const VkImageView attachmentView : held.attachmentViews) {
                    vkDestroyImageView(m_device, attachmentView, nullptr);
                }
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

auto VulkanDevice::makeBuffer(const BufferDesc& desc) -> Result<BufferHandle, RhiError> {
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

auto VulkanDevice::makeTexture(const TextureDesc& desc) -> Result<TextureHandle, RhiError> {
    expects(!desc.label.empty() && desc.extent.width > 0 && desc.extent.height > 0 && desc.mipLevels > 0 &&
                desc.arrayLayers > 0,
            "rhi::Device::createTexture: a label, a size, a mip level and a layer");
    expects(desc.arrayLayers == 1 || !desc.usage.test(TextureUsage::DepthTarget),
            "rhi::Device::createTexture: a depth target has one layer");
    expects(!desc.isCube || (desc.arrayLayers % kCubeFaces == 0 && desc.extent.width == desc.extent.height),
            "rhi::Device::createTexture: a cube's layers six by six, square");
    const VkFormat format = vulkanFormatOf(desc.format);
    const VkImageUsageFlags usage = imageUsageOf(desc.usage);
    const VkImageCreateFlags createFlags = desc.isCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    VkImageFormatProperties supported{};
    VkFormatProperties features{};
    vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &features);
    // A render target is blended into: its format must be.
    const bool isBlendable =
        !desc.usage.test(TextureUsage::RenderTarget) ||
        (features.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0;
    if (format == VK_FORMAT_UNDEFINED || !isBlendable ||
        vkGetPhysicalDeviceImageFormatProperties(m_physicalDevice, format, VK_IMAGE_TYPE_2D,
                                                 VK_IMAGE_TILING_OPTIMAL, usage, createFlags,
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
        .flags = createFlags,
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
    const VkImageViewType viewType =
        desc.isCube
            ? (desc.arrayLayers == kCubeFaces ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)
        : desc.arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                               : VK_IMAGE_VIEW_TYPE_2D;
    const auto viewOf = [&](VkImageViewType type, std::uint32_t mip, std::uint32_t mips, std::uint32_t layer,
                            std::uint32_t layers, VkImageView& view) {
        const VkImageViewCreateInfo viewInfo{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                             .pNext = nullptr,
                                             .flags = 0,
                                             .image = texture.image,
                                             .viewType = type,
                                             .format = format,
                                             .components = {},
                                             .subresourceRange = {.aspectMask = texture.aspect,
                                                                  .baseMipLevel = mip,
                                                                  .levelCount = mips,
                                                                  .baseArrayLayer = layer,
                                                                  .layerCount = layers}};
        return vkCreateImageView(m_device, &viewInfo, nullptr, &view);
    };
    const auto destroyViews = [&] {
        for (const VkImageView attachmentView : texture.attachmentViews) {
            vkDestroyImageView(m_device, attachmentView, nullptr);
        }
        vkDestroyImageView(m_device, texture.view, nullptr);
        vmaDestroyImage(m_allocator, texture.image, texture.allocation);
    };
    if (const VkResult result = viewOf(viewType, 0, desc.mipLevels, 0, desc.arrayLayers, texture.view);
        result != VK_SUCCESS) {
        destroyViews();
        return vulkanFailure(result, std::format("vkCreateImageView({:?})", desc.label));
    }
    if (desc.usage.test(TextureUsage::RenderTarget) && (desc.mipLevels > 1 || desc.arrayLayers > 1)) {
        texture.attachmentViews.resize(std::size_t{desc.mipLevels} * desc.arrayLayers, VK_NULL_HANDLE);
        for (std::uint32_t mip = 0; mip < desc.mipLevels; ++mip) {
            for (std::uint32_t layer = 0; layer < desc.arrayLayers; ++layer) {
                VkImageView& view = texture.attachmentViews[(std::size_t{mip} * desc.arrayLayers) + layer];
                if (const VkResult result = viewOf(VK_IMAGE_VIEW_TYPE_2D, mip, 1, layer, 1, view);
                    result != VK_SUCCESS) {
                    destroyViews();
                    return vulkanFailure(
                        result, std::format("vkCreateImageView({:?}, a mip level of a layer)", desc.label));
                }
            }
        }
    }
    name(VK_OBJECT_TYPE_IMAGE, objectHandleOf(texture.image), desc.label);
    return m_textures.insert(std::move(texture));
}

auto VulkanDevice::makeSampler(const SamplerDesc& desc) -> Result<SamplerHandle, RhiError> {
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

auto VulkanDevice::makeShader(const ShaderDesc& desc) -> Result<ShaderHandle, RhiError> {
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

auto VulkanDevice::makeBindGroupLayout(const BindGroupLayoutDesc& desc)
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

auto VulkanDevice::makeBindGroup(const BindGroupDesc& desc) -> Result<BindGroupHandle, RhiError> {
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
            expects(!m_textures.get(*texture).isWindowImage,
                    "rhi::Device::createBindGroup: the window's image is drawn to, never sampled");
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

auto VulkanDevice::makeRenderPipeline(const RenderPipelineDesc& desc)
    -> Result<RenderPipelineHandle, RhiError> {
    expects(desc.colorTargets.size() <= kMaxColorAttachments,
            "rhi::Device::createRenderPipeline: at most 8 targets");
    VulkanPipeline pipeline;
    if (auto layout = createPipelineLayout(desc); !layout) {
        return std::unexpected{std::move(layout.error())};
    } else {
        pipeline.layout = *layout;
    }
    const std::string vertexEntry{desc.vertex.entryPoint};
    const std::string fragmentEntry{desc.fragment.entryPoint};
    const std::array stages{
        stageOf(VK_SHADER_STAGE_VERTEX_BIT, m_shaders.get(desc.vertex.shader).module, vertexEntry),
        stageOf(VK_SHADER_STAGE_FRAGMENT_BIT, m_shaders.get(desc.fragment.shader).module, fragmentEntry)};
    const VertexInput vertexInput = vertexInputOf(desc.vertexBuffers);
    const VkPipelineVertexInputStateCreateInfo vertexInputState = vertexInput.state();
    const VkPipelineInputAssemblyStateCreateInfo inputAssembly = inputAssemblyOf(desc.topology);
    const VkPipelineRasterizationStateCreateInfo rasterization = rasterizationOf(desc);
    const VkPipelineDepthStencilStateCreateInfo depthStencil = depthStencilOf(desc.depth);
    const std::vector<VkPipelineColorBlendAttachmentState> blends =
        desc.colorTargets | std::views::transform(colorBlendOf) | std::ranges::to<std::vector>();
    const std::vector<VkFormat> colorFormats =
        desc.colorTargets |
        std::views::transform([](const ColorTarget& target) { return vulkanFormatOf(target.format); }) |
        std::ranges::to<std::vector>();
    const VkPipelineColorBlendStateCreateInfo colorBlend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = narrow<std::uint32_t>(blends.size()),
        .pAttachments = blends.data(),
        .blendConstants = {0.0F, 0.0F, 0.0F, 0.0F}};
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
                                            .pVertexInputState = &vertexInputState,
                                            .pInputAssemblyState = &inputAssembly,
                                            .pTessellationState = nullptr,
                                            .pViewportState = &kDynamicViewportState,
                                            .pRasterizationState = &rasterization,
                                            .pMultisampleState = &kSingleSampleState,
                                            .pDepthStencilState = &depthStencil,
                                            .pColorBlendState = &colorBlend,
                                            .pDynamicState = &kViewportAndScissorState,
                                            .layout = pipeline.layout,
                                            .renderPass = VK_NULL_HANDLE,
                                            .subpass = 0,
                                            .basePipelineHandle = VK_NULL_HANDLE,
                                            .basePipelineIndex = -1};
    if (const VkResult result =
            vkCreateGraphicsPipelines(m_device, m_pipelineCache, 1, &info, nullptr, &pipeline.pipeline);
        result != VK_SUCCESS) {
        vkDestroyPipelineLayout(m_device, pipeline.layout, nullptr);
        return vulkanFailure(result, std::format("vkCreateGraphicsPipelines({:?})", desc.label));
    }
    name(VK_OBJECT_TYPE_PIPELINE, objectHandleOf(pipeline.pipeline), desc.label);
    return m_pipelines.insert(pipeline);
}

auto VulkanDevice::createPipelineLayout(const RenderPipelineDesc& desc)
    -> Result<VkPipelineLayout, RhiError> {
    const std::vector<VkDescriptorSetLayout> setLayouts =
        desc.bindGroupLayouts | std::views::transform([this](BindGroupLayoutHandle layout) {
            return m_bindGroupLayouts.get(layout).layout;
        }) |
        std::ranges::to<std::vector>();
    const VkPipelineLayoutCreateInfo layoutInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                .pNext = nullptr,
                                                .flags = 0,
                                                .setLayoutCount = narrow<std::uint32_t>(setLayouts.size()),
                                                .pSetLayouts = setLayouts.data(),
                                                .pushConstantRangeCount = 0,
                                                .pPushConstantRanges = nullptr};
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (const VkResult result = vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &layout);
        result != VK_SUCCESS) {
        return vulkanFailure(result, std::format("vkCreatePipelineLayout({:?})", desc.label));
    }
    return layout;
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
    const VkSemaphoreCreateInfo semaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};
    for (VkSemaphore& semaphore : m_acquired) {
        if (const VkResult result =
                vkCreateSemaphore(m_device->handle(), &semaphoreInfo, nullptr, &semaphore);
            result != VK_SUCCESS) {
            return vulkanFailure(result, "vkCreateSemaphore");
        }
    }
    m_requestedSize = desc.size;
    m_presentMode = desc.presentMode;
    return rebuild();
}

auto VulkanSwapchain::rebuild() -> Result<void, RhiError> {
    const VkPhysicalDevice physical = m_device->physicalDevice();
    const VkSurfaceKHR surface = m_device->surface();
    const VkDevice device = m_device->handle();
    m_device->drain(); // the submission thread uses the swapchain no more
    releaseHeldImage();
    m_device->waitIdle(); // the old images may still be drawn or shown
    destroyImages();
    m_isOutOfDate.store(false, std::memory_order_relaxed);

    VkSurfaceCapabilitiesKHR capabilities{};
    if (const VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities);
        result != VK_SUCCESS) {
        return vulkanFailure(result, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    }
    m_extent = extentFor(capabilities, m_requestedSize);
    if (m_extent.width == 0 || m_extent.height == 0) {
        // Minimized: no swapchain until the window is restored and resized.
        if (m_swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, std::exchange(m_swapchain, VK_NULL_HANDLE), nullptr);
        }
        return {};
    }
    auto choice = chooseSurfaceUse(physical, surface, m_presentMode);
    if (!choice) {
        return std::unexpected{std::move(choice.error())};
    }
    m_format = formatOf(choice->format.format);

    const VkSwapchainKHR old = m_swapchain;
    const VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = surface,
        .minImageCount = imageCountFor(capabilities),
        .imageFormat = choice->format.format,
        .imageColorSpace = choice->format.colorSpace,
        .imageExtent = {.width = m_extent.width, .height = m_extent.height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0
                              ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                              : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        .presentMode = choice->mode,
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
    if (auto images = createImages(choice->format.format); !images) {
        return images;
    }
    m_window = m_device->textures().insert(
        VulkanTexture{.format = m_format, .extent = m_extent, .isWindowImage = true});
    log::debug(kLog, "swapchain: {} images of {}x{}, {}", m_images.size(), m_extent.width, m_extent.height,
               enumName(m_format));
    return {};
}

auto VulkanSwapchain::createImages(VkFormat format) -> Result<void, RhiError> {
    const VkDevice device = m_device->handle();
    auto images = enumerate<VkImage>([device, this](std::uint32_t* count, VkImage* values) {
        return vkGetSwapchainImagesKHR(device, m_swapchain, count, values);
    });
    if (!images) {
        return std::unexpected{std::move(images.error())};
    }
    for (VkImage image : *images) {
        m_images.push_back(image);
        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
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
        m_views.push_back(view);
        const VkSemaphoreCreateInfo semaphoreInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};
        VkSemaphore semaphore = VK_NULL_HANDLE;
        if (const VkResult result = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore);
            result != VK_SUCCESS) {
            return vulkanFailure(result, "vkCreateSemaphore");
        }
        m_renderFinished.push_back(semaphore);
    }
    return {};
}

void VulkanSwapchain::destroyImages() noexcept {
    const VkDevice device = m_device->handle();
    std::ignore = m_device->textures().remove(std::exchange(m_window, TextureHandle{}));
    for (VkImageView view : m_views) {
        vkDestroyImageView(device, view, nullptr); // the image is the swapchain's
    }
    for (VkSemaphore semaphore : m_renderFinished) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    m_images.clear();
    m_views.clear();
    m_renderFinished.clear();
}

void VulkanSwapchain::releaseHeldImage() noexcept {
    if (const std::optional<HeldImage> held = std::exchange(m_held, std::nullopt)) {
        if (auto consumed = m_device->consume(held->acquired); !consumed) {
            log::warning(kLog, "an image acquired ahead and never shown: {}", describe(consumed.error()));
        }
    }
}

VulkanSwapchain::~VulkanSwapchain() {
    m_device->drain();
    releaseHeldImage();
    m_device->waitIdle();
    destroyImages();
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device->handle(), m_swapchain, nullptr);
    }
    for (VkSemaphore semaphore : m_acquired) {
        vkDestroySemaphore(m_device->handle(), semaphore, nullptr);
    }
    m_device->swapchainClosed();
}

auto VulkanSwapchain::acquire() -> Result<std::optional<SwapchainImage>, RhiError> {
    expects(m_device->isRecording(), "rhi::Swapchain::acquire: between Device::beginFrame and submitFrame");
    Frame& frame = m_device->currentFrame();
    expects(!frame.hasAcquiredImage, "rhi::Swapchain::acquire: one image per frame");
    if (auto failed = m_device->submissionFailure(); !failed) {
        return std::unexpected{std::move(failed.error())};
    }
    if (m_isOutOfDate.load(std::memory_order_acquire)) {
        if (auto rebuilt = rebuild(); !rebuilt) {
            return std::unexpected{std::move(rebuilt.error())};
        }
    }
    if (m_swapchain == VK_NULL_HANDLE) {
        return std::nullopt; // minimized
    }
    frame.hasAcquiredImage = true;
    return SwapchainImage{.texture = m_window, .size = m_extent, .format = m_format};
}

auto VulkanSwapchain::resize(Extent2d size) -> Result<void, RhiError> {
    expects(!m_device->isRecording() || !m_device->currentFrame().hasAcquiredImage,
            "rhi::Swapchain::resize: not while a frame holds its image");
    m_requestedSize = size;
    return rebuild();
}

auto VulkanSwapchain::takeImage() -> Result<std::optional<HeldImage>, RhiError> {
    if (m_held) {
        return std::exchange(m_held, std::nullopt);
    }
    return acquireNow();
}

void VulkanSwapchain::giveBack(HeldImage image) noexcept {
    m_held = image;
}

auto VulkanSwapchain::acquireAhead() -> Result<void, RhiError> {
    if (m_held) {
        return {};
    }
    auto acquired = acquireNow();
    if (!acquired) {
        return std::unexpected{std::move(acquired.error())};
    }
    m_held = *acquired;
    return {};
}

auto VulkanSwapchain::acquireNow() -> Result<std::optional<HeldImage>, RhiError> {
    if (m_swapchain == VK_NULL_HANDLE || m_isOutOfDate.load(std::memory_order_relaxed)) {
        return std::nullopt; // until the render thread rebuilds it
    }
    const VkSemaphore semaphore = m_acquired.at(m_nextAcquired);
    std::uint32_t index = 0;
    const VkResult result =
        vkAcquireNextImageKHR(m_device->handle(), m_swapchain, kNoTimeout, semaphore, VK_NULL_HANDLE, &index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        m_isOutOfDate.store(true, std::memory_order_release);
        return std::nullopt;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        return vulkanFailure(result, "vkAcquireNextImageKHR");
    }
    if (result == VK_SUBOPTIMAL_KHR) {
        m_isOutOfDate.store(true, std::memory_order_release); // still shows it; rebuilt after
    }
    m_nextAcquired = (m_nextAcquired + 1) % m_acquired.size();
    return HeldImage{.index = index, .acquired = semaphore};
}

auto VulkanSwapchain::present(std::uint32_t index) -> Result<void, RhiError> {
    const VkSemaphore waitFor = m_renderFinished[index];
    const VkPresentInfoKHR presentInfo{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                       .pNext = nullptr,
                                       .waitSemaphoreCount = 1,
                                       .pWaitSemaphores = &waitFor,
                                       .swapchainCount = 1,
                                       .pSwapchains = &m_swapchain,
                                       .pImageIndices = &index,
                                       .pResults = nullptr};
    const VkResult result = vkQueuePresentKHR(m_device->queue(), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        m_isOutOfDate.store(true, std::memory_order_release);
        return {};
    }
    if (result != VK_SUCCESS) {
        return vulkanFailure(result, "vkQueuePresentKHR");
    }
    return {};
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
