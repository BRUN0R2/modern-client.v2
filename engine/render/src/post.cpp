// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.render;

import std;
import nx.core;
import nx.rhi;

namespace nx::render {

namespace {

// The bind group of every pass and its bindings, as shaders/post.slang declares them.
constexpr std::uint32_t kPassGroup = 0;
constexpr std::uint32_t kUniformBinding = 0;
constexpr std::uint32_t kSourceBinding = 1;
constexpr std::uint32_t kSamplerBinding = 2;
constexpr std::uint32_t kBloomBinding = 3;
constexpr std::uint32_t kEffectsBinding = 4;

/// The vertices of the triangle that covers a target.
constexpr std::uint32_t kCoveringVertices = 3;
/// Each pass's uniform is this far from the one before: any device's least offset alignment.
constexpr std::uint64_t kUniformStride = 256;
/// The passes with a uniform: each reduction, each enlargement, the effects' light into the first
/// level, and the tone pass.
constexpr std::size_t kPassCount = kBloomLevels + (kBloomLevels - 1) + 1 + 1;
constexpr std::size_t kEffectsSlot = kPassCount - 2;
constexpr std::size_t kToneSlot = kPassCount - 1;

/// What a pass reads (shaders/post.slang's Pass).
struct PassUniform {
    float sourceTexelWidth = 0.0F;
    float sourceTexelHeight = 0.0F;
    float bloomStrength = 0.0F;
    float knee = 0.0F;
    float bloomNormalization = 0.0F;
};

/// The size of level `level` of the chain for a screen of `size`: half the one before, at least a texel.
[[nodiscard]] auto levelSizeOf(rhi::Extent2d size, std::size_t level) noexcept -> rhi::Extent2d {
    const auto shift = narrow<std::uint32_t>(level + 1);
    return {.width = std::max(size.width >> shift, 1U), .height = std::max(size.height >> shift, 1U)};
}

[[nodiscard]] auto gpuFailed(std::string_view what, Failure<rhi::RhiError> cause)
    -> std::unexpected<Failure<PostError>> {
    return fail(PostError::GpuFailed, std::string{what}, std::move(cause));
}

} // namespace

auto PostRenderer::create(rhi::Device& device, const PostRendererDesc& desc)
    -> Result<PostRenderer, PostError> {
    PostRenderer post{device};
    auto shader = device.createShader({.label = "post", .code = desc.shaderCode});
    if (!shader) {
        return gpuFailed("the post shader", std::move(shader.error()));
    }
    post.m_shader = std::move(*shader);
    auto sampler = device.createSampler({.label = "post.linearClamp",
                                         .magnify = rhi::Filter::Linear,
                                         .minify = rhi::Filter::Linear,
                                         .mipmap = rhi::Filter::Nearest,
                                         .address = rhi::AddressMode::ClampToEdge});
    if (!sampler) {
        return gpuFailed("the post sampler", std::move(sampler.error()));
    }
    post.m_sampler = std::move(*sampler);
    const Flags<rhi::ShaderStage> fragment{rhi::ShaderStage::Fragment};
    const std::array bloomBindings{
        rhi::BindingLayout{
            .binding = kUniformBinding, .type = rhi::BindingType::UniformBuffer, .stages = fragment},
        rhi::BindingLayout{
            .binding = kSourceBinding, .type = rhi::BindingType::SampledTexture, .stages = fragment},
        rhi::BindingLayout{
            .binding = kSamplerBinding, .type = rhi::BindingType::Sampler, .stages = fragment}};
    const std::array toneBindings{
        bloomBindings[0], bloomBindings[1], bloomBindings[2],
        rhi::BindingLayout{
            .binding = kBloomBinding, .type = rhi::BindingType::SampledTexture, .stages = fragment},
        rhi::BindingLayout{
            .binding = kEffectsBinding, .type = rhi::BindingType::SampledTexture, .stages = fragment}};
    auto bloomLayout = device.createBindGroupLayout({.label = "post.bloom", .bindings = bloomBindings});
    auto toneLayout = device.createBindGroupLayout({.label = "post.tone", .bindings = toneBindings});
    if (!bloomLayout || !toneLayout) {
        return gpuFailed("the post bind group layouts",
                         std::move(bloomLayout ? toneLayout.error() : bloomLayout.error()));
    }
    post.m_bloomLayout = std::move(*bloomLayout);
    post.m_toneLayout = std::move(*toneLayout);
    // The chain keeps light beyond 1 in four bytes a texel where the device can draw to them.
    post.m_bloomFormat =
        device.createTexture({.label = "post.formatProbe",
                              .format = rhi::Format::Rg11b10Ufloat,
                              .extent = {.width = 1, .height = 1},
                              .usage = {rhi::TextureUsage::Sampled, rhi::TextureUsage::RenderTarget}})
                .has_value()
            ? rhi::Format::Rg11b10Ufloat
            : rhi::Format::Rgba16Float;
    const auto pipeline = [&](std::string_view label, std::string_view entry, rhi::Format format,
                              std::optional<rhi::BlendState> blend, rhi::BindGroupLayoutHandle layout,
                              std::optional<rhi::DepthState> depth =
                                  std::nullopt) -> Result<rhi::OwnedRenderPipeline, rhi::RhiError> {
        const std::array targets{rhi::ColorTarget{.format = format, .blend = blend}};
        const std::array layouts{layout};
        return device.createRenderPipeline(
            {.label = label,
             .vertex = {.shader = post.m_shader.get(), .entryPoint = "fullscreenMain"},
             .fragment = {.shader = post.m_shader.get(), .entryPoint = entry},
             .colorTargets = targets,
             .depth = depth,
             .bindGroupLayouts = layouts});
    };
    const rhi::BlendState added{.sourceColor = rhi::BlendFactor::One,
                                .destinationColor = rhi::BlendFactor::One,
                                .sourceAlpha = rhi::BlendFactor::One,
                                .destinationAlpha = rhi::BlendFactor::One};
    auto reduceFirst = pipeline("post.reduceFirst", "reduceFirstMain", post.m_bloomFormat, std::nullopt,
                                post.m_bloomLayout.get());
    auto reduceEffects = pipeline("post.reduceEffects", "reduceEffectsMain", post.m_bloomFormat, added,
                                  post.m_bloomLayout.get());
    auto reduce =
        pipeline("post.reduce", "reduceMain", post.m_bloomFormat, std::nullopt, post.m_bloomLayout.get());
    auto enlarge =
        pipeline("post.enlarge", "enlargeMain", post.m_bloomFormat, added, post.m_bloomLayout.get());
    // The tone pass shares its render pass with the screen's pictures: the frame's depth, neither tested
    // nor written.
    const std::optional<rhi::DepthState> frameDepth =
        desc.depthFormat == rhi::Format::Undefined
            ? std::nullopt
            : std::optional{rhi::DepthState{
                  .format = desc.depthFormat, .isWriteEnabled = false, .compare = rhi::CompareOp::Always}};
    auto tone = pipeline("post.tone", "toneMain", desc.outputFormat, std::nullopt, post.m_toneLayout.get(),
                         frameDepth);
    for (auto* const made : {&reduceFirst, &reduceEffects, &reduce, &enlarge, &tone}) {
        if (!*made) {
            return gpuFailed("a post pipeline", std::move(made->error()));
        }
    }
    post.m_reduceFirst = std::move(*reduceFirst);
    post.m_reduceEffects = std::move(*reduceEffects);
    post.m_reduce = std::move(*reduce);
    post.m_enlarge = std::move(*enlarge);
    post.m_tone = std::move(*tone);
    auto uniforms = device.createBuffer({.label = "post.uniforms",
                                         .size = kUniformStride * kPassCount,
                                         .usage = {rhi::BufferUsage::Uniform},
                                         .location = rhi::MemoryLocation::Upload});
    if (!uniforms) {
        return gpuFailed("the post uniforms", std::move(uniforms.error()));
    }
    post.m_uniforms = std::move(*uniforms);
    return post;
}

auto PostRenderer::passGroup(std::size_t slot, rhi::TextureHandle source, rhi::Extent2d sourceSize,
                             std::optional<ToneSources> tone) -> Result<rhi::OwnedBindGroup, rhi::RhiError> {
    const PassUniform uniform{.sourceTexelWidth = 1.0F / static_cast<float>(sourceSize.width),
                              .sourceTexelHeight = 1.0F / static_cast<float>(sourceSize.height),
                              .bloomStrength = kBloomStrength,
                              .knee = kToneKnee,
                              .bloomNormalization = 1.0F / static_cast<float>(kBloomLevels)};
    const std::uint64_t offset = kUniformStride * slot;
    std::ranges::copy(std::as_bytes(std::span{&uniform, 1}),
                      m_device->mappedBytes(m_uniforms.get()).subspan(narrow<std::size_t>(offset)).begin());
    std::vector<rhi::Binding> bindings{
        rhi::Binding{.binding = kUniformBinding,
                     .resource = rhi::BufferBinding{.buffer = m_uniforms.get(),
                                                    .offset = offset,
                                                    .size = sizeof(PassUniform)}},
        rhi::Binding{.binding = kSourceBinding, .resource = source},
        rhi::Binding{.binding = kSamplerBinding, .resource = m_sampler.get()}};
    if (tone) {
        bindings.push_back(rhi::Binding{.binding = kBloomBinding, .resource = tone->bloom});
        bindings.push_back(rhi::Binding{.binding = kEffectsBinding, .resource = tone->effects});
    }
    return m_device->createBindGroup({.label = "post.pass",
                                      .layout = tone ? m_toneLayout.get() : m_bloomLayout.get(),
                                      .bindings = bindings});
}

auto PostRenderer::resize(rhi::Extent2d size) -> Result<void, PostError> {
    expects(size.width > 0 && size.height > 0, "PostRenderer::resize: a screen of some size");
    if (size == m_size && !m_scene.get().isNull()) {
        return {};
    }
    m_toneGroup = {};
    m_effectsReduction = {};
    m_enlargements.clear();
    m_reductions.clear();
    m_levels.clear();
    auto scene =
        m_device->createTexture({.label = "post.scene",
                                 .format = sceneFormat(),
                                 .extent = size,
                                 .usage = {rhi::TextureUsage::Sampled, rhi::TextureUsage::RenderTarget}});
    auto effects =
        m_device->createTexture({.label = "post.effects",
                                 .format = effectsFormat(),
                                 .extent = size,
                                 .usage = {rhi::TextureUsage::Sampled, rhi::TextureUsage::RenderTarget}});
    if (!scene || !effects) {
        return gpuFailed("the scene's target and the effects' layer",
                         std::move(!scene ? scene.error() : effects.error()));
    }
    m_scene = std::move(*scene);
    m_effects = std::move(*effects);
    for (std::size_t level = 0; level < kBloomLevels; ++level) {
        auto image =
            m_device->createTexture({.label = "post.bloom",
                                     .format = m_bloomFormat,
                                     .extent = levelSizeOf(size, level),
                                     .usage = {rhi::TextureUsage::Sampled, rhi::TextureUsage::RenderTarget}});
        if (!image) {
            return gpuFailed("an image of the bloom", std::move(image.error()));
        }
        m_levels.push_back(std::move(*image));
    }
    std::size_t slot = 0;
    for (std::size_t level = 0; level < kBloomLevels; ++level) {
        const rhi::TextureHandle source = level == 0 ? m_scene.get() : m_levels[level - 1].get();
        const rhi::Extent2d sourceSize = level == 0 ? size : levelSizeOf(size, level - 1);
        auto group = passGroup(slot++, source, sourceSize, std::nullopt);
        if (!group) {
            return gpuFailed("a reduction's bind group", std::move(group.error()));
        }
        m_reductions.push_back(BloomPass{
            .target = m_levels[level].get(), .size = levelSizeOf(size, level), .group = std::move(*group)});
    }
    auto effectsGroup = passGroup(kEffectsSlot, m_effects.get(), size, std::nullopt);
    if (!effectsGroup) {
        return gpuFailed("the effects' bind group", std::move(effectsGroup.error()));
    }
    m_effectsReduction = BloomPass{
        .target = m_levels.front().get(), .size = levelSizeOf(size, 0), .group = std::move(*effectsGroup)};
    for (std::size_t level = kBloomLevels - 1; level > 0; --level) {
        auto group = passGroup(slot++, m_levels[level].get(), levelSizeOf(size, level), std::nullopt);
        if (!group) {
            return gpuFailed("an enlargement's bind group", std::move(group.error()));
        }
        m_enlargements.push_back(BloomPass{.target = m_levels[level - 1].get(),
                                           .size = levelSizeOf(size, level - 1),
                                           .group = std::move(*group)});
    }
    auto tone = passGroup(kToneSlot, m_scene.get(), size,
                          ToneSources{.bloom = m_levels.front().get(), .effects = m_effects.get()});
    if (!tone) {
        return gpuFailed("the tone pass's bind group", std::move(tone.error()));
    }
    m_toneGroup = std::move(*tone);
    m_size = size;
    return {};
}

auto PostRenderer::sceneTarget() const noexcept -> rhi::TextureHandle {
    return m_scene.get();
}

auto PostRenderer::effectsTarget() const noexcept -> rhi::TextureHandle {
    return m_effects.get();
}

void PostRenderer::bloom(rhi::CommandList& commands) const {
    expects(!m_toneGroup.get().isNull(), "PostRenderer::bloom: resized to a screen first");
    const auto run = [&commands](const BloomPass& pass, rhi::RenderPipelineHandle pipeline,
                                 rhi::LoadOp load) {
        const std::array colors{rhi::ColorAttachment{.texture = pass.target, .load = load}};
        commands.beginRendering({.colors = colors, .area = pass.size});
        commands.setPipeline(pipeline);
        commands.setBindGroup(kPassGroup, pass.group.get());
        commands.draw(kCoveringVertices, 1, 0, 0);
        commands.endRendering();
    };
    for (std::size_t level = 0; level < m_reductions.size(); ++level) {
        const BloomPass& pass = m_reductions[level];
        commands.transition(pass.target, rhi::TextureState::Undefined, rhi::TextureState::RenderTarget);
        run(pass, level == 0 ? m_reduceFirst.get() : m_reduce.get(), rhi::LoadOp::DontCare);
        if (level == 0) {
            // The effects' light, added to the scene's in the first level, from which the others reduce.
            commands.transition(pass.target, rhi::TextureState::RenderTarget,
                                rhi::TextureState::RenderTarget);
            run(m_effectsReduction, m_reduceEffects.get(), rhi::LoadOp::Load);
        }
        commands.transition(pass.target, rhi::TextureState::RenderTarget, rhi::TextureState::ShaderRead);
    }
    for (const BloomPass& pass : m_enlargements) {
        commands.transition(pass.target, rhi::TextureState::ShaderRead, rhi::TextureState::RenderTarget);
        run(pass, m_enlarge.get(), rhi::LoadOp::Load);
        commands.transition(pass.target, rhi::TextureState::RenderTarget, rhi::TextureState::ShaderRead);
    }
}

void PostRenderer::drawToneMapped(rhi::CommandList& commands) const {
    commands.setPipeline(m_tone.get());
    commands.setBindGroup(kPassGroup, m_toneGroup.get());
    commands.draw(kCoveringVertices, 1, 0, 0);
}

} // namespace nx::render
