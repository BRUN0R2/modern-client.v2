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

// The bind groups of the studio pipelines, their bindings and the vertex attributes, as
// shaders/studio.slang declares them.
constexpr std::uint32_t kFrameGroup = 0;
constexpr std::uint32_t kSkinGroup = 1;
constexpr std::uint32_t kInstanceGroup = 2;
constexpr std::uint32_t kFrameUniformBinding = 0;
constexpr std::uint32_t kLightGammaBinding = 1;
constexpr std::uint32_t kBonesBinding = 2;
constexpr std::uint32_t kAlbedoBinding = 0;
constexpr std::uint32_t kAlbedoSamplerBinding = 1;
constexpr std::uint32_t kSkinUniformBinding = 2;
constexpr std::uint32_t kInstanceUniformBinding = 0;

// Skin::flags, as the shader reads them.
constexpr std::uint32_t kFlatShade = 1;
constexpr std::uint32_t kChrome = 2;
constexpr std::uint32_t kFullbright = 4;

/// The palette index a masked texture draws as transparent.
constexpr std::uint8_t kTransparentIndex = 255;
/// The bytes of a palette color, red, green and blue; of a texel, red, green, blue and alpha.
constexpr std::size_t kRgbBytes = 3;
constexpr std::size_t kRgbaBytes = 4;
/// A channel at its brightest, and an opaque alpha.
constexpr std::byte kFull{255};
/// The texels a texel of the next mip level is the mean of, two by two, and the bits that divide by
/// them.
constexpr std::uint32_t kMipStep = 2;
constexpr std::uint32_t kMipMeanShift = 2;

/// The depths of the view's own model: the nearest share of them (the original's
/// VIEWMODEL_DEPTH_RANGE), reversed depth being 1 at the near plane.
constexpr float kViewModelDepthShare = 0.3F;

/// The room a frame starts with, which grows as the frames need: bones, and models.
constexpr std::size_t kFirstBoneCapacity = 256;
constexpr std::size_t kFirstInstanceCapacity = 16;

/// A corner of a triangle of a mesh: where it is and its normal, each in the space of its own bone,
/// and its texture coordinates, in texels.
struct StudioVertex {
    Vec3 position{};
    Vec3 normal{};
    float s = 0.0F;
    float t = 0.0F;
    std::uint32_t positionBone = 0;
    std::uint32_t normalBone = 0;
};
/// The 4-byte words of a vertex: the position's three, the normal's three, the texture's two and the
/// bones' two.
constexpr std::size_t kStudioVertexWords = 10;
static_assert(sizeof(StudioVertex) == kStudioVertexWords * sizeof(float),
              "the vertex layout of the studio pipelines");

constexpr std::array kStudioVertexAttributes{
    rhi::VertexAttribute{.location = 0, .format = rhi::VertexFormat::Float3, .offset = 0},
    rhi::VertexAttribute{.location = 1,
                         .format = rhi::VertexFormat::Float3,
                         .offset = offsetof(StudioVertex, normal)}, // NOLINT
    rhi::VertexAttribute{
        .location = 2, .format = rhi::VertexFormat::Float2, .offset = offsetof(StudioVertex, s)}, // NOLINT
    rhi::VertexAttribute{.location = 3,
                         .format = rhi::VertexFormat::Uint32,
                         .offset = offsetof(StudioVertex, positionBone)}, // NOLINT
    rhi::VertexAttribute{.location = 4,
                         .format = rhi::VertexFormat::Uint32,
                         .offset = offsetof(StudioVertex, normalBone)}}; // NOLINT

/// The frame's uniform: the rows of the view-projection matrix, where the view is and its right.
struct FrameUniform {
    Mat4 viewProjection{};
    Vec4 eye{};
    Vec4 right{};
};

/// A texture's uniform: how its texels map to coordinates, and how it is drawn.
struct SkinUniform {
    float texelWidth = 0.0F; ///< 1 / its width.
    float texelHeight = 0.0F;
    std::uint32_t flags = 0;
    std::uint32_t reserved = 0;
};

