// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

#include <cstddef> // offsetof

module nx.render;

import std;
import nx.core;
import nx.math;
import nx.rhi;

namespace nx::render {

namespace {

// The bind group of the overlay's pipelines, its bindings and the vertex attributes, as
// shaders/overlay.slang declares them.
constexpr std::uint32_t kFrameGroup = 0;
constexpr std::uint32_t kFrameUniformBinding = 0;
constexpr std::uint32_t kArrowsBinding = 1;

/// The room for arrows a frame starts with, which grows as the frames need, in stretches.
constexpr std::size_t kFirstArrowCapacity = 256;
/// The vertices of an arrows' band, two triangles, which the shader places by their number.
constexpr std::uint32_t kBandVertices = 6;
/// The largest of a display color's channels.
constexpr float kChannelMost = 255.0F;

constexpr std::array kOverlayVertexAttributes{
    rhi::VertexAttribute{.location = 0, .format = rhi::VertexFormat::Float3, .offset = 0},
    rhi::VertexAttribute{.location = 1,
                         .format = rhi::VertexFormat::Unorm8x4,
                         .offset = offsetof(OverlayVertex, color)}, // NOLINT
    rhi::VertexAttribute{.location = 2,
                         .format = rhi::VertexFormat::Float,
                         .offset = offsetof(OverlayVertex, brightness)}}; // NOLINT

/// The frame's uniform: the rows of the view-projection matrix, where the view is, how the arrows are
/// drawn, and how colors reach the target.
struct FrameUniform {
    Mat4 viewProjection{};
    std::array<float, 4> eye{};        ///< Its fourth unused.
    std::array<float, 4> arrowStyle{}; ///< Width and spacing; the others unused.
    std::uint32_t colorEncoding = 0;   ///< ColorEncoding's.
};

/// A stretch of arrows as the shader reads it: its start, its end and its brightness, and the display
/// colors of its arrows' heads and shafts, from 0 to 1.
struct GpuArrows {
    std::array<float, 4> start{}; ///< Its fourth unused.
    std::array<float, 4> endBrightness{};
    std::array<float, 4> headColor{};
    std::array<float, 4> shaftColor{};
};

/// `color`, from 0 to 1.
[[nodiscard]] auto unitColorOf(OverlayColor color) noexcept -> std::array<float, 4> {
    return {static_cast<float>(color.red) / kChannelMost, static_cast<float>(color.green) / kChannelMost,
            static_cast<float>(color.blue) / kChannelMost, static_cast<float>(color.alpha) / kChannelMost};
}

[[nodiscard]] auto gpuArrowsOf(const OverlayArrows& arrows) noexcept -> GpuArrows {
    return GpuArrows{.start = {arrows.start.x, arrows.start.y, arrows.start.z, 0.0F},
                     .endBrightness = {arrows.end.x, arrows.end.y, arrows.end.z, arrows.brightness},
                     .headColor = unitColorOf(arrows.headColor),
                     .shaftColor = unitColorOf(arrows.shaftColor)};
}

} // namespace

auto OverlayRenderer::create(rhi::Device& device, const OverlayRendererDesc& desc)
    -> Result<OverlayRenderer, OverlayError> {
    OverlayRenderer renderer{device, desc.encoding};
    if (auto made = renderer.createPipelines(desc); !made) {
        return fail(OverlayError::GpuFailed, std::move(made.error()));
    }
    renderer.m_frameSlots.reserve(rhi::kMaxFramesInFlight);
    for (std::uint32_t slot = 0; slot < rhi::kMaxFramesInFlight; ++slot) {
        if (auto reserved = renderer.reserve(renderer.m_frameSlots.emplace_back(), kFirstArrowCapacity);
            !reserved) {
            return fail(OverlayError::GpuFailed, std::move(reserved.error()));
        }
    }
    return renderer;
}

auto OverlayRenderer::createPipelines(const OverlayRendererDesc& desc) -> Result<void, rhi::RhiError> {
    rhi::Device& device = *m_device;
    constexpr Flags<rhi::ShaderStage> kBothStages{rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment};
    const std::array frameBindings{rhi::BindingLayout{.binding = kFrameUniformBinding,
                                                      .type = rhi::BindingType::UniformBuffer,
                                                      .stages = kBothStages},
                                   rhi::BindingLayout{.binding = kArrowsBinding,
                                                      .type = rhi::BindingType::StorageBuffer,
                                                      .stages = {rhi::ShaderStage::Vertex}}};
    auto shader = device.createShader({.label = "overlay", .code = desc.shaderCode});
    auto frameLayout = device.createBindGroupLayout({.label = "overlay.frame", .bindings = frameBindings});
    if (!shader || !frameLayout) {
        return std::unexpected(!shader ? std::move(shader.error()) : std::move(frameLayout.error()));
    }
    m_shader = std::move(*shader);
    m_frameLayout = std::move(*frameLayout);
    // Over the scene, behind what it drew in front: its depth tested, not written. Blended by its alpha;
    // the target's alpha kept.
    const std::array vertexBuffers{
        rhi::VertexBufferLayout{.stride = sizeof(OverlayVertex), .attributes = kOverlayVertexAttributes}};
    const std::array targets{
        rhi::ColorTarget{.format = desc.colorFormat,
                         .blend = rhi::BlendState{.sourceColor = rhi::BlendFactor::SourceAlpha,
                                                  .destinationColor = rhi::BlendFactor::OneMinusSourceAlpha,
                                                  .sourceAlpha = rhi::BlendFactor::Zero,
                                                  .destinationAlpha = rhi::BlendFactor::One}}};
    const rhi::DepthState depth{.isWriteEnabled = false, .compare = rhi::CompareOp::GreaterOrEqual};
    const std::array layouts{m_frameLayout.get()};
    const auto meshPipeline = [&](std::string_view label, rhi::PrimitiveTopology topology) {
        return device.createRenderPipeline(
            {.label = label,
             .vertex = {.shader = m_shader.get(), .entryPoint = "vertexMain"},
             .fragment = {.shader = m_shader.get(), .entryPoint = "fragmentMain"},
             .vertexBuffers = vertexBuffers,
             .topology = topology,
             .colorTargets = targets,
             .depth = depth,
             .bindGroupLayouts = layouts});
    };
    auto triangles = meshPipeline("overlay.triangles", rhi::PrimitiveTopology::TriangleList);
    auto lines = meshPipeline("overlay.lines", rhi::PrimitiveTopology::LineList);
    // The arrows read their stretches, no vertex buffer: each band's corners come of its vertices' numbers.
    // Added to what is there as much as they cover it, as the original adds a beam's sprite.
    const std::array addedTargets{
        rhi::ColorTarget{.format = desc.colorFormat,
                         .blend = rhi::BlendState{.sourceColor = rhi::BlendFactor::SourceAlpha,
                                                  .destinationColor = rhi::BlendFactor::One,
                                                  .sourceAlpha = rhi::BlendFactor::Zero,
                                                  .destinationAlpha = rhi::BlendFactor::One}}};
    auto arrows = device.createRenderPipeline(
        {.label = "overlay.arrows",
         .vertex = {.shader = m_shader.get(), .entryPoint = "arrowVertexMain"},
         .fragment = {.shader = m_shader.get(), .entryPoint = "arrowFragmentMain"},
         .colorTargets = addedTargets,
         .depth = depth,
         .bindGroupLayouts = layouts});
    if (!triangles || !lines || !arrows) {
        return std::unexpected(!triangles ? std::move(triangles.error())
                                          : (!lines ? std::move(lines.error()) : std::move(arrows.error())));
    }
    m_trianglePipeline = std::move(*triangles);
    m_linePipeline = std::move(*lines);
    m_arrowPipeline = std::move(*arrows);
    return {};
}

auto OverlayRenderer::keep(rhi::CommandList& commands, const OverlayMesh& mesh)
    -> Result<void, OverlayError> {
    Uploader uploader{*m_device, commands};
    const auto kept =
        [&uploader](std::string_view label,
                    std::span<const OverlayVertex> vertices) -> Result<rhi::OwnedBuffer, rhi::RhiError> {
        if (vertices.empty()) {
            return rhi::OwnedBuffer{};
        }
        return uploader.buffer(label, rhi::BufferUsage::Vertex, rhi::BufferState::Vertex,
                               std::as_bytes(vertices));
    };
    auto triangles = kept("overlay.meshTriangles", mesh.triangles);
    auto lines = kept("overlay.meshLines", mesh.lines);
    if (!triangles || !lines) {
        return fail(OverlayError::GpuFailed,
                    !triangles ? std::move(triangles.error()) : std::move(lines.error()));
    }
    m_meshTriangles = std::move(*triangles);
    m_meshLines = std::move(*lines);
    m_meshTriangleVertices = narrow<std::uint32_t>(mesh.triangles.size());
    m_meshLineVertices = narrow<std::uint32_t>(mesh.lines.size());
    return {};
}

auto OverlayRenderer::reserve(OverlayFrameSlot& slot, std::size_t arrows) -> Result<void, rhi::RhiError> {
    rhi::Device& device = *m_device;
    if (slot.frame.get().isNull()) {
        auto frame = device.createBuffer({.label = "overlay.frame",
                                          .size = sizeof(FrameUniform),
                                          .usage = {rhi::BufferUsage::Uniform},
                                          .location = rhi::MemoryLocation::Upload});
        if (!frame) {
            return std::unexpected{std::move(frame.error())};
        }
        slot.frame = std::move(*frame);
    }
    if (arrows > slot.arrowCapacity || slot.group.get().isNull()) {
        const std::size_t capacity = std::max({arrows, slot.arrowCapacity * 2, kFirstArrowCapacity});
        auto buffer = device.createBuffer({.label = "overlay.arrows",
                                           .size = capacity * sizeof(GpuArrows),
                                           .usage = {rhi::BufferUsage::Storage},
                                           .location = rhi::MemoryLocation::Upload});
        if (!buffer) {
            return std::unexpected{std::move(buffer.error())};
        }
        const std::array bindings{
            rhi::Binding{.binding = kFrameUniformBinding,
                         .resource = rhi::BufferBinding{.buffer = slot.frame.get()}},
            rhi::Binding{.binding = kArrowsBinding, .resource = rhi::BufferBinding{.buffer = buffer->get()}}};
        auto group = device.createBindGroup(
            {.label = "overlay.frame", .layout = m_frameLayout.get(), .bindings = bindings});
        if (!group) {
            return std::unexpected{std::move(group.error())};
        }
        slot.arrows = std::move(*buffer);
        slot.arrowCapacity = capacity;
        slot.group = std::move(*group);
    }
    return {};
}

auto OverlayRenderer::prepare(const OverlayDraw& draw) -> Result<void, OverlayError> {
    m_isMeshDrawn = false;
    m_arrowCount = 0;
    // A ring of kMaxFramesInFlight: the slot written now was last read frames in flight ago.
    OverlayFrameSlot& slot = m_frameSlots[m_drawCount++ % m_frameSlots.size()];
    const bool isMeshDrawn = draw.isMeshShown && (m_meshTriangleVertices > 0 || m_meshLineVertices > 0);
    if (!isMeshDrawn && draw.arrows.empty()) {
        return {};
    }
    if (auto reserved = reserve(slot, draw.arrows.size()); !reserved) {
        return fail(OverlayError::GpuFailed, std::move(reserved.error()));
    }
    const Vec3 eye = draw.camera.position;
    const FrameUniform uniform{
        .viewProjection = viewProjectionMatrix(draw.camera, draw.aspect),
        .eye = {eye.x, eye.y, eye.z, 0.0F},
        .arrowStyle = {draw.arrowStyle.width.value(), draw.arrowStyle.spacing.value(), 0.0F, 0.0F},
        .colorEncoding = std::to_underlying(m_encoding)};
    std::ranges::copy(std::as_bytes(std::span{&uniform, 1}), m_device->mappedBytes(slot.frame.get()).begin());
    const std::span<std::byte> mapped = m_device->mappedBytes(slot.arrows.get());
    for (std::size_t stretch = 0; stretch < draw.arrows.size(); ++stretch) {
        const GpuArrows arrows = gpuArrowsOf(draw.arrows[stretch]);
        std::ranges::copy(std::as_bytes(std::span{&arrows, 1}),
                          mapped.begin() + narrow<std::ptrdiff_t>(stretch * sizeof(GpuArrows)));
    }
    m_isMeshDrawn = isMeshDrawn;
    m_arrowCount = narrow<std::uint32_t>(draw.arrows.size());
    return {};
}

void OverlayRenderer::drawPrepared(rhi::CommandList& commands) const {
    if (!m_isMeshDrawn && m_arrowCount == 0) {
        return;
    }
    expects(m_drawCount > 0, "OverlayRenderer: prepared before it is recorded");
    const OverlayFrameSlot& slot = m_frameSlots[(m_drawCount - 1) % m_frameSlots.size()];
    if (m_isMeshDrawn && m_meshTriangleVertices > 0) {
        commands.setPipeline(m_trianglePipeline.get());
        commands.setBindGroup(kFrameGroup, slot.group.get());
        commands.setVertexBuffer(0, m_meshTriangles.get(), 0);
        commands.draw(m_meshTriangleVertices, 1, 0, 0);
    }
    if (m_isMeshDrawn && m_meshLineVertices > 0) {
        commands.setPipeline(m_linePipeline.get());
        commands.setBindGroup(kFrameGroup, slot.group.get());
        commands.setVertexBuffer(0, m_meshLines.get(), 0);
        commands.draw(m_meshLineVertices, 1, 0, 0);
    }
    if (m_arrowCount > 0) {
        commands.setPipeline(m_arrowPipeline.get());
        commands.setBindGroup(kFrameGroup, slot.group.get());
        commands.draw(kBandVertices, m_arrowCount, 0, 0);
    }
}

auto OverlayRenderer::draw(rhi::CommandList& commands, const OverlayDraw& draw)
    -> Result<void, OverlayError> {
    if (auto prepared = prepare(draw); !prepared) {
        return std::unexpected{std::move(prepared.error())};
    }
    drawPrepared(commands);
    return {};
}

} // namespace nx::render
