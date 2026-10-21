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

/// A texture the device knows: its image and the view passes draw to.
struct VulkanTexture {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    Format format = Format::Undefined;
    Extent2d extent;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
};

class VulkanCommandList final : public CommandList {
public:
    explicit VulkanCommandList(VulkanDevice& device) noexcept : m_device{&device} {}

    void startRecording(VkCommandBuffer commands) noexcept { m_commands = commands; }

    void transition(TextureHandle texture, TextureState before, TextureState after) override;
    void beginRendering(const RenderingDesc& desc) override;
    void endRendering() override;

private:
    VulkanDevice* m_device;
    VkCommandBuffer m_commands = VK_NULL_HANDLE;
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
    [[nodiscard]] auto submit(VulkanSwapchain* presentTo) -> Result<void, RhiError>;

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
    HandlePool<TextureTag, VulkanTexture> m_textures;
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
        .pDepthAttachment = nullptr,
        .pStencilAttachment = nullptr,
    };
    vkCmdBeginRendering(m_commands, &rendering);
}

void VulkanCommandList::endRendering() {
    vkCmdEndRendering(m_commands);
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

        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
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
            features12.timelineSemaphore == VK_FALSE) {
            refusals.push_back(
                std::format("{}: no dynamic rendering, synchronization2 or timeline semaphores", name));
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
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.timelineSemaphore = VK_TRUE;
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.pNext = &features12;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    const std::array swapchainExtension{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const bool hasSurface = m_surface != VK_NULL_HANDLE;
    const VkDeviceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features13,
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
