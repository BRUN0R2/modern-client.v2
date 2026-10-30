// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Studio models on the GPU (docs/05-rendering.md, section 9): each model's meshes, of every choice
/// of every body part, in one vertex and one index buffer, each vertex with the bone of its position
/// and the bone of its normal (MDL v10 binds each to one bone); its textures through `texgamma`, with
/// their mip levels. A frame draws its models posed by the bones the CPU computed (cs::poseBones) and
/// lit as the original lights them (nx.render:light, R_StudioLighting), mesh after mesh, each as its
/// texture says: masked, additive, chrome, flat shaded or fullbright.
///
/// A model drawn other than normally blends as its render mode makes it (GL_StudioSetRenderMode): added,
/// unlit; or blended, lit, as opaque as `renderamt` says. The solid models are drawn with the solid
/// things of the frame, the others among everything it draws blended (nx.render:blendedOrder).
///
/// The enhanced look (docs/adr/0025-enhanced-rendering.md) shades each texture by its surface
/// (StudioMaterial::surface): a metallic-roughness one, glTF 2.0's, lit by the original's light and
/// reflecting the map's probes or sky; a painted one, whose texels hold their light, kept as it is
/// with the reflection its material gives added. The lights near a model give highlights, and what a
/// texture emits goes beyond white, where the bloom spreads it; a texture without a surface draws as in
/// the original.
///
/// Today: the skins and body parts of models, a draw call for each mesh, the render modes, the view's
/// own model kept out of walls, and models drawn mirrored. The render effects (renderfx), the players'
/// colors, the dynamic lights and instancing come with their consumers.
export module nx.render:studio;

import std;
import nx.core;
import nx.math;
import nx.rhi;
import nx.assets;
import :camera;
import :fog;
import :gamma;
import :world;
import :light;
import :renderMode;
import :blendedOrder;
import :probes;

namespace nx::render {

// What the studio renderer keeps of a model and of a frame, which studio.cpp builds; not exported.

/// How a mesh is drawn, from its texture's flags.
enum class MeshKind : std::uint8_t {
    Opaque,
    Masked,   ///< Cut where its texture's alpha is 0.5 or less.
    Additive, ///< Added to what is behind.
    Count,
};

/// How a model's render mode blends it (GL_StudioSetRenderMode).
enum class StudioBlending : std::uint8_t {
    Solid,   ///< Normal: opaque; its additive meshes added, their depth not written.
    Blended, ///< Over what is behind, as opaque as `renderamt` says, its depth written.
    Added,   ///< Unlit, added to what is behind, its depth not written.
    Count,
};

/// Which way a model's triangles face as drawn: as modeled, or turned over by bones that mirror it.
enum class StudioFacing : std::uint8_t {
    AsModeled,
    Mirrored,
    Count,
};

/// A mesh of a choice of a body part: its triangles in its model's index buffer, and its texture's
/// place in each skin.
struct StudioMeshDraw {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t skinReference = 0;
};

/// A texture of a model on the GPU, and the bind group a mesh is drawn with it by.
struct StudioSkinDraw {
    rhi::OwnedTexture texture{};
    rhi::OwnedTexture emissiveMask{}; ///< Its material's; none: the renderer's white.
    rhi::OwnedTexture specularMap{};  ///< Its painted surface's; none: the renderer's white.
    rhi::OwnedTexture paintedLight{}; ///< Its painted surface's light's map; none: the renderer's white.
    rhi::OwnedBuffer uniform{};
    rhi::OwnedBindGroup group{};
    MeshKind kind = MeshKind::Opaque;
};

/// A mesh a frame draws: its triangles in its model's index buffer, and its texture's bind group.
struct StudioMeshCall {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    MeshKind kind = MeshKind::Opaque;
    rhi::BindGroupHandle texture{};
};

/// A model a frame draws: its buffers, how it blends, and its meshes, a range of the frame's
/// StudioMeshCalls.
struct StudioModelCall {
    rhi::BufferHandle vertices{};
    rhi::BufferHandle indices{};
    StudioBlending blending = StudioBlending::Solid;
    StudioFacing facing = StudioFacing::AsModeled;
    std::uint32_t firstMesh = 0;
    std::uint32_t meshCount = 0;
};

/// What one frame in flight writes the view, its models' bones and their lights to.
struct StudioFrameSlot {
    rhi::OwnedBuffer frame{};
    rhi::OwnedBuffer bones{};     ///< Every model's, one after another.
    std::size_t boneCapacity = 0; ///< The bones it holds.
    rhi::OwnedBindGroup group{};  ///< The frame's uniform, the light curve and the bones.
    rhi::OwnedBuffer instances{}; ///< Each model's uniform.
    std::vector<rhi::OwnedBindGroup> instanceGroups{}; ///< Each model's uniform, by its place.
};

} // namespace nx::render

