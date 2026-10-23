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
constexpr std::uint32_t kProbesBinding = 5;
constexpr std::uint32_t kSpecularIntegralBinding = 6;
constexpr std::uint32_t kAlbedoBinding = 0;
constexpr std::uint32_t kAlbedoSamplerBinding = 1;
constexpr std::uint32_t kSkinUniformBinding = 2;
constexpr std::uint32_t kEmissiveMaskBinding = 3;
constexpr std::uint32_t kSpecularMapBinding = 4;
constexpr std::uint32_t kPaintedLightBinding = 5;
constexpr std::uint32_t kInstanceUniformBinding = 0;

/// The most texels the enhanced look's sampler takes along a texture seen aslant, so that a painting's
/// detail stays on faces seen edge on; the device holds it to its own most.
constexpr float kEnhancedAnisotropy = 16.0F;

// Skin::flags, as the shader reads them.
constexpr std::uint32_t kFlatShade = 1;
constexpr std::uint32_t kChrome = 2;
constexpr std::uint32_t kFullbright = 4;

// Skin::surface, as the shader reads it: none, StudioMetallicRoughness or StudioPainted.
constexpr std::uint32_t kOriginalSurface = 0;
constexpr std::uint32_t kMetallicRoughnessSurface = 1;
constexpr std::uint32_t kPaintedSurface = 2;

/// The palette index a masked texture draws as transparent.
constexpr std::uint8_t kTransparentIndex = 255;
/// The bytes of a palette color, red, green and blue; of a texel, red, green, blue and alpha.
constexpr std::size_t kRgbBytes = 3;
constexpr std::size_t kRgbaBytes = 4;
/// A channel at its brightest, and an opaque alpha.
constexpr std::byte kFull{255};
/// A channel at its brightest, as `renderamt` counts it.
constexpr float kChannelTop = 255.0F;

/// GGX's reflection integrated over the directions it gathers (the split sum's second factor), which
/// does not depend on the surroundings: its texels, across the cosine of the view to the normal and down
/// the roughness, each from the middle of its square; and the directions each integral samples.
constexpr std::uint32_t kSpecularIntegralSize = 64;
constexpr std::size_t kSpecularIntegralSamples = 256;
constexpr float kTexelMiddle = 0.5F;
/// Hammersley's points count their second coordinate in this base (the van der Corput sequence).
constexpr std::uint32_t kRadicalBase = 2;
/// A reflection's two: the light comes in at twice the angle between the view and the half vector.
constexpr float kReflectionScale = 2.0F;
constexpr float kSchlickPower = 5.0F;
constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;

/// `index` with its digits in kRadicalBase mirrored about the point: the van der Corput sequence.
[[nodiscard]] auto radicalInverseOf(std::uint32_t index) noexcept -> float {
    float inverse = 0.0F;
    float place = 1.0F / static_cast<float>(kRadicalBase);
    while (index > 0) {
        inverse += static_cast<float>(index % kRadicalBase) * place;
        index /= kRadicalBase;
        place /= static_cast<float>(kRadicalBase);
    }
    return inverse;
}

/// Half vectors about the normal (+Z) as GGX of `alphaSquared` distributes microfacet normals, at
/// Hammersley's points.
[[nodiscard]] auto halfVectorsOf(float alphaSquared) -> std::array<Vec3, kSpecularIntegralSamples> {
    std::array<Vec3, kSpecularIntegralSamples> samples{};
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const float azimuth = kTwoPi * static_cast<float>(index) / static_cast<float>(samples.size());
        const float share = radicalInverseOf(narrow<std::uint32_t>(index));
        const float cosine = std::sqrt((1.0F - share) / (1.0F + ((alphaSquared - 1.0F) * share)));
        const float sine = std::sqrt(std::max(1.0F - (cosine * cosine), 0.0F));
        samples[index] = {.x = std::cos(azimuth) * sine, .y = std::sin(azimuth) * sine, .z = cosine};
    }
    return samples;
}

/// GGX's reflection with height-correlated Smith visibility and Schlick's Fresnel, integrated over the
/// directions it gathers for a view at `nDotV` to the normal (Filament, sections 5.3.4 and 9.5,
/// https://google.github.io/filament/main/filament.html#annex/precomputinglforimage-basedlighting):
/// the share of the reflectance head on (F0), and what grazing angles add; the mean of `halfVectors`,
/// each weighted by how likely GGX made it.
struct SpecularIntegral {
    float scale = 0.0F;
    float bias = 0.0F;
};

