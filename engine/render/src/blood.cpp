// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.render;

import std;
import nx.core;
import nx.math;
import nx.rhi;

namespace nx::render {

namespace {

// The bind group of the blood pipeline and its bindings, as shaders/blood.slang declares them.
constexpr std::uint32_t kFrameGroup = 0;
constexpr std::uint32_t kFrameUniformBinding = 0;
constexpr std::uint32_t kDropsBinding = 1;

/// The room for drops a frame starts with, which grows as the frames need.
constexpr std::size_t kFirstDropCapacity = 256;
/// The vertices of a drop's quad, two triangles, which the shader places by their number.
constexpr std::uint32_t kQuadVertices = 6;
/// The largest of a display color's channels.
constexpr float kChannelMost = 255.0F;

/// The frame's uniform: the rows of the view-projection matrix, where the view is and its right and up,
/// the fog, how droplets are drawn, and how colors reach the target.
struct FrameUniform {
    Mat4 viewProjection{};
    std::array<float, 4> eye{};   ///< Its fourth unused.
    std::array<float, 4> right{}; ///< Its fourth unused.
    std::array<float, 4> up{};    ///< Its fourth unused.
    FogBlock fog{};
    std::array<float, 4> style{};    ///< The streak time, in seconds; the others unused.
    std::uint32_t colorEncoding = 0; ///< ColorEncoding's.
};

/// A drop as the shader reads it: where it is and its radius, its velocity and how opaque it is, its
/// display color from 0 to 1 and the light where it is, its shape, and a splat's normal and its seed.
struct GpuDrop {
    std::array<float, 4> positionRadius{};
    std::array<float, 4> velocityOpacity{};
    std::array<float, 4> colorLight{};
    std::array<std::uint32_t, 4> shape{}; ///< BloodShape's; the others unused.
    std::array<float, 4> normalSeed{};
};

[[nodiscard]] auto rowOf(Vec3 vector) noexcept -> std::array<float, 4> {
    return {vector.x, vector.y, vector.z, 0.0F};
}

[[nodiscard]] auto gpuDropOf(const BloodInstance& drop) noexcept -> GpuDrop {
    return GpuDrop{.positionRadius = {drop.position.x, drop.position.y, drop.position.z, drop.radius.value()},
                   .velocityOpacity = {drop.velocity.x, drop.velocity.y, drop.velocity.z, drop.opacity},
                   .colorLight = {static_cast<float>(drop.color.red) / kChannelMost,
                                  static_cast<float>(drop.color.green) / kChannelMost,
                                  static_cast<float>(drop.color.blue) / kChannelMost, drop.light},
                   .shape = {std::to_underlying(drop.shape), 0, 0, 0},
                   .normalSeed = {drop.normal.x, drop.normal.y, drop.normal.z, drop.seed}};
}

} // namespace

auto BloodRenderer::create(rhi::Device& device, const BloodRendererDesc& desc)
    -> Result<BloodRenderer, BloodError> {
    BloodRenderer renderer{device, desc.encoding};
    if (auto made = renderer.createPipeline(desc); !made) {
        return fail(BloodError::GpuFailed, std::move(made.error()));
    }
    renderer.frameSlots.reserve(rhi::kMaxFramesInFlight);
    for (std::uint32_t slot = 0; slot < rhi::kMaxFramesInFlight; ++slot) {
        if (auto reserved = renderer.reserve(renderer.frameSlots.emplace_back(), kFirstDropCapacity);
            !reserved) {
            return fail(BloodError::GpuFailed, std::move(reserved.error()));
        }
    }
    return renderer;
}

auto BloodRenderer::createPipeline(const BloodRendererDesc& desc) -> Result<void, rhi::RhiError> {
    rhi::Device& device = *gpu;
    constexpr Flags<rhi::ShaderStage> kBothStages{rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment};
    const std::array frameBindings{rhi::BindingLayout{.binding = kFrameUniformBinding,
                                                      .type = rhi::BindingType::UniformBuffer,
                                                      .stages = kBothStages},
                                   rhi::BindingLayout{.binding = kDropsBinding,
                                                      .type = rhi::BindingType::StorageBuffer,
                                                      .stages = {rhi::ShaderStage::Vertex}}};
    auto compiled = device.createShader({.label = "blood", .code = desc.shaderCode});
    auto layout = device.createBindGroupLayout({.label = "blood.frame", .bindings = frameBindings});
    if (!compiled || !layout) {
        return std::unexpected(!compiled ? std::move(compiled.error()) : std::move(layout.error()));
    }
    shader = std::move(*compiled);
    frameLayout = std::move(*layout);
    // Blended over what is there by how opaque it is, its depth tested but not written; no vertex buffer:
    // each quad's corners come of its vertices' numbers, turned to the view, so none is culled.
    const std::array targets{rhi::ColorTarget{
        .format = desc.colorFormat,
        .blend = rhi::BlendState{.sourceColor = rhi::BlendFactor::SourceAlpha,
                                 .destinationColor = rhi::BlendFactor::OneMinusSourceAlpha,
                                 .sourceAlpha = rhi::BlendFactor::SourceAlpha,
                                 .destinationAlpha = rhi::BlendFactor::OneMinusSourceAlpha}}};
    const std::array layouts{frameLayout.get()};
    auto made = device.createRenderPipeline(
        {.label = "blood",
         .vertex = {.shader = shader.get(), .entryPoint = "vertexMain"},
         .fragment = {.shader = shader.get(), .entryPoint = "fragmentMain"},
         .cullMode = rhi::CullMode::None,
         .frontFace = rhi::FrontFace::CounterClockwise,
         .colorTargets = targets,
         .depth = rhi::DepthState{.isWriteEnabled = false, .compare = rhi::CompareOp::Greater},
         .bindGroupLayouts = layouts});
    if (!made) {
        return std::unexpected{std::move(made.error())};
    }
    pipeline = std::move(*made);
    return {};
}

auto BloodRenderer::reserve(BloodFrameSlot& slot, std::size_t drops) -> Result<void, rhi::RhiError> {
    rhi::Device& device = *gpu;
    if (slot.frame.get().isNull()) {
        auto frame = device.createBuffer({.label = "blood.frame",
                                          .size = sizeof(FrameUniform),
                                          .usage = {rhi::BufferUsage::Uniform},
                                          .location = rhi::MemoryLocation::Upload});
        if (!frame) {
            return std::unexpected{std::move(frame.error())};
        }
        slot.frame = std::move(*frame);
    }
    if (drops > slot.dropCapacity || slot.group.get().isNull()) {
        const std::size_t capacity = std::max({drops, slot.dropCapacity * 2, kFirstDropCapacity});
        auto buffer = device.createBuffer({.label = "blood.drops",
                                           .size = capacity * sizeof(GpuDrop),
                                           .usage = {rhi::BufferUsage::Storage},
                                           .location = rhi::MemoryLocation::Upload});
        if (!buffer) {
            return std::unexpected{std::move(buffer.error())};
        }
        const std::array bindings{
            rhi::Binding{.binding = kFrameUniformBinding,
                         .resource = rhi::BufferBinding{.buffer = slot.frame.get()}},
            rhi::Binding{.binding = kDropsBinding, .resource = rhi::BufferBinding{.buffer = buffer->get()}}};
        auto group = device.createBindGroup(
            {.label = "blood.frame", .layout = frameLayout.get(), .bindings = bindings});
        if (!group) {
            return std::unexpected{std::move(group.error())};
        }
        slot.drops = std::move(*buffer);
        slot.dropCapacity = capacity;
        slot.group = std::move(*group);
    }
    return {};
}

auto BloodRenderer::prepare(const BloodDraw& draw) -> Result<void, BloodError> {
    dropCount = 0;
    // A ring of kMaxFramesInFlight: the slot written now was last read frames in flight ago.
    BloodFrameSlot& slot = frameSlots[drawCount++ % frameSlots.size()];
    if (draw.drops.empty()) {
        return {};
    }
    if (auto reserved = reserve(slot, draw.drops.size()); !reserved) {
        return fail(BloodError::GpuFailed, std::move(reserved.error()));
    }
    const AngleVectors view = angleVectors(draw.camera.angles);
    const FrameUniform uniform{.viewProjection = viewProjectionMatrix(draw.camera, draw.aspect),
                               .eye = rowOf(draw.camera.position),
                               .right = rowOf(view.right),
                               .up = rowOf(view.up),
                               .fog = fogBlockOf(draw.fog, draw.camera, encoding),
                               .style = {draw.streakTime.count(), 0.0F, 0.0F, 0.0F},
                               .colorEncoding = std::to_underlying(encoding)};
    std::ranges::copy(std::as_bytes(std::span{&uniform, 1}), gpu->mappedBytes(slot.frame.get()).begin());
    const std::span<std::byte> mapped = gpu->mappedBytes(slot.drops.get());
    for (std::size_t index = 0; index < draw.drops.size(); ++index) {
        const GpuDrop drop = gpuDropOf(draw.drops[index]);
        std::ranges::copy(std::as_bytes(std::span{&drop, 1}),
                          mapped.begin() + narrow<std::ptrdiff_t>(index * sizeof(GpuDrop)));
    }
    dropCount = narrow<std::uint32_t>(draw.drops.size());
    return {};
}

void BloodRenderer::drawPrepared(rhi::CommandList& commands) const {
    if (dropCount == 0) {
        return;
    }
    expects(drawCount > 0, "BloodRenderer: prepared before it is recorded");
    const BloodFrameSlot& slot = frameSlots[(drawCount - 1) % frameSlots.size()];
    commands.setPipeline(pipeline.get());
    commands.setBindGroup(kFrameGroup, slot.group.get());
    commands.draw(kQuadVertices, dropCount, 0, 0);
}

auto BloodRenderer::draw(rhi::CommandList& commands, const BloodDraw& draw) -> Result<void, BloodError> {
    if (auto prepared = prepare(draw); !prepared) {
        return std::unexpected{std::move(prepared.error())};
    }
    drawPrepared(commands);
    return {};
}

} // namespace nx::render