/// A model's uniform: its light, how its depths are mapped, and where its bones start.
struct InstanceUniform {
    Vec4 lightDirection{};
    Vec4 lightColor{};
    float ambient = 0.0F;
    float shade = 0.0F;
    float depthScale = 1.0F; ///< Clip depth becomes depth * depthScale + w * depthOffset.
    float depthOffset = 0.0F;
    std::uint32_t firstBone = 0;
    std::array<std::uint32_t, 3> reserved{};
};

/// Each model's uniform is this far from the last in a frame's buffer of them: the largest offset
/// alignment of uniform buffers a GPU asks for.
constexpr std::uint64_t kInstanceUniformStride = 256;
static_assert(sizeof(InstanceUniform) <= kInstanceUniformStride);

/// The triangles of every mesh of a model, and the meshes of each choice of each body part.
struct Geometry {
    std::vector<StudioVertex> vertices{};
    std::vector<std::uint32_t> indices{};
    std::vector<std::vector<std::vector<StudioMeshDraw>>> bodyParts{};
};

/// Adds the triangles of the `cornerCount` corners of a run of `winding`, its first at `first`, wound as
/// the original's GL draws them: a strip's every other triangle turned back.
void addTriangles(assets::Winding winding, std::uint32_t first, std::uint32_t cornerCount,
                  std::vector<std::uint32_t>& indices) {
    for (std::uint32_t corner = 2; corner < cornerCount; ++corner) {
        const std::uint32_t triangle = corner - 2;
        if (winding == assets::Winding::Fan) {
            indices.insert(indices.end(), {first, first + corner - 1, first + corner});
        } else if (triangle % 2 == 0) {
            indices.insert(indices.end(), {first + triangle, first + triangle + 1, first + corner});
        } else {
            indices.insert(indices.end(), {first + triangle + 1, first + triangle, first + corner});
        }
    }
}

[[nodiscard]] auto geometryOf(const assets::MdlView& model) -> Geometry {
    Geometry geometry;
    for (const assets::BodyPart& part : model.bodyParts()) {
        auto& choices = geometry.bodyParts.emplace_back();
        for (const assets::StudioModel& choice : part.models) {
            auto& meshes = choices.emplace_back();
            for (const assets::StudioMesh& mesh : choice.meshes) {
                StudioMeshDraw draw{.firstIndex = narrow<std::uint32_t>(geometry.indices.size()),
                                    .skinReference = mesh.skinReference};
                for (const assets::TriangleRun& run : mesh.runs) {
                    const auto first = narrow<std::uint32_t>(geometry.vertices.size());
                    for (const assets::MeshCorner& corner : run.corners) {
                        const assets::BoundVector& position = choice.vertices[corner.vertex.value];
                        const assets::BoundVector& normal = choice.normals[corner.normal.value];
                        geometry.vertices.push_back(StudioVertex{.position = position.vector,
                                                                 .normal = normal.vector,
                                                                 .s = static_cast<float>(corner.s),
                                                                 .t = static_cast<float>(corner.t),
                                                                 .positionBone = position.bone.value,
                                                                 .normalBone = normal.bone.value});
                    }
                    addTriangles(run.winding, first, narrow<std::uint32_t>(run.corners.size()),
                                 geometry.indices);
                }
                draw.indexCount = narrow<std::uint32_t>(geometry.indices.size()) - draw.firstIndex;
                meshes.push_back(draw);
            }
        }
    }
    return geometry;
}

/// The texels of a texture's levels, and where each starts.
struct SkinTexels {
    std::vector<std::byte> bytes{};
    std::vector<std::size_t> offsets{};
};