[[nodiscard]] auto specularIntegralOf(std::span<const Vec3> halfVectors, float nDotV, float alphaSquared)
    -> SpecularIntegral {
    const Vec3 view{.x = std::sqrt(1.0F - (nDotV * nDotV)), .z = nDotV};
    SpecularIntegral integral{};
    for (const Vec3 half : halfVectors) {
        const float vDotH = std::clamp(dot(view, half), 0.0F, 1.0F);
        const float nDotL = (kReflectionScale * vDotH * half.z) - nDotV;
        if (nDotL <= 0.0F) {
            continue;
        }
        const float visibilityView =
            nDotL * std::sqrt((nDotV * nDotV * (1.0F - alphaSquared)) + alphaSquared);
        const float visibilityLight =
            nDotV * std::sqrt((nDotL * nDotL * (1.0F - alphaSquared)) + alphaSquared);
        const float weight =
            (kReflectionScale * nDotL * vDotH) / ((visibilityView + visibilityLight) * half.z);
        const float fresnel = std::pow(1.0F - vDotH, kSchlickPower);
        integral.scale += weight * (1.0F - fresnel);
        integral.bias += weight * fresnel;
    }
    const float count = static_cast<float>(halfVectors.size());
    return {.scale = integral.scale / count, .bias = integral.bias / count};
}

/// The texels of the specular integral's texture: red its scale, green its bias.
[[nodiscard]] auto specularIntegralTexels() -> std::vector<std::byte> {
    std::vector<std::byte> texels;
    texels.reserve(std::size_t{kSpecularIntegralSize} * kSpecularIntegralSize * kRgbaBytes);
    const auto channelOf = [](float share) {
        return std::byte{narrow<std::uint8_t>(std::lround(std::clamp(share, 0.0F, 1.0F) * kChannelTop))};
    };
    for (std::uint32_t row = 0; row < kSpecularIntegralSize; ++row) {
        const float roughness = (static_cast<float>(row) + kTexelMiddle) / kSpecularIntegralSize;
        const float alpha = roughness * roughness;
        const float alphaSquared = alpha * alpha;
        const auto halfVectors = halfVectorsOf(alphaSquared);
        for (std::uint32_t column = 0; column < kSpecularIntegralSize; ++column) {
            const float nDotV = (static_cast<float>(column) + kTexelMiddle) / kSpecularIntegralSize;
            const SpecularIntegral integral = specularIntegralOf(halfVectors, nDotV, alphaSquared);
            texels.insert(texels.end(),
                          {channelOf(integral.scale), channelOf(integral.bias), std::byte{0}, kFull});
        }
    }
    return texels;
}

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

/// The frame's uniform: the rows of the view-projection matrix, where the view is and its right, the
/// last mip levels of the environment and of the probes and whether the environment is a sky, and how
/// colors reach the target and models are shaded.
struct FrameUniform {
    Mat4 viewProjection{};
    Vec4 eye{};
    Vec4 right{};
    Vec4 environment{};
    std::uint32_t colorEncoding = 0; ///< ColorEncoding's.
    std::uint32_t look = 0;          ///< StudioLook's.
};

