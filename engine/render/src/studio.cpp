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
constexpr std::uint32_t kEnvironmentBinding = 3;
constexpr std::uint32_t kEnvironmentSamplerBinding = 4;
constexpr std::uint32_t kAlbedoBinding = 0;
constexpr std::uint32_t kAlbedoSamplerBinding = 1;
constexpr std::uint32_t kSkinUniformBinding = 2;
constexpr std::uint32_t kEmissiveMaskBinding = 3;
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
/// A channel at its brightest, as `renderamt` counts it.
constexpr float kChannelTop = 255.0F;

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

/// The frame's uniform: the rows of the view-projection matrix, where the view is and its right, and
/// the environment's last mip level and whether there is one.
struct FrameUniform {
    Mat4 viewProjection{};
    Vec4 eye{};
    Vec4 right{};
    Vec4 environment{};
};

/// A texture's uniform: how its texels map to coordinates, how it is drawn, and its material.
struct SkinUniform {
    float texelWidth = 0.0F; ///< 1 / its width.
    float texelHeight = 0.0F;
    std::uint32_t flags = 0;
    float metallic = 0.0F;
    float roughness = 1.0F;
    float emissive = 0.0F;
    float padding0 = 0.0F;
    float padding1 = 0.0F;
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
    float blend = 1.0F;        ///< How opaque it is drawn, from 0 to 1.
    std::uint32_t isUnlit = 0; ///< 1: its vertices' light is its blend (an added model).
    std::uint32_t lightCount = 0;
    std::array<Vec4, kMostStudioLights> lightPositions{}; ///< xyz where, w how far it reaches.
    std::array<Vec4, kMostStudioLights> lightColors{};
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

/// The texels of `texture` through `textureGamma`, a masked one's palette index 255 transparent, and
/// its mip levels, unless it has none.
[[nodiscard]] auto skinTexelsOf(const assets::StudioTexture& texture, const ChannelTable& textureGamma)
    -> MipChain {
    const bool isMasked = texture.flags.test(assets::StudioTextureFlag::Masked);
    std::vector<std::byte> texels;
    texels.reserve(texture.pixels.size() * kRgbaBytes);
    for (const std::byte index : texture.pixels) {
        const auto entry = std::to_integer<std::size_t>(index);
        const bool isTransparent = isMasked && entry == kTransparentIndex;
        for (std::size_t channel = 0; channel < kRgbBytes; ++channel) {
            const auto value = std::to_integer<std::size_t>(texture.palette[(entry * kRgbBytes) + channel]);
            texels.push_back(isTransparent ? std::byte{0} : std::byte{textureGamma[value]});
        }
        texels.push_back(isTransparent ? std::byte{0} : kFull);
    }
    if (texture.flags.test(assets::StudioTextureFlag::NoMipmaps)) {
        return MipChain{.texels = std::move(texels), .offsets = {0}};
    }
    return mipChainOf(std::move(texels), texture.width, texture.height);
}

[[nodiscard]] auto meshKindOf(const assets::StudioTexture& texture,
                              const std::optional<StudioMaterial>& material) noexcept -> MeshKind {
    if (material && material->blend) {
        switch (*material->blend) {
        case StudioBlend::Masked:
            return MeshKind::Masked;
        case StudioBlend::Additive:
            return MeshKind::Additive;
        case StudioBlend::Opaque:
        case StudioBlend::Count:
            break;
        }
        return MeshKind::Opaque;
    }
    if (texture.flags.test(assets::StudioTextureFlag::Masked)) {
        return MeshKind::Masked;
    }
    return texture.flags.test(assets::StudioTextureFlag::Additive) ? MeshKind::Additive : MeshKind::Opaque;
}

/// The uniform of `texture`, of `material` in the enhanced look: its fullbright flag the material's
/// when it says.
[[nodiscard]] auto skinUniformOf(const assets::StudioTexture& texture,
                                 const std::optional<StudioMaterial>& material) noexcept -> SkinUniform {
    const rhi::Extent2d coordinates = material && material->coordinateSize
                                          ? *material->coordinateSize
                                          : rhi::Extent2d{.width = texture.width, .height = texture.height};
    SkinUniform uniform{.texelWidth = 1.0F / static_cast<float>(coordinates.width),
                        .texelHeight = 1.0F / static_cast<float>(coordinates.height)};
    for (const auto& [flag, bit] : {std::pair{assets::StudioTextureFlag::FlatShade, kFlatShade},
                                    std::pair{assets::StudioTextureFlag::Chrome, kChrome},
                                    std::pair{assets::StudioTextureFlag::Fullbright, kFullbright}}) {
        if (texture.flags.test(flag)) {
            uniform.flags |= bit;
        }
    }
    if (material) {
        uniform.metallic = material->metallic;
        uniform.roughness = material->roughness;
        uniform.emissive = material->emissive;
        if (material->isFullbright) {
            uniform.flags =
                *material->isFullbright ? (uniform.flags | kFullbright) : (uniform.flags & ~kFullbright);
        }
    }
    return uniform;
}

/// The RGBA texels of `image` and its mip levels.
[[nodiscard]] auto imageMipChainOf(const assets::RgbaImage& image) -> MipChain {
    return mipChainOf(image.pixels, image.width, image.height);
}

/// The texels of `image` in place of a texture's, through `textureGamma` as the model's are, and its
/// mip levels.
[[nodiscard]] auto baseColorTexelsOf(const assets::RgbaImage& image, const ChannelTable& textureGamma)
    -> MipChain {
    std::vector<std::byte> texels = image.pixels;
    for (std::size_t at = 0; at < texels.size(); at += kRgbaBytes) {
        for (std::size_t channel = 0; channel < kRgbBytes; ++channel) {
            texels[at + channel] =
                std::byte{textureGamma[std::to_integer<std::size_t>(texels[at + channel])]};
        }
    }
    return mipChainOf(std::move(texels), image.width, image.height);
}

/// How `mode` blends a model (GL_StudioSetRenderMode): any mode but Normal and TransAdd as blended.
[[nodiscard]] auto blendingOf(RenderMode mode) noexcept -> StudioBlending {
    switch (mode) {
    case RenderMode::Normal:
        return StudioBlending::Solid;
    case RenderMode::TransAdd:
        return StudioBlending::Added;
    case RenderMode::TransColor:
    case RenderMode::TransTexture:
    case RenderMode::Glow:
    case RenderMode::TransAlpha:
    case RenderMode::Count:
        break;
    }
    return StudioBlending::Blended;
}

/// How opaque `instance` is drawn: a solid model wholly, another as its `renderamt` says.
[[nodiscard]] auto blendOf(const StudioInstance& instance) noexcept -> float {
    return blendingOf(instance.mode) == StudioBlending::Solid
               ? 1.0F
               : static_cast<float>(instance.amount) / kChannelTop;
}

/// The uniform of `instance`, its bones from `firstBone` on.
[[nodiscard]] auto instanceUniformOf(const StudioInstance& instance, std::size_t firstBone)
    -> InstanceUniform {
    InstanceUniform uniform{.lightDirection = extend(instance.light.direction, 0.0F),
                            .lightColor = extend(instance.light.color, 0.0F),
                            .ambient = instance.light.ambient,
                            .shade = instance.light.shade,
                            .firstBone = narrow<std::uint32_t>(firstBone),
                            .blend = blendOf(instance),
                            .isUnlit = blendingOf(instance.mode) == StudioBlending::Added ? 1U : 0U};
    const std::span<const StudioLight> lights =
        instance.lights.first(std::min(instance.lights.size(), kMostStudioLights));
    uniform.lightCount = narrow<std::uint32_t>(lights.size());
    for (std::size_t index = 0; index < lights.size(); ++index) {
        uniform.lightPositions.at(index) = extend(lights[index].position, lights[index].reach);
        uniform.lightColors.at(index) = extend(lights[index].color, 0.0F);
    }
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
    if (auto made = renderer.createPipelines(desc.shaderCode, desc.colorFormat, desc.look); !made) {
        return fail(StudioError::GpuFailed, std::move(made.error()));
    }
    // A material without an emissive mask emits everywhere; a renderer without a sky reflects none.
    constexpr std::array kWhite{std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
    constexpr std::array kBlackSides = [] {
        std::array<std::byte, kRgbaBytes * rhi::kCubeFaces> sides{};
        for (std::size_t side = 0; side < rhi::kCubeFaces; ++side) {
            sides.at((side * kRgbaBytes) + kRgbBytes) = kFull;
        }
        return sides;
    }();
    std::array<std::size_t, rhi::kCubeFaces> sideOffsets{};
    for (std::size_t side = 0; side < sideOffsets.size(); ++side) {
        sideOffsets.at(side) = side * kRgbaBytes;
    }
    auto white = uploader.texture("studio.whiteMask", {.width = 1, .height = 1}, 1, 1, kWhite, kOneLevel);
    auto black = uploader.texture("studio.noEnvironment", {.width = 1, .height = 1}, 1, rhi::kCubeFaces,
                                  kBlackSides, sideOffsets);
    if (!white || !black) {
        return fail(StudioError::GpuFailed, std::move(!white ? white.error() : black.error()));
    }
    renderer.m_whiteMask = std::move(*white);
    renderer.m_environment = std::move(*black);
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

auto StudioRenderer::createPipelines(std::span<const std::byte> shaderCode, rhi::Format colorFormat,
                                     StudioLook look) -> Result<void, rhi::RhiError> {
    rhi::Device& device = *m_device;
    constexpr Flags<rhi::ShaderStage> kVertex{rhi::ShaderStage::Vertex};
    constexpr Flags<rhi::ShaderStage> kFragment{rhi::ShaderStage::Fragment};
    constexpr Flags<rhi::ShaderStage> kBothStages{rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment};
    const std::array frameBindings{
        rhi::BindingLayout{
            .binding = kFrameUniformBinding, .type = rhi::BindingType::UniformBuffer, .stages = kBothStages},
        rhi::BindingLayout{
            .binding = kLightGammaBinding, .type = rhi::BindingType::StorageBuffer, .stages = kVertex},
        rhi::BindingLayout{
            .binding = kBonesBinding, .type = rhi::BindingType::StorageBuffer, .stages = kVertex},
        rhi::BindingLayout{
            .binding = kEnvironmentBinding, .type = rhi::BindingType::SampledTexture, .stages = kFragment},
        rhi::BindingLayout{
            .binding = kEnvironmentSamplerBinding, .type = rhi::BindingType::Sampler, .stages = kFragment}};
    const std::array skinBindings{
        rhi::BindingLayout{
            .binding = kAlbedoBinding, .type = rhi::BindingType::SampledTexture, .stages = kFragment},
        rhi::BindingLayout{
            .binding = kAlbedoSamplerBinding, .type = rhi::BindingType::Sampler, .stages = kFragment},
        rhi::BindingLayout{
            .binding = kSkinUniformBinding, .type = rhi::BindingType::UniformBuffer, .stages = kBothStages},
        rhi::BindingLayout{
            .binding = kEmissiveMaskBinding, .type = rhi::BindingType::SampledTexture, .stages = kFragment}};
    const std::array instanceBindings{rhi::BindingLayout{
        .binding = kInstanceUniformBinding, .type = rhi::BindingType::UniformBuffer, .stages = kBothStages}};
    auto shader = device.createShader({.label = "studio", .code = shaderCode});
    auto frameLayout = device.createBindGroupLayout({.label = "studio.frame", .bindings = frameBindings});
    auto skinLayout = device.createBindGroupLayout({.label = "studio.skin", .bindings = skinBindings});
    auto instanceLayout =
        device.createBindGroupLayout({.label = "studio.instance", .bindings = instanceBindings});
    auto sampler = device.createSampler({.label = "studio.skin"});
    auto environmentSampler =
        device.createSampler({.label = "studio.environment", .address = rhi::AddressMode::ClampToEdge});
    if (!shader || !frameLayout || !skinLayout || !instanceLayout || !sampler || !environmentSampler) {
        return std::unexpected(!shader           ? std::move(shader.error())
                               : !frameLayout    ? std::move(frameLayout.error())
                               : !skinLayout     ? std::move(skinLayout.error())
                               : !instanceLayout ? std::move(instanceLayout.error())
                               : !sampler        ? std::move(sampler.error())
                                                 : std::move(environmentSampler.error()));
    }
    m_environmentSampler = std::move(*environmentSampler);
    m_shader = std::move(*shader);
    m_frameLayout = std::move(*frameLayout);
    m_skinLayout = std::move(*skinLayout);
    m_instanceLayout = std::move(*instanceLayout);
    m_sampler = std::move(*sampler);

    // The original culls the triangles that face the viewer in GL's sense (GL_FRONT): a model's
    // triangles face away from it; mirrored, they face the other way, and the others are culled. Each
    // facing, each render mode's blending, and in it each kind of mesh, as
    // GL_StudioSetRenderMode and R_StudioDrawPoints set them: an additive mesh adds itself, as
    // its texture's alpha says unless the model is solid; a masked one is cut at an alpha of 0.5.
    const std::array vertexBuffers{
        rhi::VertexBufferLayout{.stride = sizeof(StudioVertex), .attributes = kStudioVertexAttributes}};
    const std::array layouts{m_frameLayout.get(), m_skinLayout.get(), m_instanceLayout.get()};
    const auto blendOf = [](rhi::BlendFactor source, rhi::BlendFactor destination) {
        return rhi::BlendState{.sourceColor = source,
                               .destinationColor = destination,
                               .sourceAlpha = source,
                               .destinationAlpha = destination};
    };
    const rhi::BlendState overBehind =
        blendOf(rhi::BlendFactor::SourceAlpha, rhi::BlendFactor::OneMinusSourceAlpha);
    const rhi::BlendState added = blendOf(rhi::BlendFactor::One, rhi::BlendFactor::One);
    const rhi::BlendState addedByAlpha = blendOf(rhi::BlendFactor::SourceAlpha, rhi::BlendFactor::One);
    /// How a pipeline draws: its fragments, its blend, and whether it writes its depth.
    struct Way {
        std::string_view fragment;
        std::optional<rhi::BlendState> blend;
        bool writesDepth = true;
    };
    const bool isEnhanced = look == StudioLook::Enhanced;
    const std::string_view kWhole = isEnhanced ? "enhancedFragmentMain" : "fragmentMain";
    const std::string_view kCut = isEnhanced ? "enhancedMaskedFragmentMain" : "maskedFragmentMain";
    using WaysOfMeshes = EnumArray<MeshKind, Way>;
    const EnumArray<StudioBlending, WaysOfMeshes> ways{{
        WaysOfMeshes{{Way{.fragment = kWhole, .blend = std::nullopt},
                      Way{.fragment = kCut, .blend = std::nullopt},
                      Way{.fragment = kWhole, .blend = added, .writesDepth = false}}},
        WaysOfMeshes{{Way{.fragment = kWhole, .blend = overBehind},
                      Way{.fragment = kCut, .blend = overBehind},
                      Way{.fragment = kWhole, .blend = addedByAlpha}}},
        WaysOfMeshes{{Way{.fragment = kWhole, .blend = added, .writesDepth = false},
                      Way{.fragment = kCut, .blend = added, .writesDepth = false},
                      Way{.fragment = kWhole, .blend = addedByAlpha, .writesDepth = false}}},
    }};
    for (const StudioFacing facing : EnumArray<StudioFacing, int>::keys()) {
        const rhi::CullMode culled =
            facing == StudioFacing::Mirrored ? rhi::CullMode::Back : rhi::CullMode::Front;
        for (const StudioBlending blending : EnumArray<StudioBlending, WaysOfMeshes>::keys()) {
            for (const MeshKind kind : WaysOfMeshes::keys()) {
                const Way& way = ways[blending][kind];
                const std::array targets{rhi::ColorTarget{.format = colorFormat, .blend = way.blend}};
                auto pipeline = device.createRenderPipeline(
                    {.label = std::format("studio.{}.{}.{}", std::to_underlying(facing),
                                          std::to_underlying(blending), std::to_underlying(kind)),
                     .vertex = {.shader = m_shader.get(), .entryPoint = "vertexMain"},
                     .fragment = {.shader = m_shader.get(), .entryPoint = way.fragment},
                     .vertexBuffers = vertexBuffers,
                     .cullMode = culled,
                     .frontFace = rhi::FrontFace::CounterClockwise,
                     .colorTargets = targets,
                     .depth = rhi::DepthState{.isWriteEnabled = way.writesDepth,
                                              .compare = rhi::CompareOp::Greater},
                     .bindGroupLayouts = layouts});
                if (!pipeline) {
                    return std::unexpected{std::move(pipeline.error())};
                }
                m_pipelines[facing][blending][kind] = std::move(*pipeline);
            }
        }
    }
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
            rhi::Binding{.binding = kBonesBinding, .resource = rhi::BufferBinding{.buffer = buffer->get()}},
            rhi::Binding{.binding = kEnvironmentBinding, .resource = m_environment.get()},
            rhi::Binding{.binding = kEnvironmentSamplerBinding, .resource = m_environmentSampler.get()}};
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
    model.m_middle = (desc.model.bounds().min + desc.model.bounds().max) * (1.0F / 2.0F);
    model.m_skins.assign(desc.textures.skins().begin(), desc.textures.skins().end());
    const std::span<const assets::StudioTexture> textures = desc.textures.textures();
    for (std::size_t index = 0; index < textures.size(); ++index) {
        const assets::StudioTexture& texture = textures[index];
        const std::optional<StudioMaterial> material =
            index < desc.materials.size() ? desc.materials[index] : std::nullopt;
        const bool hasBaseColor = material && material->baseColor;
        const MipChain texels = hasBaseColor ? baseColorTexelsOf(*material->baseColor, m_textureGamma)
                                             : skinTexelsOf(texture, m_textureGamma);
        const rhi::Extent2d extent =
            hasBaseColor
                ? rhi::Extent2d{.width = material->baseColor->width, .height = material->baseColor->height}
                : rhi::Extent2d{.width = texture.width, .height = texture.height};
        const std::string label = std::format("studio.{}", texture.name);
        auto uploaded = uploader.texture(label, extent, narrow<std::uint32_t>(texels.offsets.size()), 1,
                                         texels.texels, texels.offsets);
        const SkinUniform skin = skinUniformOf(texture, material);
        auto uniform = uploader.buffer(label, rhi::BufferUsage::Uniform, rhi::BufferState::Uniform,
                                       std::as_bytes(std::span{&skin, 1}));
        if (!uploaded || !uniform) {
            return gpuFailure(std::move(!uploaded ? uploaded.error() : uniform.error()));
        }
        rhi::OwnedTexture emissiveMask;
        if (material && material->emissiveMask) {
            const assets::RgbaImage& mask = *material->emissiveMask;
            const MipChain maskTexels = imageMipChainOf(mask);
            auto maskUploaded = uploader.texture(
                std::format("{}.emissiveMask", label), {.width = mask.width, .height = mask.height},
                narrow<std::uint32_t>(maskTexels.offsets.size()), 1, maskTexels.texels, maskTexels.offsets);
            if (!maskUploaded) {
                return gpuFailure(std::move(maskUploaded.error()));
            }
            emissiveMask = std::move(*maskUploaded);
        }
        const std::array bindings{
            rhi::Binding{.binding = kAlbedoBinding, .resource = uploaded->get()},
            rhi::Binding{.binding = kAlbedoSamplerBinding, .resource = m_sampler.get()},
            rhi::Binding{.binding = kSkinUniformBinding,
                         .resource = rhi::BufferBinding{.buffer = uniform->get()}},
            rhi::Binding{.binding = kEmissiveMaskBinding,
                         .resource = emissiveMask.get().isNull() ? m_whiteMask.get() : emissiveMask.get()}};
        auto group =
            m_device->createBindGroup({.label = label, .layout = m_skinLayout.get(), .bindings = bindings});
        if (!group) {
            return gpuFailure(std::move(group.error()));
        }
        model.m_textures.push_back(StudioSkinDraw{.texture = std::move(*uploaded),
                                                  .emissiveMask = std::move(emissiveMask),
                                                  .uniform = std::move(*uniform),
                                                  .group = std::move(*group),
                                                  .kind = meshKindOf(texture, material)});
    }
    return model;
}

auto StudioRenderer::setEnvironment(rhi::CommandList& commands, std::span<const assets::RgbaImage> sky)
    -> Result<void, StudioError> {
    if (sky.empty()) {
        return {};
    }
    const auto isOfTheFirstsSize = [&sky](const assets::RgbaImage& side) {
        return side.width == sky.front().width && side.height == sky.front().height &&
               side.pixels.size() == std::size_t{side.width} * side.height * kRgbaBytes;
    };
    if (sky.size() != kSkySuffixes.size() || sky.front().width == 0 || sky.front().height == 0 ||
        !std::ranges::all_of(sky, isOfTheFirstsSize)) {
        return fail(StudioError::BadTextures, std::format("a sky of {} sides", sky.size()));
    }
    // Its sides toward +X, -X, +Y, -Y, +Z and -Z, each with its mip levels, layer after layer.
    std::vector<std::byte> texels;
    std::vector<std::size_t> offsets;
    std::uint32_t levels = 0;
    for (const std::size_t side : kSkySideOfAxis) {
        const MipChain chain = imageMipChainOf(sky[side]);
        levels = narrow<std::uint32_t>(chain.offsets.size());
        for (const std::size_t offset : chain.offsets) {
            offsets.push_back(texels.size() + offset);
        }
        texels.insert(texels.end(), chain.texels.begin(), chain.texels.end());
    }
    Uploader uploader{*m_device, commands};
    auto environment =
        uploader.texture("studio.environment", {.width = sky.front().width, .height = sky.front().height},
                         levels, narrow<std::uint32_t>(kSkySuffixes.size()), texels, offsets);
    if (!environment) {
        return fail(StudioError::GpuFailed, std::move(environment.error()));
    }
    m_environment = std::move(*environment);
    m_environmentLevels = levels;
    m_hasEnvironment = true;
    // Each frame's group names the environment: made again as each slot is next written.
    for (StudioFrameSlot& slot : m_frameSlots) {
        slot.group = {};
    }
    return {};
}

auto StudioRenderer::prepare(const StudioFrame& frame) -> Result<void, StudioError> {
    m_modelCalls.clear();
    m_meshCalls.clear();
    m_blendedDraws.clear();
    // A ring of kMaxFramesInFlight: the slot written now was last read frames in flight ago.
    StudioFrameSlot& slot = m_frameSlots[m_drawCount++ % m_frameSlots.size()];
    std::size_t boneCount = 0;
    for (const StudioInstance& instance : frame.instances) {
        expects(instance.bones.size() == instance.model.boneCount(),
                "StudioRenderer::prepare: every bone of a model");
        boneCount += instance.bones.size();
    }
    if (auto reserved = reserve(slot, boneCount, frame.instances.size()); !reserved) {
        return fail(StudioError::GpuFailed, std::move(reserved.error()));
    }
    const FrameUniform uniform{.viewProjection = viewProjectionMatrix(frame.camera, frame.aspect),
                               .eye = extend(frame.camera.position, 1.0F),
                               .right = extend(angleVectors(frame.camera.angles).right, 0.0F),
                               .environment = Vec4{.x = static_cast<float>(m_environmentLevels - 1),
                                                   .y = m_hasEnvironment ? 1.0F : 0.0F,
                                                   .z = 0.0F,
                                                   .w = 0.0F}};
    std::ranges::copy(std::as_bytes(std::span{&uniform, 1}), m_device->mappedBytes(slot.frame.get()).begin());

    // Every model but the invisible ones (R_AddEntity): the solid ones drawn with the solid things of
    // the frame, the others among everything drawn blended, by the middle of their bounds
    // (R_TransEntityCompare).
    const std::span<std::byte> boneBytes = m_device->mappedBytes(slot.bones.get());
    const std::span<std::byte> instanceBytes = m_device->mappedBytes(slot.instances.get());
    std::size_t firstBone = 0;
    for (const StudioInstance& instance : frame.instances) {
        const StudioBlending blending = blendingOf(instance.mode);
        if (blending != StudioBlending::Solid && instance.amount == 0) {
            continue;
        }
        const std::size_t index = m_modelCalls.size();
        std::ranges::copy(std::as_bytes(instance.bones),
                          boneBytes.subspan(firstBone * sizeof(Mat3x4)).begin());
        const InstanceUniform instanceUniform = instanceUniformOf(instance, firstBone);
        std::ranges::copy(std::as_bytes(std::span{&instanceUniform, 1}),
                          instanceBytes.subspan(index * kInstanceUniformStride).begin());
        firstBone += instance.bones.size();
        if (blending != StudioBlending::Solid) {
            m_blendedDraws.push_back(BlendedDraw{
                .distance =
                    lengthSquared(frame.camera.position - (instance.origin + instance.model.m_middle)),
                .mode = instance.mode,
                .index = narrow<std::uint32_t>(index)});
        }
        m_modelCalls.push_back(callOf(instance, blending));
    }
    return {};
}

auto StudioRenderer::draw(rhi::CommandList& commands, const StudioFrame& frame) -> Result<void, StudioError> {
    if (auto prepared = prepare(frame); !prepared) {
        return std::unexpected{std::move(prepared.error())};
    }
    drawInOrder(commands, *this);
    return {};
}

void StudioRenderer::drawSolid(rhi::CommandList& commands) const {
    for (std::size_t index = 0; index < m_modelCalls.size(); ++index) {
        if (m_modelCalls[index].blending == StudioBlending::Solid) {
            drawModel(commands, index);
        }
    }
}

void StudioRenderer::drawBlended(rhi::CommandList& commands, std::uint32_t index) const {
    if (index >= m_modelCalls.size()) {
        expects(false, "StudioRenderer::drawBlended: one of blendedDraws()'s");
        return;
    }
    drawModel(commands, index);
}

auto StudioRenderer::callOf(const StudioInstance& instance, StudioBlending blending) -> StudioModelCall {
    const StudioModel& model = instance.model;
    StudioModelCall call{.vertices = model.m_vertices.get(),
                         .indices = model.m_indices.get(),
                         .blending = blending,
                         .facing = instance.isMirrored ? StudioFacing::Mirrored : StudioFacing::AsModeled,
                         .firstMesh = narrow<std::uint32_t>(m_meshCalls.size())};
    if (model.m_skins.empty()) {
        return call; // no mesh: the reader checked that every mesh has its skin
    }
    // A skin the model does not have is its first, as the original's R_StudioDrawPoints takes it.
    const std::vector<assets::StudioTextureIndex>& skin =
        model.m_skins[instance.skin < model.m_skins.size() ? instance.skin : 0];
    for (std::size_t part = 0; part < model.m_bodyParts.size(); ++part) {
        const auto& choices = model.m_bodyParts[part];
        std::uint32_t choice = part < instance.bodyChoices.size() ? instance.bodyChoices[part] : 0;
        if (choice >= choices.size()) {
            expects(false, "StudioRenderer::prepare: a choice its body part has");
            choice = 0;
        }
        for (const StudioMeshDraw& mesh : choices[choice]) {
            const StudioSkinDraw& texture = model.m_textures[skin[mesh.skinReference].value];
            m_meshCalls.push_back(StudioMeshCall{.firstIndex = mesh.firstIndex,
                                                 .indexCount = mesh.indexCount,
                                                 .kind = texture.kind,
                                                 .texture = texture.group.get()});
        }
    }
    call.meshCount = narrow<std::uint32_t>(m_meshCalls.size()) - call.firstMesh;
    return call;
}

auto StudioRenderer::preparedSlot() const noexcept -> const StudioFrameSlot& {
    expects(m_drawCount > 0, "StudioRenderer: prepared before it is drawn");
    return m_frameSlots[(m_drawCount - 1) % m_frameSlots.size()];
}

void StudioRenderer::drawModel(rhi::CommandList& commands, std::size_t index) const {
    const StudioModelCall& call = m_modelCalls[index];
    const std::span<const StudioMeshCall> meshes =
        std::span{m_meshCalls}.subspan(call.firstMesh, call.meshCount);
    if (meshes.empty()) {
        return;
    }
    const StudioFrameSlot& slot = preparedSlot();
    // The frame's group binds with a pipeline set.
    MeshKind bound = meshes.front().kind;
    commands.setPipeline(m_pipelines[call.facing][call.blending][bound].get());
    commands.setBindGroup(kFrameGroup, slot.group.get());
    commands.setBindGroup(kInstanceGroup, slot.instanceGroups[index].get());
    commands.setVertexBuffer(0, call.vertices, 0);
    commands.setIndexBuffer(call.indices, 0, rhi::IndexFormat::Uint32);
    for (const StudioMeshCall& mesh : meshes) {
        if (mesh.kind != bound) {
            bound = mesh.kind;
            commands.setPipeline(m_pipelines[call.facing][call.blending][bound].get());
        }
        commands.setBindGroup(kSkinGroup, mesh.texture);
        commands.drawIndexed(mesh.indexCount, 1, mesh.firstIndex, 0, 0);
    }
}

} // namespace nx::render