/// The level below the `width` x `height` one at `from` of `texels`, appended to them: each texel the
/// mean of the two by two above it, cut as the original's GL_MipMap cuts it, the last row or column
/// of an odd size taken twice.
void addMipLevel(SkinTexels& texels, std::size_t from, std::uint32_t width, std::uint32_t height) {
    const std::uint32_t lowerWidth = std::max(width / kMipStep, 1U);
    const std::uint32_t lowerHeight = std::max(height / kMipStep, 1U);
    texels.offsets.push_back(texels.bytes.size());
    const auto at = [&](std::uint32_t x, std::uint32_t y, std::size_t channel) {
        const std::size_t texel = (std::size_t{std::min(y, height - 1)} * width) + std::min(x, width - 1);
        return std::to_integer<std::uint32_t>(texels.bytes[from + (texel * kRgbaBytes) + channel]);
    };
    for (std::uint32_t y = 0; y < lowerHeight; ++y) {
        for (std::uint32_t x = 0; x < lowerWidth; ++x) {
            for (std::size_t channel = 0; channel < kRgbaBytes; ++channel) {
                const std::uint32_t sum = at(x * kMipStep, y * kMipStep, channel) +
                                          at((x * kMipStep) + 1, y * kMipStep, channel) +
                                          at(x * kMipStep, (y * kMipStep) + 1, channel) +
                                          at((x * kMipStep) + 1, (y * kMipStep) + 1, channel);
                texels.bytes.push_back(std::byte{narrow<std::uint8_t>(sum >> kMipMeanShift)});
            }
        }
    }
}

/// The texels of `texture` through `textureGamma`, a masked one's palette index 255 transparent, and
/// its mip levels down to one texel, unless it has none.
[[nodiscard]] auto skinTexelsOf(const assets::StudioTexture& texture, const ChannelTable& textureGamma)
    -> SkinTexels {
    const bool isMasked = texture.flags.test(assets::StudioTextureFlag::Masked);
    SkinTexels texels{.offsets = {0}};
    texels.bytes.reserve(texture.pixels.size() * kRgbaBytes * 2);
    for (const std::byte index : texture.pixels) {
        const auto entry = std::to_integer<std::size_t>(index);
        const bool isTransparent = isMasked && entry == kTransparentIndex;
        for (std::size_t channel = 0; channel < kRgbBytes; ++channel) {
            const auto value = std::to_integer<std::size_t>(texture.palette[(entry * kRgbBytes) + channel]);
            texels.bytes.push_back(isTransparent ? std::byte{0} : std::byte{textureGamma[value]});
        }
        texels.bytes.push_back(isTransparent ? std::byte{0} : kFull);
    }
    if (!texture.flags.test(assets::StudioTextureFlag::NoMipmaps)) {
        const auto levels =
            static_cast<std::uint32_t>(std::bit_width(std::max(texture.width, texture.height)));
        for (std::uint32_t level = 1; level < levels; ++level) {
            addMipLevel(texels, texels.offsets.back(), std::max(texture.width >> (level - 1), 1U),
                        std::max(texture.height >> (level - 1), 1U));
        }
    }
    return texels;
}

[[nodiscard]] auto meshKindOf(const assets::StudioTexture& texture) noexcept -> MeshKind {
    if (texture.flags.test(assets::StudioTextureFlag::Masked)) {
        return MeshKind::Masked;
    }
    return texture.flags.test(assets::StudioTextureFlag::Additive) ? MeshKind::Additive : MeshKind::Opaque;
}

[[nodiscard]] auto skinUniformOf(const assets::StudioTexture& texture) noexcept -> SkinUniform {
    SkinUniform uniform{.texelWidth = 1.0F / static_cast<float>(texture.width),
                        .texelHeight = 1.0F / static_cast<float>(texture.height)};
    for (const auto& [flag, bit] : {std::pair{assets::StudioTextureFlag::FlatShade, kFlatShade},
                                    std::pair{assets::StudioTextureFlag::Chrome, kChrome},
                                    std::pair{assets::StudioTextureFlag::Fullbright, kFullbright}}) {
        if (texture.flags.test(flag)) {
            uniform.flags |= bit;
        }
    }
    return uniform;
}

/// The uniform of `instance`, its bones from `firstBone` on.
[[nodiscard]] auto instanceUniformOf(const StudioInstance& instance, std::size_t firstBone)
    -> InstanceUniform {
    InstanceUniform uniform{.lightDirection = extend(instance.light.direction, 0.0F),
                            .lightColor = extend(instance.light.color, 0.0F),
                            .ambient = instance.light.ambient,
                            .shade = instance.light.shade,
                            .firstBone = narrow<std::uint32_t>(firstBone)};
    if (instance.depth == StudioDepth::ViewModel) {
        uniform.depthScale = kViewModelDepthShare;
        uniform.depthOffset = 1.0F - kViewModelDepthShare;
    }
    return uniform;
}

} // namespace