/// A texture's uniform: how its texels map to coordinates, how it is drawn, and its material.
struct SkinUniform {
    float texelWidth = 0.0F; ///< 1 / its width.
    float texelHeight = 0.0F;
    std::uint32_t flags = 0;
    std::uint32_t surface = kOriginalSurface;
    float metallic = 0.0F;
    float roughness = 1.0F;
    float emissive = 0.0F;
    std::uint32_t hasSpecularMap = 0;
    Vec4 specular{}; ///< A painted surface's reflection: xyz its color head on, w how much is added.
    std::uint32_t hasPaintedLight = 0;
    float paintedLightGain = 1.0F;
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
    std::int32_t probe = -1; ///< The reflection probe it reflects; -1 for none.
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
/// its mip levels, unless it has none, as a color texture of `encoding` stores them.
[[nodiscard]] auto skinTexelsOf(const assets::StudioTexture& texture, const ChannelTable& textureGamma,
                                ColorEncoding encoding) -> MipChain {
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
        return MipChain{.texels = colorTexelsOf(std::move(texels), encoding), .offsets = {0}};
    }
    return colorMipChainOf(std::move(texels), texture.width, texture.height, encoding);
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

/// Writes a metallic-roughness surface to `uniform`.
void addSurface(SkinUniform& uniform, const StudioMetallicRoughness& surface) noexcept {
    uniform.surface = kMetallicRoughnessSurface;
    uniform.metallic = surface.metallic;
    uniform.roughness = surface.roughness;
}

/// Writes a painted surface to `uniform`.
void addSurface(SkinUniform& uniform, const StudioPainted& surface) noexcept {
    uniform.surface = kPaintedSurface;
    uniform.roughness = surface.roughness;
    uniform.specular = extend(surface.specularColor, surface.specularWeight);
    uniform.hasSpecularMap = surface.specularMap ? 1U : 0U;
    uniform.hasPaintedLight = surface.light ? 1U : 0U;
    uniform.paintedLightGain = surface.light ? surface.light->gain : 1.0F;
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
        uniform.emissive = material->emissive;
        if (material->surface) {
            std::visit([&uniform](const auto& surface) { addSurface(uniform, surface); }, *material->surface);
        }
        if (material->isFullbright) {
            uniform.flags =
                *material->isFullbright ? (uniform.flags | kFullbright) : (uniform.flags & ~kFullbright);
        }
    }
    return uniform;
}

/// The texels of `image` in place of a texture's, through `textureGamma` as the model's are, and its
/// mip levels, as a color texture of `encoding` stores them.
[[nodiscard]] auto baseColorTexelsOf(const assets::RgbaImage& image, const ChannelTable& textureGamma,
                                     ColorEncoding encoding) -> MipChain {
    std::vector<std::byte> texels = image.pixels;
    for (std::size_t at = 0; at < texels.size(); at += kRgbaBytes) {
        for (std::size_t channel = 0; channel < kRgbBytes; ++channel) {
            texels[at + channel] =
                std::byte{textureGamma[std::to_integer<std::size_t>(texels[at + channel])]};
        }
    }
    return colorMipChainOf(std::move(texels), image.width, image.height, encoding);
}

/// `chain`, the texels and mip levels of an image of `extent`, on the GPU as `format`.
[[nodiscard]] auto uploadChain(Uploader& uploader, std::string_view label, rhi::Extent2d extent,
                               const MipChain& chain, rhi::Format format)
    -> Result<rhi::OwnedTexture, rhi::RhiError> {
    return uploader.texture(label, extent, narrow<std::uint32_t>(chain.offsets.size()), 1, chain.texels,
                            chain.offsets, format);
}

[[nodiscard]] auto extentOf(const assets::RgbaImage& image) noexcept -> rhi::Extent2d {
    return {.width = image.width, .height = image.height};
}

/// The images of a texture's material on the GPU; each it lacks null, which binds the renderer's white.
struct MaterialImages {
    rhi::OwnedTexture emissiveMask{};
    rhi::OwnedTexture specularMap{};
    rhi::OwnedTexture paintedLight{};
};

