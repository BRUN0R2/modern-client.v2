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

// The bind group of the overlay's pipelines, its binding and the vertex attributes, as
// shaders/overlay.slang declares them.
constexpr std::uint32_t kFrameGroup = 0;
constexpr std::uint32_t kFrameUniformBinding = 0;

/// The room a frame starts with, which grows as the frames need, in vertices.
constexpr std::size_t kFirstVertexCapacity = 1024;

constexpr std::array kOverlayVertexAttributes{
    rhi::VertexAttribute{.location = 0, .format = rhi::VertexFormat::Float3, .offset = 0},
    rhi::VertexAttribute{.location = 1,
                         .format = rhi::VertexFormat::Unorm8x4,
                         .offset = offsetof(OverlayVertex, color)}}; // NOLINT

/// The frame's uniform: the rows of the view-projection matrix, and how colors reach the target.
struct FrameUniform {
    Mat4 viewProjection{};
    std::uint32_t colorEncoding = 0; ///< ColorEncoding's.
};

} // namespace

auto OverlayRenderer::create(rhi::Device& device, const OverlayRendererDesc& desc)
    -> Result<OverlayRenderer, OverlayError> {
    OverlayRenderer renderer{device, desc.encoding};
    if (auto made = renderer.createPipelines(desc); !made) {
        return fail(OverlayError::GpuFailed, std::move(made.error()));
    }
    renderer.m_frameSlots.reserve(rhi::kMaxFramesInFlight);
    for (std::uint32_t slot = 0; slot < rhi::kMaxFramesInFlight; ++slot) {
        if (auto reserved = renderer.reserve(renderer.m_frameSlots.emplace_back(), kFirstVertexCapacity);
            !reserved) {
            return fail(OverlayError::GpuFailed, std::move(reserved.error()));
        }
    }
    return renderer;
}

auto OverlayRenderer::createPipelines(const OverlayRendererDesc& desc) -> Result<void, rhi::RhiError> {
    rhi::Device& device = *m_device;
    constexpr Flags<rhi::ShaderStage> kBothStages{rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment};
    const std::array frameBindings{rhi::BindingLayout{
        .binding = kFrameUniformBinding, .type = rhi::BindingType::UniformBuffer, .stages = kBothStages}};
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
    const rhi::DepthState depth{
        .format = desc.depthFormat, .isWriteEnabled = false, .compare = rhi::CompareOp::GreaterOrEqual};
    const std::array layouts{m_frameLayout.get()};
    const auto pipeline = [&](std::string_view label, rhi::PrimitiveTopology topology) {
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
    auto triangles = pipeline("overlay.triangles", rhi::PrimitiveTopology::TriangleList);
    auto lines = pipeline("overlay.lines", rhi::PrimitiveTopology::LineList);
    if (!triangles || !lines) {
        return std::unexpected(!triangles ? std::move(triangles.error()) : std::move(lines.error()));
    }
    m_trianglePipeline = std::move(*triangles);
    m_linePipeline = std::move(*lines);
    return {};
}

auto OverlayRenderer::reserve(OverlayFrameSlot& slot, std::size_t vertices) -> Result<void, rhi::RhiError> {
    rhi::Device& device = *m_device;
    if (slot.frame.get().isNull()) {
        auto frame = device.createBuffer({.label = "overlay.frame",
                                          .size = sizeof(FrameUniform),
                                          .usage = {rhi::BufferUsage::Uniform},
                                          .location = rhi::MemoryLocation::Upload});
        if (!frame) {
            return std::unexpected{std::move(frame.error())};
        }
        const std::array bindings{rhi::Binding{.binding = kFrameUniformBinding,
                                               .resource = rhi::BufferBinding{.buffer = frame->get()}}};
        auto group = device.createBindGroup(
            {.label = "overlay.frame", .layout = m_frameLayout.get(), .bindings = bindings});
        if (!group) {
            return std::unexpected{std::move(group.error())};
        }
        slot.frame = std::move(*frame);
        slot.group = std::move(*group);
    }
    if (vertices > slot.vertexCapacity) {
        const std::size_t capacity = std::max({vertices, slot.vertexCapacity * 2, kFirstVertexCapacity});
        auto buffer = device.createBuffer({.label = "overlay.vertices",
                                           .size = capacity * sizeof(OverlayVertex),
                                           .usage = {rhi::BufferUsage::Vertex},
                                           .location = rhi::MemoryLocation::Upload});
        if (!buffer) {
            return std::unexpected{std::move(buffer.error())};
        }
        slot.vertices = std::move(*buffer);
        slot.vertexCapacity = capacity;
    }
    return {};
}

auto OverlayRenderer::prepare(const OverlayDraw& draw) -> Result<void, OverlayError> {
    m_triangleVertices = 0;
    m_lineVertices = 0;
    // A ring of kMaxFramesInFlight: the slot written now was last read frames in flight ago.
    OverlayFrameSlot& slot = m_frameSlots[m_drawCount++ % m_frameSlots.size()];
    const std::size_t vertices = draw.triangles.size() + draw.lines.size();
    if (vertices == 0) {
        return {};
    }
    if (auto reserved = reserve(slot, vertices); !reserved) {
        return fail(OverlayError::GpuFailed, std::move(reserved.error()));
    }
    const FrameUniform uniform{.viewProjection = viewProjectionMatrix(draw.camera, draw.aspect),
                               .colorEncoding = std::to_underlying(m_encoding)};
    std::ranges::copy(std::as_bytes(std::span{&uniform, 1}), m_device->mappedBytes(slot.frame.get()).begin());
    const std::span<std::byte> mapped = m_device->mappedBytes(slot.vertices.get());
    const auto triangleBytes = std::as_bytes(draw.triangles);
    std::ranges::copy(triangleBytes, mapped.begin());
    std::ranges::copy(std::as_bytes(draw.lines),
                      mapped.begin() + narrow<std::ptrdiff_t>(triangleBytes.size()));
    m_triangleVertices = narrow<std::uint32_t>(draw.triangles.size());
    m_lineVertices = narrow<std::uint32_t>(draw.lines.size());
    return {};
}

void OverlayRenderer::record(rhi::CommandList& commands) const {
    if (m_triangleVertices == 0 && m_lineVertices == 0) {
        return;
    }
    expects(m_drawCount > 0, "OverlayRenderer: prepared before it is recorded");
    const OverlayFrameSlot& slot = m_frameSlots[(m_drawCount - 1) % m_frameSlots.size()];
    commands.setVertexBuffer(0, slot.vertices.get(), 0);
    if (m_triangleVertices > 0) {
        commands.setPipeline(m_trianglePipeline.get());
        commands.setBindGroup(kFrameGroup, slot.group.get());
        commands.draw(m_triangleVertices, 1, 0, 0);
    }
    if (m_lineVertices > 0) {
        commands.setPipeline(m_linePipeline.get());
        commands.setBindGroup(kFrameGroup, slot.group.get());
        commands.draw(m_lineVertices, 1, m_triangleVertices, 0);
    }
}

auto OverlayRenderer::draw(rhi::CommandList& commands, const OverlayDraw& draw)
    -> Result<void, OverlayError> {
    if (auto prepared = prepare(draw); !prepared) {
        return std::unexpected{std::move(prepared.error())};
    }
    record(commands);
    return {};
}

} // namespace nx::render