auto StudioRenderer::create(rhi::Device& device, rhi::CommandList& commands, const StudioRendererDesc& desc)
    -> Result<StudioRenderer, StudioError> {
    StudioRenderer renderer{device};
    renderer.m_textureGamma = textureGammaTable(desc.gamma);
    std::vector<std::uint32_t> curve;
    std::ranges::copy(lightGammaTable(desc.gamma), std::back_inserter(curve));
    Uploader uploader{device, commands};
    auto lightGamma = uploader.buffer("studio.lightGamma", rhi::BufferUsage::Storage,
                                      rhi::BufferState::ShaderRead, std::as_bytes(std::span{curve}));
    if (!lightGamma) {
        return fail(StudioError::GpuFailed, std::move(lightGamma.error()));
    }
    renderer.m_lightGamma = std::move(*lightGamma);
    if (auto made = renderer.createPipelines(desc.shaderCode, desc.colorFormat); !made) {
        return fail(StudioError::GpuFailed, std::move(made.error()));
    }
    renderer.m_frameSlots.reserve(rhi::kMaxFramesInFlight);
    for (std::uint32_t slot = 0; slot < rhi::kMaxFramesInFlight; ++slot) {
        if (auto reserved = renderer.reserve(renderer.m_frameSlots.emplace_back(), kFirstBoneCapacity,
                                             kFirstInstanceCapacity);
            !reserved) {
            return fail(StudioError::GpuFailed, std::move(reserved.error()));
        }
    }
    return renderer;
}

auto StudioRenderer::createPipelines(std::span<const std::byte> shaderCode, rhi::Format colorFormat)
    -> Result<void, rhi::RhiError> {
    rhi::Device& device = *m_device;
    constexpr Flags<rhi::ShaderStage> kVertex{rhi::ShaderStage::Vertex};
    constexpr Flags<rhi::ShaderStage> kFragment{rhi::ShaderStage::Fragment};
    constexpr Flags<rhi::ShaderStage> kBothStages{rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment};
    const std::array frameBindings{
        rhi::BindingLayout{
            .binding = kFrameUniformBinding, .type = rhi::BindingType::UniformBuffer, .stages = kVertex},
        rhi::BindingLayout{
            .binding = kLightGammaBinding, .type = rhi::BindingType::StorageBuffer, .stages = kVertex},
        rhi::BindingLayout{
            .binding = kBonesBinding, .type = rhi::BindingType::StorageBuffer, .stages = kVertex}};
    const std::array skinBindings{
        rhi::BindingLayout{
            .binding = kAlbedoBinding, .type = rhi::BindingType::SampledTexture, .stages = kFragment},
        rhi::BindingLayout{
            .binding = kAlbedoSamplerBinding, .type = rhi::BindingType::Sampler, .stages = kFragment},
        rhi::BindingLayout{
            .binding = kSkinUniformBinding, .type = rhi::BindingType::UniformBuffer, .stages = kBothStages}};
    const std::array instanceBindings{rhi::BindingLayout{
        .binding = kInstanceUniformBinding, .type = rhi::BindingType::UniformBuffer, .stages = kVertex}};
    auto shader = device.createShader({.label = "studio", .code = shaderCode});
    auto frameLayout = device.createBindGroupLayout({.label = "studio.frame", .bindings = frameBindings});
    auto skinLayout = device.createBindGroupLayout({.label = "studio.skin", .bindings = skinBindings});
    auto instanceLayout =
        device.createBindGroupLayout({.label = "studio.instance", .bindings = instanceBindings});
    auto sampler = device.createSampler({.label = "studio.skin"});
    if (!shader || !frameLayout || !skinLayout || !instanceLayout || !sampler) {
        return std::unexpected(!shader           ? std::move(shader.error())
                               : !frameLayout    ? std::move(frameLayout.error())
                               : !skinLayout     ? std::move(skinLayout.error())
                               : !instanceLayout ? std::move(instanceLayout.error())
                                                 : std::move(sampler.error()));
    }
    m_shader = std::move(*shader);
    m_frameLayout = std::move(*frameLayout);
    m_skinLayout = std::move(*skinLayout);
    m_instanceLayout = std::move(*instanceLayout);
    m_sampler = std::move(*sampler);

    // The original culls the triangles that face the viewer in GL's sense (GL_FRONT): a model's
    // triangles face away from it. An additive mesh adds itself, its depth tested but not written.
    const std::array vertexBuffers{
        rhi::VertexBufferLayout{.stride = sizeof(StudioVertex), .attributes = kStudioVertexAttributes}};
    const std::array layouts{m_frameLayout.get(), m_skinLayout.get(), m_instanceLayout.get()};
    const rhi::BlendState additiveBlend{.sourceColor = rhi::BlendFactor::One,
                                        .destinationColor = rhi::BlendFactor::One,
                                        .sourceAlpha = rhi::BlendFactor::One,
                                        .destinationAlpha = rhi::BlendFactor::One};
    const auto pipeline = [&](std::string_view label, std::string_view fragment,
                              std::optional<rhi::BlendState> blend) {
        const std::array targets{rhi::ColorTarget{.format = colorFormat, .blend = blend}};
        return device.createRenderPipeline(
            {.label = label,
             .vertex = {.shader = m_shader.get(), .entryPoint = "vertexMain"},
             .fragment = {.shader = m_shader.get(), .entryPoint = fragment},
             .vertexBuffers = vertexBuffers,
             .cullMode = rhi::CullMode::Front,
             .frontFace = rhi::FrontFace::CounterClockwise,
             .colorTargets = targets,
             .depth = rhi::DepthState{.isWriteEnabled = !blend, .compare = rhi::CompareOp::Greater},
             .bindGroupLayouts = layouts});
    };
    auto opaque = pipeline("studio", "fragmentMain", std::nullopt);
    auto masked = pipeline("studio.masked", "maskedFragmentMain", std::nullopt);
    auto additive = pipeline("studio.additive", "fragmentMain", additiveBlend);
    for (auto* const created : {&opaque, &masked, &additive}) {
        if (!*created) {
            return std::unexpected{std::move(created->error())};
        }
    }
    m_opaquePipeline = std::move(*opaque);
    m_maskedPipeline = std::move(*masked);
    m_additivePipeline = std::move(*additive);
    return {};
}