/// The images of `material` on the GPU, labeled after its texture's `label`: its masks and maps as data,
/// its painted light's map as light (sRGB, as a linear target's colors). Errors: those of the uploads.
[[nodiscard]] auto uploadMaterialImages(Uploader& uploader, std::string_view label,
                                        const StudioMaterial& material)
    -> Result<MaterialImages, rhi::RhiError> {
    const auto uploadData = [&uploader, label](std::string_view part, const assets::RgbaImage& image) {
        return uploadChain(uploader, std::format("{}.{}", label, part), extentOf(image),
                           mipChainOf(image.pixels, image.width, image.height), rhi::Format::Rgba8Unorm);
    };
    MaterialImages images;
    if (material.emissiveMask) {
        auto mask = uploadData("emissiveMask", *material.emissiveMask);
        if (!mask) {
            return std::unexpected{std::move(mask.error())};
        }
        images.emissiveMask = std::move(*mask);
    }
    const StudioPainted* const painted =
        material.surface ? std::get_if<StudioPainted>(&*material.surface) : nullptr;
    if (painted != nullptr && painted->specularMap) {
        auto map = uploadData("specularMap", *painted->specularMap);
        if (!map) {
            return std::unexpected{std::move(map.error())};
        }
        images.specularMap = std::move(*map);
    }
    if (painted != nullptr && painted->light) {
        const assets::RgbaImage& map = painted->light->map;
        auto light = uploadChain(uploader, std::format("{}.paintedLight", label), extentOf(map),
                                 colorMipChainOf(map.pixels, map.width, map.height, ColorEncoding::Linear),
                                 colorFormatOf(ColorEncoding::Linear));
        if (!light) {
            return std::unexpected{std::move(light.error())};
        }
        images.paintedLight = std::move(*light);
    }
    return images;
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
    uniform.probe = instance.probe ? narrow<std::int32_t>(*instance.probe) : -1;
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
    renderer.m_look = desc.look;
    renderer.m_encoding = desc.encoding;
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
    // A material without an image is white; a renderer without a sky or probes reflects none.
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
    if (desc.look == StudioLook::Enhanced) {
        auto integral = uploader.texture("studio.specularIntegral",
                                         {.width = kSpecularIntegralSize, .height = kSpecularIntegralSize}, 1,
                                         1, specularIntegralTexels(), kOneLevel);
        if (!integral) {
            return fail(StudioError::GpuFailed, std::move(integral.error()));
        }
        renderer.m_specularIntegral = std::move(*integral);
    }
    auto noProbes = uploader.texture("studio.noProbes", {.width = 1, .height = 1}, 1, rhi::kCubeFaces,
                                     kBlackSides, sideOffsets);
    if (!noProbes) {
        return fail(StudioError::GpuFailed, std::move(noProbes.error()));
    }
    renderer.m_noProbes = std::move(*noProbes);
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
            .binding = kEnvironmentSamplerBinding, .type = rhi::BindingType::Sampler, .stages = kFragment},
        rhi::BindingLayout{
            .binding = kProbesBinding, .type = rhi::BindingType::SampledTexture, .stages = kFragment},
        rhi::BindingLayout{.binding = kSpecularIntegralBinding,
                           .type = rhi::BindingType::SampledTexture,
                           .stages = kFragment}};
    const std::array skinBindings{
        rhi::BindingLayout{
            .binding = kAlbedoBinding, .type = rhi::BindingType::SampledTexture, .stages = kFragment},
        rhi::BindingLayout{
            .binding = kAlbedoSamplerBinding, .type = rhi::BindingType::Sampler, .stages = kFragment},
        rhi::BindingLayout{
            .binding = kSkinUniformBinding, .type = rhi::BindingType::UniformBuffer, .stages = kBothStages},
        rhi::BindingLayout{
            .binding = kEmissiveMaskBinding, .type = rhi::BindingType::SampledTexture, .stages = kFragment},
        rhi::BindingLayout{
            .binding = kSpecularMapBinding, .type = rhi::BindingType::SampledTexture, .stages = kFragment},
        rhi::BindingLayout{
            .binding = kPaintedLightBinding, .type = rhi::BindingType::SampledTexture, .stages = kFragment}};
    const std::array instanceBindings{rhi::BindingLayout{
        .binding = kInstanceUniformBinding, .type = rhi::BindingType::UniformBuffer, .stages = kBothStages}};
    auto shader = device.createShader({.label = "studio", .code = shaderCode});
    auto frameLayout = device.createBindGroupLayout({.label = "studio.frame", .bindings = frameBindings});
    auto skinLayout = device.createBindGroupLayout({.label = "studio.skin", .bindings = skinBindings});
    auto instanceLayout =
        device.createBindGroupLayout({.label = "studio.instance", .bindings = instanceBindings});
    auto sampler = device.createSampler(
        {.label = "studio.skin", .maxAnisotropy = look == StudioLook::Enhanced ? kEnhancedAnisotropy : 1.0F});
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
    // its texture's alpha says unless the model is solid; a masked one is cut at an alpha of 0.5. Drawn
    // as linear light, an added model's additive mesh is added whole, its shader having scaled it by
    // its blend once its colors are light.
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
                      Way{.fragment = kWhole,
                          .blend = m_encoding == ColorEncoding::Linear ? added : addedByAlpha,
                          .writesDepth = false}}},
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
            rhi::Binding{.binding = kEnvironmentSamplerBinding, .resource = m_environmentSampler.get()},
            rhi::Binding{.binding = kProbesBinding,
                         .resource = m_probes.isNull() ? m_noProbes.get() : m_probes},
            rhi::Binding{.binding = kSpecularIntegralBinding,
                         .resource = m_specularIntegral.get().isNull() ? m_whiteMask.get()
                                                                       : m_specularIntegral.get()}};
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
        const MipChain texels = hasBaseColor
                                    ? baseColorTexelsOf(*material->baseColor, m_textureGamma, m_encoding)
                                    : skinTexelsOf(texture, m_textureGamma, m_encoding);
        const rhi::Extent2d extent = hasBaseColor
                                         ? extentOf(*material->baseColor)
                                         : rhi::Extent2d{.width = texture.width, .height = texture.height};
        const std::string label = std::format("studio.{}", texture.name);
        auto uploaded = uploadChain(uploader, label, extent, texels, colorFormatOf(m_encoding));
        const SkinUniform skin = skinUniformOf(texture, material);
        auto uniform = uploader.buffer(label, rhi::BufferUsage::Uniform, rhi::BufferState::Uniform,
                                       std::as_bytes(std::span{&skin, 1}));
        auto images = material ? uploadMaterialImages(uploader, label, *material) : MaterialImages{};
        if (!uploaded || !uniform || !images) {
            return gpuFailure(std::move(!uploaded  ? uploaded.error()
                                        : !uniform ? uniform.error()
                                                   : images.error()));
        }
        const auto orWhite = [this](const rhi::OwnedTexture& image) {
            return image.get().isNull() ? m_whiteMask.get() : image.get();
        };
        const std::array bindings{
            rhi::Binding{.binding = kAlbedoBinding, .resource = uploaded->get()},
            rhi::Binding{.binding = kAlbedoSamplerBinding, .resource = m_sampler.get()},
            rhi::Binding{.binding = kSkinUniformBinding,
                         .resource = rhi::BufferBinding{.buffer = uniform->get()}},
            rhi::Binding{.binding = kEmissiveMaskBinding, .resource = orWhite(images->emissiveMask)},
            rhi::Binding{.binding = kSpecularMapBinding, .resource = orWhite(images->specularMap)},
            rhi::Binding{.binding = kPaintedLightBinding, .resource = orWhite(images->paintedLight)}};
        auto group =
            m_device->createBindGroup({.label = label, .layout = m_skinLayout.get(), .bindings = bindings});
        if (!group) {
            return gpuFailure(std::move(group.error()));
        }
        model.m_textures.push_back(StudioSkinDraw{.texture = std::move(*uploaded),
                                                  .emissiveMask = std::move(images->emissiveMask),
                                                  .specularMap = std::move(images->specularMap),
                                                  .paintedLight = std::move(images->paintedLight),
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
        const MipChain chain =
            colorMipChainOf(sky[side].pixels, sky[side].width, sky[side].height, m_encoding);
        levels = narrow<std::uint32_t>(chain.offsets.size());
        for (const std::size_t offset : chain.offsets) {
            offsets.push_back(texels.size() + offset);
        }
        texels.insert(texels.end(), chain.texels.begin(), chain.texels.end());
    }
    Uploader uploader{*m_device, commands};
    auto environment = uploader.texture(
        "studio.environment", {.width = sky.front().width, .height = sky.front().height}, levels,
        narrow<std::uint32_t>(kSkySuffixes.size()), texels, offsets, colorFormatOf(m_encoding));
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

void StudioRenderer::setProbes(rhi::TextureHandle probes, std::uint32_t mipLevels) {
    m_probes = probes;
    m_probeLevels = std::max(mipLevels, 1U);
    // Each frame's group names the probes: made again as each slot is next written.
    for (StudioFrameSlot& slot : m_frameSlots) {
        slot.group = {};
    }
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
                                                   .z = static_cast<float>(m_probeLevels - 1)},
                               .colorEncoding = std::to_underlying(m_encoding),
                               .look = std::to_underlying(m_look)};
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