export namespace nx::render {

/// Why a studio model, or what draws them, could not be put on the GPU.
enum class StudioError : std::uint8_t {
    GpuFailed,   ///< The GPU refused a resource (cause: the RhiError).
    BadTextures, ///< The textures given lack what a mesh is drawn with (cause: the MdlError).
};

/// How studio models are shaded.
enum class StudioLook : std::uint8_t {
    Legacy,   ///< As the original shades them.
    Enhanced, ///< By their materials (ADR-0025), to a target that keeps light beyond 1.
    Count,
};

/// The most lights near a model it lights itself by, in the enhanced look.
inline constexpr std::size_t kMostStudioLights = 4;

/// What a studio renderer draws with (StudioRenderer::create): its shader, its target's format, the
/// original's picture, its look, and how its meshes are blended when not as their flags say.
struct StudioRendererDesc {
    /// shaders/studio.slang, compiled for the device (bin/shaders/studio.spv).
    std::span<const std::byte> shaderCode{};
    rhi::Format colorFormat = rhi::Format::Undefined;
    GammaSettings gamma{};
    StudioLook look = StudioLook::Legacy;
    /// How its colors reach the target it draws to: as the original draws them, or as linear light.
    ColorEncoding encoding = ColorEncoding::Display;
};

/// How a texture's meshes are drawn: opaque, cut where its alpha is 0.5 or less, or added to what is
/// behind; its flags' way without a material that says.
enum class StudioBlend : std::uint8_t {
    Opaque,
    Masked,
    Additive,
    Count,
};

/// A physically based surface, glTF 2.0's metallic-roughness: its texels are its color, a dielectric's
/// lit by the original's light, a metal's what it reflects.
struct StudioMetallicRoughness {
    float metallic = 0.0F;  ///< From 0, a dielectric, to 1, a metal.
    float roughness = 1.0F; ///< From 0, a mirror, to 1.
};

/// The light painted for a painted surface by the direction it reflects, all around: an image,
/// equirectangular (across, the longitude atan2(y, x) / 2π + 1/2; down, the latitude acos(z) / π), of
/// display colors as the original's textures are, times `gain`.
struct StudioPaintedLight {
    assets::RgbaImage map{};
    float gain = 1.0F; ///< Its scale, in linear light.
};

/// A surface whose texels are a painting with its light painted in, as Counter-Strike Online paints
/// its weapons: the painting stays, lit by the original's light and by its painted light, and a
/// reflection is added over it as its material gives it, an artist's layer, not a conductor's.
struct StudioPainted {
    float roughness = 1.0F; ///< Of its reflection, from 0, a mirror, to 1.
    /// Its reflection's color head on (F0), in linear light.
    Vec3 specularColor{.x = 0.04F, .y = 0.04F, .z = 0.04F};
    /// How much of its reflection is added, grazing angles included, from 0, none, to 1.
    float specularWeight = 1.0F;
    /// Its reflection by texel: rgb its color head on in place of specularColor, alpha times
    /// specularWeight; data, through neither `texgamma` nor sRGB.
    std::optional<assets::RgbaImage> specularMap{};
    /// The light painted for it by direction; none: the original's light alone.
    std::optional<StudioPaintedLight> light{};
};

/// How the enhanced look shades a texture.
using StudioSurface = std::variant<StudioMetallicRoughness, StudioPainted>;

/// What a texture of a model is made of: in the enhanced look, its surface and what it emits; in both,
/// its texels and the way it is drawn, where they replace the model's own.
struct StudioMaterial {
    /// How the enhanced look shades it; none: as the original shades it.
    std::optional<StudioSurface> surface{};
    /// How bright what it emits is: its color times this, where emissiveMask says; 1 as bright as white,
    /// beyond 1 it blooms. GoldSrc's fullbright lights a whole texture as 1 does; this lights a part.
    float emissive = 0.0F;
    /// Where it emits and how much, by its texels, of the texture's size or any other; none: everywhere.
    std::optional<assets::RgbaImage> emissiveMask{};
    /// Whether the original's light reaches it, in place of its Fullbright flag; none: as its flags say.
    std::optional<bool> isFullbright{};
    /// Its texels, of any size, in place of the model's; none: the model's.
    std::optional<assets::RgbaImage> baseColor{};
    /// The size its triangles' texture coordinates count in, when not the model's texture's: Counter-
    /// Strike Online's textures named #WWWHHH, whose pixels in the model only stand in.
    std::optional<rhi::Extent2d> coordinateSize{};
    /// How its meshes are drawn, in place of its flags' way.
    std::optional<StudioBlend> blend{};
};

/// A studio model to put on the GPU (StudioRenderer::load): its file, the file of its textures, and
/// the materials of its textures in the enhanced look.
struct StudioModelDesc {
    const assets::MdlView& model;
    /// Where its textures and skins are: the model itself, or its `*T.mdl` (assets::texturesFilePath).
    const assets::MdlView& textures;
    /// The material of each texture, by its index, in the enhanced look; one it lacks has none.
    std::span<const std::optional<StudioMaterial>> materials{};
};

/// A light near a model, which gives it highlights in the enhanced look.
struct StudioLight {
    Vec3 position{};
    float reach = 0.0F; ///< How far its light goes, in units.
    Vec3 color{};       ///< Its light at a unit's distance, in linear light.
};

/// A studio model on the GPU, which a StudioRenderer loads and draws.
class StudioModel {
public:
    StudioModel(StudioModel&& other) noexcept = default;
    auto operator=(StudioModel&& other) noexcept -> StudioModel& = default;
    StudioModel(const StudioModel&) = delete;
    auto operator=(const StudioModel&) -> StudioModel& = delete;
    /// Its resources go with it, which the device keeps until the GPU is done with them.
    ~StudioModel() = default;