auto StudioRenderer::reserve(StudioFrameSlot& slot, std::size_t bones, std::size_t instances)
    -> Result<void, rhi::RhiError> {
    rhi::Device& device = *m_device;
    if (slot.frame.get().isNull()) {
        auto frame = device.createBuffer({.label = "studio.frame",
                                          .size = sizeof(FrameUniform),
                                          .usage = {rhi::BufferUsage::Uniform},
                                          .location = rhi::MemoryLocation::Upload});
        if (!frame) {
            return std::unexpected{std::move(frame.error())};
        }
        slot.frame = std::move(*frame);
    }
    if (bones > slot.boneCapacity || slot.group.get().isNull()) {
        const std::size_t capacity = std::max({bones, slot.boneCapacity * 2, kFirstBoneCapacity});
        auto buffer = device.createBuffer({.label = "studio.bones",
                                           .size = capacity * sizeof(Mat3x4),
                                           .usage = {rhi::BufferUsage::Storage},
                                           .location = rhi::MemoryLocation::Upload});
        if (!buffer) {
            return std::unexpected{std::move(buffer.error())};
        }
        const std::array bindings{
            rhi::Binding{.binding = kFrameUniformBinding,
                         .resource = rhi::BufferBinding{.buffer = slot.frame.get()}},
            rhi::Binding{.binding = kLightGammaBinding,
                         .resource = rhi::BufferBinding{.buffer = m_lightGamma.get()}},
            rhi::Binding{.binding = kBonesBinding, .resource = rhi::BufferBinding{.buffer = buffer->get()}}};
        auto group = device.createBindGroup(
            {.label = "studio.frame", .layout = m_frameLayout.get(), .bindings = bindings});
        if (!group) {
            return std::unexpected{std::move(group.error())};
        }
        slot.bones = std::move(*buffer);
        slot.boneCapacity = capacity;
        slot.group = std::move(*group);
    }
    if (instances > slot.instanceGroups.size()) {
        const std::size_t capacity =
            std::max({instances, slot.instanceGroups.size() * 2, kFirstInstanceCapacity});
        auto buffer = device.createBuffer({.label = "studio.instances",
                                           .size = capacity * kInstanceUniformStride,
                                           .usage = {rhi::BufferUsage::Uniform},
                                           .location = rhi::MemoryLocation::Upload});
        if (!buffer) {
            return std::unexpected{std::move(buffer.error())};
        }
        std::vector<rhi::OwnedBindGroup> groups;
        for (std::size_t instance = 0; instance < capacity; ++instance) {
            const std::array bindings{
                rhi::Binding{.binding = kInstanceUniformBinding,
                             .resource = rhi::BufferBinding{.buffer = buffer->get(),
                                                            .offset = kInstanceUniformStride * instance,
                                                            .size = sizeof(InstanceUniform)}}};
            auto group = device.createBindGroup(
                {.label = "studio.instance", .layout = m_instanceLayout.get(), .bindings = bindings});
            if (!group) {
                return std::unexpected{std::move(group.error())};
            }
            groups.push_back(std::move(*group));
        }
        slot.instances = std::move(*buffer);
        slot.instanceGroups = std::move(groups);
    }
    return {};
}

