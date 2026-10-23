// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.render;

import std;
import nx.core;
import nx.math;
import nx.rhi;

namespace nx::render {

namespace {

// The bind group of the prefilter passes and its bindings, as shaders/probe.slang declares them.
constexpr std::uint32_t kPassGroup = 0;
constexpr std::uint32_t kUniformBinding = 0;
constexpr std::uint32_t kCaptureBinding = 1;
constexpr std::uint32_t kSamplerBinding = 2;

/// The vertices of the triangle that covers a target.
constexpr std::uint32_t kCoveringVertices = 3;
/// Each pass's uniform is this far from the one before: any device's least offset alignment.
constexpr std::uint64_t kUniformStride = 256;
/// The directions a rough level averages for each of its texels.
constexpr float kSamplesPerTexel = 64.0F;
/// A side seen square, 90 degrees each way: the horizontal angle of a 4:3 view whose vertical one is
/// 90 degrees (Camera::fieldOfView), 2 atan(4 / 3).
constexpr Degrees kSideFieldOfView{106.26020470831196F};

/// What a prefilter pass reads (shaders/probe.slang's Pass).
struct PassUniform {
    float side = 0.0F;
    float roughness = 0.0F;
    float sampleCount = 0.0F;
    float padding = 0.0F;
};

[[nodiscard]] auto gpuFailed(std::string_view what, Failure<rhi::RhiError> cause)
    -> std::unexpected<Failure<ProbeError>> {
    return fail(ProbeError::GpuFailed, std::string{what}, std::move(cause));
}

} // namespace

auto probeSideAngles(std::uint32_t side) noexcept -> EulerAngles {
    constexpr float kQuarter = 90.0F;
    constexpr float kHalf = 180.0F;
    switch (side) {
    case 1:
        return {.yaw = Degrees{kHalf}};
    case 2:
        return {.yaw = Degrees{kQuarter}};
    case 3:
        return {.yaw = Degrees{-kQuarter}};
    case 4:
        return {.pitch = Degrees{-kQuarter}}; // up: the pitch looks down as it grows
    case rhi::kCubeFaces - 1:
        return {.pitch = Degrees{kQuarter}};
    default:
        return {};
    }
}

auto ReflectionProbes::create(rhi::Device& device, rhi::CommandList& commands,
                              const ReflectionProbesDesc& desc) -> Result<ReflectionProbes, ProbeError> {
    ReflectionProbes probes{device};
    probes.m_positions.assign(desc.positions.begin(),
                              desc.positions.begin() +
                                  narrow<std::ptrdiff_t>(std::min(desc.positions.size(), kMostProbes)));
    probes.m_mipLevels = std::bit_width(kProbeSideSize);
    const rhi::Extent2d side{.width = kProbeSideSize, .height = kProbeSideSize};
    const auto probeCount = narrow<std::uint32_t>(std::max<std::size_t>(probes.m_positions.size(), 1));
    auto capture =
        device.createTexture({.label = "probes.capture",
                              .format = desc.captureFormat,
                              .extent = side,
                              .arrayLayers = rhi::kCubeFaces,
                              .usage = {rhi::TextureUsage::Sampled, rhi::TextureUsage::RenderTarget}});
    auto depth = device.createTexture({.label = "probes.captureDepth",
                                       .format = rhi::Format::Depth32Float,
                                       .extent = side,
                                       .usage = {rhi::TextureUsage::DepthTarget}});
    auto textures =
        device.createTexture({.label = "probes",
                              .format = desc.captureFormat,
                              .extent = side,
                              .mipLevels = probes.m_mipLevels,
                              .arrayLayers = probeCount * rhi::kCubeFaces,
                              .usage = {rhi::TextureUsage::Sampled, rhi::TextureUsage::RenderTarget}});
    if (!capture || !depth || !textures) {
        return gpuFailed("the probes' textures", std::move(!capture ? capture.error()
                                                           : !depth ? depth.error()
                                                                    : textures.error()));
    }
    probes.m_capture = std::move(*capture);
    probes.m_captureDepth = std::move(*depth);
    probes.m_probes = std::move(*textures);
    commands.transition(probes.m_capture.get(), rhi::TextureState::Undefined, rhi::TextureState::ShaderRead);
    commands.transition(probes.m_probes.get(), rhi::TextureState::Undefined, rhi::TextureState::ShaderRead);

    auto shader = device.createShader({.label = "probes", .code = desc.shaderCode});
    auto sampler = device.createSampler({.label = "probes.linearClamp",
                                         .mipmap = rhi::Filter::Nearest,
                                         .address = rhi::AddressMode::ClampToEdge});
    const Flags<rhi::ShaderStage> fragment{rhi::ShaderStage::Fragment};
    const std::array bindings{
        rhi::BindingLayout{
            .binding = kUniformBinding, .type = rhi::BindingType::UniformBuffer, .stages = fragment},
        rhi::BindingLayout{
            .binding = kCaptureBinding, .type = rhi::BindingType::SampledTexture, .stages = fragment},
        rhi::BindingLayout{
            .binding = kSamplerBinding, .type = rhi::BindingType::Sampler, .stages = fragment}};
    auto layout = device.createBindGroupLayout({.label = "probes.prefilter", .bindings = bindings});
    if (!shader || !sampler || !layout) {
        return gpuFailed("the prefilter's shader", std::move(!shader    ? shader.error()
                                                             : !sampler ? sampler.error()
                                                                        : layout.error()));
    }
    probes.m_shader = std::move(*shader);
    probes.m_sampler = std::move(*sampler);
    probes.m_layout = std::move(*layout);
    const std::array targets{rhi::ColorTarget{.format = desc.captureFormat}};
    const std::array layouts{probes.m_layout.get()};
    auto pipeline = device.createRenderPipeline(
        {.label = "probes.prefilter",
         .vertex = {.shader = probes.m_shader.get(), .entryPoint = "fullscreenMain"},
         .fragment = {.shader = probes.m_shader.get(), .entryPoint = "prefilterMain"},
         .colorTargets = targets,
         .bindGroupLayouts = layouts});
    if (!pipeline) {
        return gpuFailed("the prefilter's pipeline", std::move(pipeline.error()));
    }
    probes.m_prefilter = std::move(*pipeline);

    // A pass for each side at each level: its roughness grows with the level, the sharpest copied.
    const std::uint32_t passCount = rhi::kCubeFaces * probes.m_mipLevels;
    auto uniforms = device.createBuffer({.label = "probes.passes",
                                         .size = kUniformStride * passCount,
                                         .usage = {rhi::BufferUsage::Uniform},
                                         .location = rhi::MemoryLocation::Upload});
    if (!uniforms) {
        return gpuFailed("the prefilter's uniforms", std::move(uniforms.error()));
    }
    probes.m_uniforms = std::move(*uniforms);
    const std::span<std::byte> uniformBytes = device.mappedBytes(probes.m_uniforms.get());
    for (std::uint32_t level = 0; level < probes.m_mipLevels; ++level) {
        for (std::uint32_t sideIndex = 0; sideIndex < rhi::kCubeFaces; ++sideIndex) {
            const std::uint64_t offset = kUniformStride * probes.m_passes.size();
            const float roughness = static_cast<float>(level) / static_cast<float>(probes.m_mipLevels - 1);
            const PassUniform uniform{.side = static_cast<float>(sideIndex),
                                      .roughness = roughness,
                                      .sampleCount = level == 0 ? 0.0F : kSamplesPerTexel};
            std::ranges::copy(std::as_bytes(std::span{&uniform, 1}),
                              uniformBytes.subspan(narrow<std::size_t>(offset)).begin());
            const std::array passBindings{
                rhi::Binding{.binding = kUniformBinding,
                             .resource = rhi::BufferBinding{.buffer = probes.m_uniforms.get(),
                                                            .offset = offset,
                                                            .size = sizeof(PassUniform)}},
                rhi::Binding{.binding = kCaptureBinding, .resource = probes.m_capture.get()},
                rhi::Binding{.binding = kSamplerBinding, .resource = probes.m_sampler.get()}};
            auto group = device.createBindGroup(
                {.label = "probes.prefilter", .layout = probes.m_layout.get(), .bindings = passBindings});
            if (!group) {
                return gpuFailed("a prefilter pass's bind group", std::move(group.error()));
            }
            probes.m_passes.push_back(
                PrefilterPass{.side = sideIndex, .level = level, .group = std::move(*group)});
        }
    }
    return probes;
}

void ReflectionProbes::bakeNext(rhi::CommandList& commands, BspWorld& world, const WorldFrame& scene) {
    if (isBaked()) {
        return;
    }
    const auto probe = narrow<std::uint32_t>(m_baked);
    const Vec3 at = m_positions[m_baked];
    const rhi::Extent2d sideSize{.width = kProbeSideSize, .height = kProbeSideSize};
    // The world toward each side, as the scene shows it now.
    commands.transition(m_capture.get(), rhi::TextureState::ShaderRead, rhi::TextureState::RenderTarget);
    commands.transition(m_captureDepth.get(), rhi::TextureState::Undefined, rhi::TextureState::DepthTarget);
    for (std::uint32_t side = 0; side < rhi::kCubeFaces; ++side) {
        const std::array colors{rhi::ColorAttachment{.texture = m_capture.get(), .arrayLayer = side}};
        commands.beginRendering(
            {.colors = colors,
             .depth = rhi::DepthAttachment{.texture = m_captureDepth.get(), .clear = 0.0F},
             .area = sideSize});
        world.draw(commands, WorldFrame{.camera = Camera{.position = at,
                                                         .angles = probeSideAngles(side),
                                                         .fieldOfView = kSideFieldOfView},
                                        .aspect = 1.0F,
                                        .time = scene.time,
                                        .lightStyles = scene.lightStyles,
                                        .placements = scene.placements});
        commands.endRendering();
    }
    commands.transition(m_capture.get(), rhi::TextureState::RenderTarget, rhi::TextureState::ShaderRead);
    // Each side's levels, from the sides drawn.
    commands.transition(m_probes.get(), rhi::TextureState::ShaderRead, rhi::TextureState::RenderTarget);
    for (const PrefilterPass& pass : m_passes) {
        const std::array colors{rhi::ColorAttachment{.texture = m_probes.get(),
                                                     .load = rhi::LoadOp::DontCare,
                                                     .mipLevel = pass.level,
                                                     .arrayLayer = (probe * rhi::kCubeFaces) + pass.side}};
        const std::uint32_t size = std::max(kProbeSideSize >> pass.level, 1U);
        commands.beginRendering({.colors = colors, .area = {.width = size, .height = size}});
        commands.setPipeline(m_prefilter.get());
        commands.setBindGroup(kPassGroup, pass.group.get());
        commands.draw(kCoveringVertices, 1, 0, 0);
        commands.endRendering();
    }
    commands.transition(m_probes.get(), rhi::TextureState::RenderTarget, rhi::TextureState::ShaderRead);
    ++m_baked;
}

auto ReflectionProbes::nearest(Vec3 point) const noexcept -> std::optional<std::uint32_t> {
    std::optional<std::uint32_t> found;
    float closest = std::numeric_limits<float>::infinity();
    for (std::size_t index = 0; index < m_baked; ++index) {
        const float distance = lengthSquared(m_positions[index] - point);
        if (distance < closest) {
            closest = distance;
            found = narrow<std::uint32_t>(index);
        }
    }
    return found;
}

} // namespace nx::render