    /// Its bones: as many as an instance of it gives.
    [[nodiscard]] inline auto boneCount() const noexcept -> std::size_t { return m_boneCount; }

private:
    friend class StudioRenderer;
    StudioModel() = default;

    rhi::OwnedBuffer m_vertices;
    rhi::OwnedBuffer m_indices;
    std::vector<StudioSkinDraw> m_textures; ///< By the index of its texture.
    std::vector<std::vector<assets::StudioTextureIndex>> m_skins;
    /// The meshes of each choice of each body part.
    std::vector<std::vector<std::vector<StudioMeshDraw>>> m_bodyParts;
    std::size_t m_boneCount = 0;
    Vec3 m_middle{}; ///< Of the bounds its header gives, in its entity's space.
};

/// A model drawn in a frame.
struct StudioInstance {
    const StudioModel& model;
    /// Each of its bones' transform from the bone's space to the world, by its index: cs::poseBones's.
    std::span<const Mat3x4> bones{};
    ModelLight light{};
    std::uint32_t skin = 0; ///< Its skin; one the model does not have is its first.
    /// The choice of each body part, by its index; those it leaves out are their first.
    std::span<const std::uint32_t> bodyChoices{};
    DepthLayer depth = DepthLayer::World;
    /// Whether its bones mirror its model (a view model held in the other hand than it was modeled
    /// in): its triangles face the other way, and the other way is culled.
    bool isMirrored = false;
    /// Where its entity is: a blended model is sorted by the middle of its bounds there.
    Vec3 origin{};
    RenderMode mode = RenderMode::Normal;
    std::uint8_t amount = 0; ///< `renderamt`: how opaque a mode other than Normal draws it.
    /// The lights near it, nearest first, which give it highlights in the enhanced look; the first
    /// kMostStudioLights count.
    std::span<const StudioLight> lights{};
    /// The reflection probes it reflects in the enhanced look, of those setProbes gave (ReflectionProbes::
    /// blendAt); none: the sky.
    std::optional<ProbeBlend> probes{};
};

/// What a draw shows.
struct StudioFrame {
    Camera camera{};
    float aspect = 1.0F; ///< Of the target: width over height.
    std::span<const StudioInstance> instances{};
    /// The fog the models are seen through, but for what they add; none, clear (nx.render:fog).
    std::optional<Fog> fog{};
};

/// What draws studio models: their shader, pipelines and samplers, the original's light curve, and
/// what each frame in flight writes.
class StudioRenderer {
public:
    /// Records the upload of the light curve in `commands`. Errors: GpuFailed.
    [[nodiscard]] static auto create(rhi::Device& device, rhi::CommandList& commands,
                                     const StudioRendererDesc& desc) -> Result<StudioRenderer, StudioError>;

    StudioRenderer(StudioRenderer&& other) noexcept = default;
    auto operator=(StudioRenderer&& other) -> StudioRenderer& = delete;
    StudioRenderer(const StudioRenderer&) = delete;
    auto operator=(const StudioRenderer&) -> StudioRenderer& = delete;
    /// Its resources go with it, which the device keeps until the GPU is done with them.
    ~StudioRenderer() = default;

    /// Puts `desc.model` on the GPU, recording its uploads in `commands`: draw it from the same frame
    /// on. Errors: BadTextures, GpuFailed.
    [[nodiscard]] auto load(rhi::CommandList& commands, const StudioModelDesc& desc)
        -> Result<StudioModel, StudioError>;