auto StudioRenderer::load(rhi::CommandList& commands, const StudioModelDesc& desc)
    -> Result<StudioModel, StudioError> {
    if (auto checked = desc.model.checkTextures(desc.textures); !checked) {
        return fail(StudioError::BadTextures, std::format("{:?}", desc.model.name()),
                    std::move(checked.error()));
    }
    const auto gpuFailure = [](Failure<rhi::RhiError> failure) {
        return fail(StudioError::GpuFailed, std::move(failure));
    };
    Uploader uploader{*m_device, commands};
    StudioModel model;
    Geometry geometry = geometryOf(desc.model);
    auto vertices = uploader.buffer("studio.vertices", rhi::BufferUsage::Vertex, rhi::BufferState::Vertex,
                                    std::as_bytes(std::span{geometry.vertices}));
    auto indices = uploader.buffer("studio.indices", rhi::BufferUsage::Index, rhi::BufferState::Index,
                                   std::as_bytes(std::span{geometry.indices}));
    if (!vertices || !indices) {
        return gpuFailure(std::move(!vertices ? vertices.error() : indices.error()));
    }
    model.m_vertices = std::move(*vertices);
    model.m_indices = std::move(*indices);
    model.m_bodyParts = std::move(geometry.bodyParts);
    model.m_boneCount = desc.model.bones().size();
    model.m_skins.assign(desc.textures.skins().begin(), desc.textures.skins().end());
    for (const assets::StudioTexture& texture : desc.textures.textures()) {
        const SkinTexels texels = skinTexelsOf(texture, m_textureGamma);
        const std::string label = std::format("studio.{}", texture.name);
        auto uploaded =
            uploader.texture(label, {.width = texture.width, .height = texture.height},
                             narrow<std::uint32_t>(texels.offsets.size()), 1, texels.bytes, texels.offsets);
        const SkinUniform skin = skinUniformOf(texture);
        auto uniform = uploader.buffer(label, rhi::BufferUsage::Uniform, rhi::BufferState::Uniform,
                                       std::as_bytes(std::span{&skin, 1}));
        if (!uploaded || !uniform) {
            return gpuFailure(std::move(!uploaded ? uploaded.error() : uniform.error()));
        }
        const std::array bindings{rhi::Binding{.binding = kAlbedoBinding, .resource = uploaded->get()},
                                  rhi::Binding{.binding = kAlbedoSamplerBinding, .resource = m_sampler.get()},
                                  rhi::Binding{.binding = kSkinUniformBinding,
                                               .resource = rhi::BufferBinding{.buffer = uniform->get()}}};
        auto group =
            m_device->createBindGroup({.label = label, .layout = m_skinLayout.get(), .bindings = bindings});
        if (!group) {
            return gpuFailure(std::move(group.error()));
        }
        model.m_textures.push_back(StudioSkinDraw{.texture = std::move(*uploaded),
                                                  .uniform = std::move(*uniform),
                                                  .group = std::move(*group),
                                                  .kind = meshKindOf(texture)});
    }
    return model;
}

