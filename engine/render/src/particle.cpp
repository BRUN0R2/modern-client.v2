// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

#include <cstddef> // offsetof

module nx.render;

import std;
import nx.core;
import nx.math;
import nx.rhi;
import nx.assets;

namespace nx::render {

namespace {

// The bind groups of the particle pipeline, their bindings and the vertex attributes, as
// shaders/particle.slang declares them.
constexpr std::uint32_t kFrameGroup = 0;
constexpr std::uint32_t kImageGroup = 1;
constexpr std::uint32_t kFrameUniformBinding = 0;
constexpr std::uint32_t kImageBinding = 0;
constexpr std::uint32_t kImageSamplerBinding = 1;

constexpr float kChannelTop = 255.0F;
/// The vertices of a quad: two triangles.
constexpr std::uint32_t kQuadVertices = 6;
/// The room a frame starts with, which grows as the frames need, in quads.
constexpr std::size_t kFirstQuadCapacity = 256;

/// Half the side of a particle's quad near the view (PART_SIZE, r_drawparticles 1), in units; it grows
/// by kSizePerDistance of its distance in front of the view, that distance and this half at least
/// kNearSizeDistance (CL_DrawParticles).
constexpr float kHalfSize = 1.0F;
constexpr float kSizePerDistance = 0.002F;
constexpr float kNearSizeDistance = 20.0F;

/// How far along the streaks' sprite a streak's quad reaches, from its start to its end (CL_DrawTracers).
constexpr float kStreakImageLength = 0.8F;

/// The engine's dot (R_InitParticleTexture's): kDotSide texels a side, clear white but for a round dot
/// of kDotWidth texels a side in its first corner, its own corners clear, as GoldSrc has it.
constexpr std::uint32_t kDotSide = 8;
constexpr std::uint32_t kDotWidth = 4;
constexpr std::size_t kTexelBytes = 4;

/// A corner of a particle's quad: where it is, its texture coordinates, and its color and opacity.
struct ParticleVertex {
    Vec3 position{};
    float u = 0.0F;
    float v = 0.0F;
    Vec4 color{};
};
/// The 4-byte words of a vertex: the position's three, the texture's two and the color's four.
constexpr std::size_t kParticleVertexWords = 9;
static_assert(sizeof(ParticleVertex) == kParticleVertexWords * sizeof(float),
              "the vertex layout of the particle pipeline");

constexpr std::array kParticleVertexAttributes{
    rhi::VertexAttribute{.location = 0, .format = rhi::VertexFormat::Float3, .offset = 0},
    rhi::VertexAttribute{
        .location = 1, .format = rhi::VertexFormat::Float2, .offset = offsetof(ParticleVertex, u)}, // NOLINT
    rhi::VertexAttribute{.location = 2,
                         .format = rhi::VertexFormat::Float4,
                         .offset = offsetof(ParticleVertex, color)}}; // NOLINT

/// The frame's uniform: the rows of the view-projection matrix, the fog, and how colors reach the
/// target.
struct FrameUniform {
    Mat4 viewProjection{};
    FogBlock fog{};
    std::uint32_t colorEncoding = 0; ///< ColorEncoding's.
};

/// The RGBA texels of the engine's dot.
[[nodiscard]] auto dotTexels() -> std::vector<std::byte> {
    std::vector<std::byte> texels(std::size_t{kDotSide} * kDotSide * kTexelBytes, std::byte{0});
    for (std::uint32_t y = 0; y < kDotSide; ++y) {
        for (std::uint32_t x = 0; x < kDotSide; ++x) {
            const bool isInDot = x < kDotWidth && y < kDotWidth;
            const bool isDotCorner = (x == 0 || x == kDotWidth - 1) && (y == 0 || y == kDotWidth - 1);
            const std::size_t at = ((std::size_t{y} * kDotSide) + x) * kTexelBytes;
            // White, clear but in the dot.
            texels[at] = texels[at + 1] = texels[at + 2] = std::byte{0xff};
            texels[at + 3] = isInDot && !isDotCorner ? std::byte{0xff} : std::byte{0};
        }
    }
    return texels;
}

/// The vertex color of `color`, as opaque as `amount`.
[[nodiscard]] auto vertexColorOf(RenderColor color, std::uint8_t amount) noexcept -> Vec4 {
    return Vec4{.x = static_cast<float>(color.red) / kChannelTop,
                .y = static_cast<float>(color.green) / kChannelTop,
                .z = static_cast<float>(color.blue) / kChannelTop,
                .w = static_cast<float>(amount) / kChannelTop};
}

/// Appends the quad of `particle` seen by a view at `eye` looking along `view` (CL_DrawParticles).
void addQuad(std::vector<ParticleVertex>& vertices, const ParticleInstance& particle, Vec3 eye,
             const AngleVectors& view) {
    const float reach = kHalfSize + dot(particle.origin - eye, view.forward);
    const float size = reach < kNearSizeDistance ? kHalfSize : kHalfSize + (reach * kSizePerDistance);
    const Vec3 right = view.right * size;
    const Vec3 up = view.up * size;
    const Vec4 color = vertexColorOf(particle.color, particle.amount);
    // The original's corners and texture coordinates: the dot lies toward the view's right and up.
    const ParticleVertex upLeft{
        .position = particle.origin - right + up, .u = 0.0F, .v = 1.0F, .color = color};
    const ParticleVertex upRight{
        .position = particle.origin + right + up, .u = 0.0F, .v = 0.0F, .color = color};
    const ParticleVertex downRight{
        .position = particle.origin + right - up, .u = 1.0F, .v = 0.0F, .color = color};
    const ParticleVertex downLeft{
        .position = particle.origin - right - up, .u = 1.0F, .v = 1.0F, .color = color};
    vertices.insert(vertices.end(), {upLeft, upRight, downRight, upLeft, downRight, downLeft});
}

/// Appends the quad of `streak` seen through `toClip`, a view looking along `view`, on a target of
/// `aspect` (CL_DrawTracers): along its line, twice its half width across the line it makes on the
/// screen. Whether it was appended: not behind the view, nor seen end on.
[[nodiscard]] auto addStreak(std::vector<ParticleVertex>& vertices, const StreakInstance& streak,
                             const Mat4& toClip, float aspect, const AngleVectors& view) -> bool {
    const Vec4 start = toClip * extend(streak.start, 1.0F);
    const Vec4 end = toClip * extend(streak.end, 1.0F);
    if (start.w <= 0.0F || end.w <= 0.0F) {
        return false;
    }
    // The line on the screen, its across measured in its height, as its pixels are.
    const float across = ((start.x / start.w) - (end.x / end.w)) * aspect;
    const float upward = (start.y / start.w) - (end.y / end.w);
    const float length = std::hypot(across, upward);
    if (!(length > 0.0F)) {
        return false;
    }
    const Vec3 side = ((view.right * -upward) + (view.up * across)) * (streak.halfWidth.value() / length);
    const Vec4 color = vertexColorOf(streak.color, streak.amount);
    // The original's corners and texture coordinates: the sprite across the line, 0.8 of it along it.
    const ParticleVertex startLeft{.position = streak.start - side, .u = 0.0F, .v = 0.0F, .color = color};
    const ParticleVertex startRight{.position = streak.start + side, .u = 1.0F, .v = 0.0F, .color = color};
    const ParticleVertex endLeft{
        .position = streak.end - side, .u = 0.0F, .v = kStreakImageLength, .color = color};
    const ParticleVertex endRight{
        .position = streak.end + side, .u = 1.0F, .v = kStreakImageLength, .color = color};
    vertices.insert(vertices.end(), {endLeft, endRight, startRight, endLeft, startRight, startLeft});
    return true;
}

} // namespace

auto ParticleRenderer::create(rhi::Device& device, rhi::CommandList& commands,
                              const ParticleRendererDesc& desc) -> Result<ParticleRenderer, ParticleError> {
    ParticleRenderer renderer{device};
    renderer.m_encoding = desc.encoding;
    renderer.m_textureGamma = textureGammaTable(desc.gamma);
    if (auto made = renderer.createPipeline(desc.shaderCode, desc.colorFormat); !made) {
        return fail(ParticleError::GpuFailed, std::move(made.error()));
    }
    if (auto made = renderer.createDot(commands); !made) {
        return fail(ParticleError::GpuFailed, std::move(made.error()));
    }
    renderer.m_frameSlots.reserve(rhi::kMaxFramesInFlight);
    for (std::uint32_t slot = 0; slot < rhi::kMaxFramesInFlight; ++slot) {
        if (auto reserved = renderer.reserve(renderer.m_frameSlots.emplace_back(), kFirstQuadCapacity);
            !reserved) {
            return fail(ParticleError::GpuFailed, std::move(reserved.error()));
        }
    }
    return renderer;
}

auto ParticleRenderer::createPipeline(std::span<const std::byte> shaderCode, rhi::Format colorFormat)
    -> Result<void, rhi::RhiError> {
    rhi::Device& device = *m_device;
    constexpr Flags<rhi::ShaderStage> kBothStages{rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment};
    constexpr Flags<rhi::ShaderStage> kFragment{rhi::ShaderStage::Fragment};
    const std::array frameBindings{rhi::BindingLayout{
        .binding = kFrameUniformBinding, .type = rhi::BindingType::UniformBuffer, .stages = kBothStages}};
    const std::array imageBindings{
        rhi::BindingLayout{
            .binding = kImageBinding, .type = rhi::BindingType::SampledTexture, .stages = kFragment},
        rhi::BindingLayout{
            .binding = kImageSamplerBinding, .type = rhi::BindingType::Sampler, .stages = kFragment}};
    auto shader = device.createShader({.label = "particle", .code = shaderCode});
    auto frameLayout = device.createBindGroupLayout({.label = "particle.frame", .bindings = frameBindings});
    auto imageLayout = device.createBindGroupLayout({.label = "particle.dot", .bindings = imageBindings});
    // TF_CLAMP: the dot's edge does not wrap around into its corner.
    auto sampler = device.createSampler({.label = "particle.dot", .address = rhi::AddressMode::ClampToEdge});
    if (!shader || !frameLayout || !imageLayout || !sampler) {
        return std::unexpected(!shader        ? std::move(shader.error())
                               : !frameLayout ? std::move(frameLayout.error())
                               : !imageLayout ? std::move(imageLayout.error())
                                              : std::move(sampler.error()));
    }
    m_shader = std::move(*shader);
    m_frameLayout = std::move(*frameLayout);
    m_imageLayout = std::move(*imageLayout);
    m_sampler = std::move(*sampler);

    // As CL_DrawParticles and CL_DrawTracers set them: a particle blended over what is there, a streak
    // added to it, the depth tested but not written; each quad is turned to the view, so none is culled.
    const std::array vertexBuffers{
        rhi::VertexBufferLayout{.stride = sizeof(ParticleVertex), .attributes = kParticleVertexAttributes}};
    const std::array layouts{m_frameLayout.get(), m_imageLayout.get()};
    const auto pipeline = [&](std::string_view label, std::string_view fragment,
                              rhi::BlendFactor destination) {
        const std::array targets{
            rhi::ColorTarget{.format = colorFormat,
                             .blend = rhi::BlendState{.sourceColor = rhi::BlendFactor::SourceAlpha,
                                                      .destinationColor = destination,
                                                      .sourceAlpha = rhi::BlendFactor::SourceAlpha,
                                                      .destinationAlpha = destination}}};
        return device.createRenderPipeline(
            {.label = label,
             .vertex = {.shader = m_shader.get(), .entryPoint = "vertexMain"},
             .fragment = {.shader = m_shader.get(), .entryPoint = fragment},
             .vertexBuffers = vertexBuffers,
             .cullMode = rhi::CullMode::None,
             .frontFace = rhi::FrontFace::CounterClockwise,
             .colorTargets = targets,
             .depth = rhi::DepthState{.isWriteEnabled = false, .compare = rhi::CompareOp::Greater},
             .bindGroupLayouts = layouts});
    };
    auto blended = pipeline("particle", "fragmentMain", rhi::BlendFactor::OneMinusSourceAlpha);
    // The streaks are added, through no fog, as the original's fog leaves what is added (nx.render:fog).
    auto added = pipeline("particle.streak", "addedFragmentMain", rhi::BlendFactor::One);
    if (!blended || !added) {
        return std::unexpected{!blended ? std::move(blended.error()) : std::move(added.error())};
    }
    m_pipeline = std::move(*blended);
    streakPipeline = std::move(*added);
    return {};
}

auto ParticleRenderer::createDot(rhi::CommandList& commands) -> Result<void, rhi::RhiError> {
    Uploader uploader{*m_device, commands};
    const std::vector<std::byte> texels = dotTexels();
    const std::array<std::size_t, 1> offsets{0};
    auto texture =
        uploader.texture("particle.dot", {.width = kDotSide, .height = kDotSide}, 1, 1, texels, offsets);
    if (!texture) {
        return std::unexpected{std::move(texture.error())};
    }
    const std::array bindings{rhi::Binding{.binding = kImageBinding, .resource = texture->get()},
                              rhi::Binding{.binding = kImageSamplerBinding, .resource = m_sampler.get()}};
    auto group = m_device->createBindGroup(
        {.label = "particle.dot", .layout = m_imageLayout.get(), .bindings = bindings});
    if (!group) {
        return std::unexpected{std::move(group.error())};
    }
    m_dot = std::move(*texture);
    m_dotGroup = std::move(*group);
    return {};
}

auto ParticleRenderer::loadStreakSprite(rhi::CommandList& commands, const assets::SpriteView& sprite)
    -> Result<void, ParticleError> {
    if (sprite.frames.empty() || sprite.frames.front().images.empty()) {
        return {};
    }
    const assets::SpriteImage& image = sprite.frames.front().images.front();
    Uploader uploader{*m_device, commands};
    const MipChain texels =
        colorMipChainOf(spriteTexels(sprite, image, m_textureGamma), image.width, image.height, m_encoding);
    auto texture = uploader.texture("particle.streak", {.width = image.width, .height = image.height},
                                    narrow<std::uint32_t>(texels.offsets.size()), 1, texels.texels,
                                    texels.offsets, colorFormatOf(m_encoding));
    if (!texture) {
        return fail(ParticleError::GpuFailed, std::move(texture.error()));
    }
    const std::array bindings{rhi::Binding{.binding = kImageBinding, .resource = texture->get()},
                              rhi::Binding{.binding = kImageSamplerBinding, .resource = m_sampler.get()}};
    auto group = m_device->createBindGroup(
        {.label = "particle.streak", .layout = m_imageLayout.get(), .bindings = bindings});
    if (!group) {
        return fail(ParticleError::GpuFailed, std::move(group.error()));
    }
    streakImage = std::move(*texture);
    streakGroup = std::move(*group);
    return {};
}

auto ParticleRenderer::reserve(ParticleFrameSlot& slot, std::size_t quads) -> Result<void, rhi::RhiError> {
    rhi::Device& device = *m_device;
    if (slot.frame.get().isNull()) {
        auto frame = device.createBuffer({.label = "particle.frame",
                                          .size = sizeof(FrameUniform),
                                          .usage = {rhi::BufferUsage::Uniform},
                                          .location = rhi::MemoryLocation::Upload});
        if (!frame) {
            return std::unexpected{std::move(frame.error())};
        }
        const std::array bindings{rhi::Binding{.binding = kFrameUniformBinding,
                                               .resource = rhi::BufferBinding{.buffer = frame->get()}}};
        auto group = device.createBindGroup(
            {.label = "particle.frame", .layout = m_frameLayout.get(), .bindings = bindings});
        if (!group) {
            return std::unexpected{std::move(group.error())};
        }
        slot.frame = std::move(*frame);
        slot.group = std::move(*group);
    }
    if (quads > slot.quadCapacity) {
        const std::size_t capacity = std::max({quads, slot.quadCapacity * 2, kFirstQuadCapacity});
        auto vertices = device.createBuffer({.label = "particle.vertices",
                                             .size = capacity * kQuadVertices * sizeof(ParticleVertex),
                                             .usage = {rhi::BufferUsage::Vertex},
                                             .location = rhi::MemoryLocation::Upload});
        if (!vertices) {
            return std::unexpected{std::move(vertices.error())};
        }
        slot.vertices = std::move(*vertices);
        slot.quadCapacity = capacity;
    }
    return {};
}

auto ParticleRenderer::prepare(const ParticleDraw& draw) -> Result<void, ParticleError> {
    m_preparedQuads = 0;
    preparedStreaks = 0;
    // A ring of kMaxFramesInFlight: the slot written now was last read frames in flight ago.
    ParticleFrameSlot& slot = m_frameSlots[m_drawCount++ % m_frameSlots.size()];
    const FrameUniform uniform{.viewProjection = viewProjectionMatrix(draw.camera, draw.aspect),
                               .fog = fogBlockOf(draw.fog, draw.camera, m_encoding),
                               .colorEncoding = std::to_underlying(m_encoding)};
    std::ranges::copy(std::as_bytes(std::span{&uniform, 1}), m_device->mappedBytes(slot.frame.get()).begin());
    const std::span<const StreakInstance> streaks =
        streakGroup.get().isNull() ? std::span<const StreakInstance>{} : draw.streaks;
    if (draw.particles.empty() && streaks.empty()) {
        return {};
    }
    const AngleVectors view = angleVectors(draw.camera.angles);
    std::vector<ParticleVertex> vertices;
    vertices.reserve((draw.particles.size() + streaks.size()) * kQuadVertices);
    for (const ParticleInstance& particle : draw.particles) {
        addQuad(vertices, particle, draw.camera.position, view);
    }
    const Mat4 toClip = viewProjectionMatrix(draw.camera, draw.aspect);
    const auto streakCount =
        static_cast<std::size_t>(std::ranges::count_if(streaks, [&](const StreakInstance& streak) {
            return addStreak(vertices, streak, toClip, draw.aspect, view);
        }));
    const std::size_t quads = draw.particles.size() + streakCount;
    if (auto reserved = reserve(slot, quads); !reserved) {
        return fail(ParticleError::GpuFailed, std::move(reserved.error()));
    }
    std::ranges::copy(std::as_bytes(std::span{vertices}), m_device->mappedBytes(slot.vertices.get()).begin());
    m_preparedQuads = narrow<std::uint32_t>(draw.particles.size());
    preparedStreaks = narrow<std::uint32_t>(streakCount);
    return {};
}

void ParticleRenderer::drawPrepared(rhi::CommandList& commands) const {
    if (m_preparedQuads == 0 && preparedStreaks == 0) {
        return;
    }
    // The particles, then the streaks after them (CL_DrawEFX).
    const ParticleFrameSlot& slot = m_frameSlots[(m_drawCount - 1) % m_frameSlots.size()];
    commands.setVertexBuffer(0, slot.vertices.get(), 0);
    if (m_preparedQuads > 0) {
        commands.setPipeline(m_pipeline.get());
        commands.setBindGroup(kFrameGroup, slot.group.get());
        commands.setBindGroup(kImageGroup, m_dotGroup.get());
        commands.draw(m_preparedQuads * kQuadVertices, 1, 0, 0);
    }
    if (preparedStreaks > 0) {
        commands.setPipeline(streakPipeline.get());
        commands.setBindGroup(kFrameGroup, slot.group.get());
        commands.setBindGroup(kImageGroup, streakGroup.get());
        commands.draw(preparedStreaks * kQuadVertices, 1, m_preparedQuads * kQuadVertices, 0);
    }
}

auto ParticleRenderer::draw(rhi::CommandList& commands, const ParticleDraw& draw)
    -> Result<void, ParticleError> {
    if (auto prepared = prepare(draw); !prepared) {
        return std::unexpected{std::move(prepared.error())};
    }
    drawPrepared(commands);
    return {};
}

} // namespace nx::render