    /// The sky the enhanced look's metals reflect, its uploads recorded in `commands`: its sides in the
    /// order of kSkySuffixes, all of one size, as BspWorldDesc::sky has them; none: nothing reflected.
    /// Errors: BadTextures (sides that are not six of one size), GpuFailed.
    [[nodiscard]] auto setEnvironment(rhi::CommandList& commands, std::span<const assets::RgbaImage> sky)
        -> Result<void, StudioError>;
    /// The reflection probes the enhanced look's models reflect (nx.render:probes): `probes`, which
    /// must outlive the renderer, of `mipLevels` levels, sampled in ShaderRead. RGB is linear radiance.
    void setProbes(rhi::TextureHandle probes, std::uint32_t mipLevels);

    /// Writes `frame`'s models for the frame in flight, for drawInOrder to record with the other
    /// renderers of the frame before the next prepare; the models must last until then. Each instance
    /// gives its model's every bone. Errors: GpuFailed (room for more bones or models).
    [[nodiscard]] auto prepare(const StudioFrame& frame) -> Result<void, StudioError>;
    /// Draws `frame`'s models alone: prepares them, then records them (drawInOrder). Errors: those of
    /// prepare.
    [[nodiscard]] auto draw(rhi::CommandList& commands, const StudioFrame& frame)
        -> Result<void, StudioError>;

    /// The models the last prepare found that are drawn blended, the index of each its own.
    [[nodiscard]] inline auto blendedDraws() const noexcept -> std::span<const BlendedDraw> {
        return m_blendedDraws;
    }
    /// Records the solid models of the last prepare in their order, inside a render pass with a color
    /// target of desc.colorFormat and a Depth32Float depth target (reversed depth), after the world.
    void drawSolid(rhi::CommandList& commands) const;
    /// Records the model of `index`, one of blendedDraws()'s, in the same render pass.
    void drawBlended(rhi::CommandList& commands, std::uint32_t index) const;

private:
    inline explicit StudioRenderer(rhi::Device& device) noexcept : m_device{&device} {}
    /// The shader, the bind group layouts, the samplers and the pipelines.
    [[nodiscard]] auto createPipelines(std::span<const std::byte> shaderCode, rhi::Format colorFormat,
                                       StudioLook look) -> Result<void, rhi::RhiError>;
    /// Makes room in `slot` for `bones` bones and `instances` models, at least.
    [[nodiscard]] auto reserve(StudioFrameSlot& slot, std::size_t bones, std::size_t instances)
        -> Result<void, rhi::RhiError>;
    /// What `instance` draws, blended as `blending` says: its meshes put in m_meshCalls.
    [[nodiscard]] auto callOf(const StudioInstance& instance, StudioBlending blending) -> StudioModelCall;
    /// The frame slot the last prepare wrote.
    [[nodiscard]] auto preparedSlot() const noexcept -> const StudioFrameSlot&;
    /// Records the model of m_modelCalls' `index`.
    void drawModel(rhi::CommandList& commands, std::size_t index) const;

    rhi::Device* m_device;
    ChannelTable m_textureGamma{};
    rhi::OwnedShader m_shader;
    rhi::OwnedBindGroupLayout m_frameLayout;
    rhi::OwnedBindGroupLayout m_skinLayout;
    rhi::OwnedBindGroupLayout m_instanceLayout;
    rhi::OwnedSampler m_sampler;
    rhi::OwnedSampler m_environmentSampler;
    EnumArray<StudioFacing, EnumArray<StudioBlending, EnumArray<MeshKind, rhi::OwnedRenderPipeline>>>
        m_pipelines;
    rhi::OwnedTexture m_whiteMask; ///< The images of a material without them.
    /// GGX's reflection integrated (shaders/studio.slang's specularIntegral), for the enhanced look.
    rhi::OwnedTexture m_specularIntegral;
    rhi::OwnedTexture m_environment; ///< The sky's sides with their mip levels; one black texel without.
    std::uint32_t m_environmentLevels = 1;
    bool m_hasEnvironment = false;
    StudioLook m_look = StudioLook::Legacy;
    ColorEncoding m_encoding = ColorEncoding::Display;
    rhi::OwnedTexture m_noProbes;  ///< Black sides: what the probes' binding holds without probes.
    rhi::TextureHandle m_probes{}; ///< setProbes's; none: m_noProbes.
    std::uint32_t m_probeLevels = 1;
    rhi::OwnedBuffer m_lightGamma;
    std::vector<StudioFrameSlot> m_frameSlots;
    std::uint64_t m_drawCount = 0;

    // What the last prepare found, kept to reuse the memory.
    std::vector<StudioModelCall> m_modelCalls; ///< By the place of their uniform.
    std::vector<StudioMeshCall> m_meshCalls;
    std::vector<BlendedDraw> m_blendedDraws; ///< Of m_modelCalls, by their index.
};

} // namespace nx::render