auto StudioRenderer::pipelineOf(MeshKind kind) const noexcept -> rhi::RenderPipelineHandle {
    switch (kind) {
    case MeshKind::Masked:
        return m_maskedPipeline.get();
    case MeshKind::Additive:
        return m_additivePipeline.get();
    case MeshKind::Opaque:
        break;
    }
    return m_opaquePipeline.get();
}

auto StudioRenderer::draw(rhi::CommandList& commands, const StudioFrame& frame) -> Result<void, StudioError> {
    // A ring of kMaxFramesInFlight: the slot written now was last read frames in flight ago.
    StudioFrameSlot& slot = m_frameSlots[m_drawCount++ % m_frameSlots.size()];
    std::size_t boneCount = 0;
    for (const StudioInstance& instance : frame.instances) {
        expects(instance.bones.size() == instance.model.boneCount(),
                "StudioRenderer::draw: every bone of a model");
        boneCount += instance.bones.size();
    }
    if (auto reserved = reserve(slot, boneCount, frame.instances.size()); !reserved) {
        return fail(StudioError::GpuFailed, std::move(reserved.error()));
    }
    const FrameUniform uniform{.viewProjection = viewProjectionMatrix(frame.camera, frame.aspect),
                               .eye = extend(frame.camera.position, 1.0F),
                               .right = extend(angleVectors(frame.camera.angles).right, 0.0F)};
    std::ranges::copy(std::as_bytes(std::span{&uniform, 1}), m_device->mappedBytes(slot.frame.get()).begin());
    const std::span<std::byte> boneBytes = m_device->mappedBytes(slot.bones.get());
    const std::span<std::byte> instanceBytes = m_device->mappedBytes(slot.instances.get());
    std::size_t firstBone = 0;
    for (std::size_t index = 0; index < frame.instances.size(); ++index) {
        const StudioInstance& instance = frame.instances[index];
        std::ranges::copy(std::as_bytes(instance.bones),
                          boneBytes.subspan(firstBone * sizeof(Mat3x4)).begin());
        const InstanceUniform instanceUniform = instanceUniformOf(instance, firstBone);
        std::ranges::copy(std::as_bytes(std::span{&instanceUniform, 1}),
                          instanceBytes.subspan(index * kInstanceUniformStride).begin());
        firstBone += instance.bones.size();
    }
    for (std::size_t index = 0; index < frame.instances.size(); ++index) {
        commands.setPipeline(m_opaquePipeline.get()); // the frame's group binds with a pipeline set
        commands.setBindGroup(kFrameGroup, slot.group.get());
        commands.setBindGroup(kInstanceGroup, slot.instanceGroups[index].get());
        drawMeshes(commands, frame.instances[index]);
    }
    return {};
}

void StudioRenderer::drawMeshes(rhi::CommandList& commands, const StudioInstance& instance) {
    const StudioModel& model = instance.model;
    if (model.m_skins.empty()) {
        return; // no mesh: the reader checked that every mesh has its skin
    }
    commands.setVertexBuffer(0, model.m_vertices.get(), 0);
    commands.setIndexBuffer(model.m_indices.get(), 0, rhi::IndexFormat::Uint32);
    // A skin the model does not have is its first, as the original's R_StudioDrawPoints takes it.
    const std::vector<assets::StudioTextureIndex>& skin =
        model.m_skins[instance.skin < model.m_skins.size() ? instance.skin : 0];
    std::optional<MeshKind> bound;
    for (std::size_t part = 0; part < model.m_bodyParts.size(); ++part) {
        const auto& choices = model.m_bodyParts[part];
        std::uint32_t choice = part < instance.bodyChoices.size() ? instance.bodyChoices[part] : 0;
        if (choice >= choices.size()) {
            expects(false, "StudioRenderer::draw: a choice its body part has");
            choice = 0;
        }
        for (const StudioMeshDraw& mesh : choices[choice]) {
            const StudioSkinDraw& texture = model.m_textures[skin[mesh.skinReference].value];
            if (bound != texture.kind) {
                commands.setPipeline(pipelineOf(texture.kind));
                bound = texture.kind;
            }
            commands.setBindGroup(kSkinGroup, texture.group.get());
            commands.drawIndexed(mesh.indexCount, 1, mesh.firstIndex, 0, 0);
        }
    }
}

} // namespace nx::render
