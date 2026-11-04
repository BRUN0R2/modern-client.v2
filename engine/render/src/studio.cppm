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
/// Today: the skins and body parts of models, a draw call for each mesh of those drawn alike, the render
/// modes, the view's own model kept out of walls, models drawn mirrored, and crowds the GPU animates, culls
/// and draws (docs/adr/0032-gpu-crowds.md). The render effects (renderfx), the players' colors and the
/// dynamic lights come with their consumers.
export module nx.render:studio;

import std;
import nx.core;
import nx.math;
import nx.jobs;
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

/// The models a frame draws alike: their buffers, how they blend, their meshes, a range of the frame's
/// StudioMeshCalls, and the instances they are, a range of those of the frame.
struct StudioModelCall {
    rhi::BufferHandle vertices{};
    rhi::BufferHandle indices{};
    StudioBlending blending = StudioBlending::Solid;
    StudioFacing facing = StudioFacing::AsModeled;
    std::uint32_t firstMesh = 0;
    std::uint32_t meshCount = 0;
    std::uint32_t firstInstance = 0;
    std::uint32_t instanceCount = 0;
};

/// Where an instance of a frame's goes among those of the frame: its call, its place, and its bones'.
struct StudioPlace {
    std::uint32_t call = 0;
    std::uint32_t instance = 0;
    std::uint32_t firstBone = 0;
    bool isDrawn = false;
};

/// What one frame in flight tells the GPU of the crowd: the bodies, written once a tick, and what they are
/// drawn by, each buffer as long as it has had to be.
struct StudioCrowdSlot {
    rhi::OwnedBuffer frame{};
    rhi::OwnedBuffer bodies{};
    std::size_t bodyCapacity = 0;
    rhi::OwnedBuffer models{};
    std::size_t modelCapacity = 0;
    rhi::OwnedBuffer templates{};
    std::size_t templateCapacity = 0;
    rhi::OwnedBuffer visible{};
    std::size_t visibleCapacity = 0;
    rhi::TrackedBindGroup group{}; ///< The tables, the crowd's buffers and these.
    std::uint64_t tick = 0;        ///< Whose bodies it holds.
    bool hasBodies = false;
    std::vector<std::uint32_t> bodiesOfModel{};
};

/// What a frame's models are drawn by: the bindings of the frame's bind group layout.
inline constexpr std::size_t kStudioFrameBindings = 8;

/// What one frame in flight writes the view, its models' bones and their lights to.
struct StudioFrameSlot {
    rhi::OwnedBuffer frame{};
    rhi::OwnedBuffer bones{};           ///< Every model's, one after another.
    std::size_t boneCapacity = 0;       ///< The bones it holds.
    rhi::TrackedBindGroup group{};      ///< The frame's uniform, the light curve, the bones and the models.
    rhi::OwnedBuffer instances{};       ///< Every model's light, and where its bones start.
    std::size_t instanceCapacity = 0;   ///< The models it holds.
    rhi::TrackedBindGroup crowdGroup{}; ///< As `group`, the bones and the models the crowd's.
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
    /// What a crowd's GPU work runs (shaders/crowd.slang); none, it draws no crowd.
    std::span<const std::byte> crowdShaderCode{};
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
    /// Borrowed for loading; the view must stay alive through the call. Temporaries are rejected.
    std::reference_wrapper<const assets::MdlView> model;
    /// Where its textures and skins are: the model itself, or its `*T.mdl` (assets::texturesFilePath).
    /// The view must stay alive through loading; temporaries are rejected.
    std::reference_wrapper<const assets::MdlView> textures;
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

/// A model of the crowd: what it is drawn with, and where what it plays is in the crowd's tables.
struct StudioCrowdModel {
    const StudioModel* model = nullptr;
    std::uint32_t boneCount = 0;
    std::uint32_t firstSequence = 0;
    std::uint32_t firstParent = 0;
};

/// A model drawn in a frame.
struct StudioInstance {
    /// Borrowed through the draw; the model must outlive the instance. Temporaries are rejected.
    std::reference_wrapper<const StudioModel> model;
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

/// A bone in a frame of a sequence, in its parent's space, as shaders/crowd.slang reads it.
struct CrowdBonePose {
    Vec4 position{};
    Vec4 rotation{}; ///< A quaternion: x, y, z, w.
};

/// A sequence of a model of a crowd, as cs::poseBones would play it: its frames' poses, each frame's bones
/// one after another, how it plays, and what it may show in about a body.
struct CrowdSequence {
    std::span<const CrowdBonePose> poses{};
    std::uint32_t frameCount = 1;
    float fps = 0.0F;
    bool isLooping = false;
    float reach = 0.0F;  ///< Across, each way.
    float lowest = 0.0F; ///< Up from the body's origin, less than none below it.
    float highest = 0.0F;
};

/// A model for a crowd to be of, and what it plays (StudioRenderer::loadCrowdModel). Its parents go before
/// their children, as a studio model's do.
struct CrowdModelDesc {
    /// Borrowed for as long as the renderer draws it. Temporaries are rejected.
    std::reference_wrapper<const StudioModel> model;
    std::span<const std::int32_t> parents{}; ///< Of each bone; less than none, it has none.
    std::span<const CrowdSequence> sequences{};
};

/// A body of a crowd as its last move left it, as shaders/crowd.slang reads it.
struct alignas(16) CrowdBody {
    Vec3 previousOrigin{}; ///< Where its last move started.
    float yaw = 0.0F;      ///< About its height, in radians.
    Vec3 origin{};
    std::uint32_t model = 0; ///< As loadCrowdModel numbers them.
    std::uint32_t sequence = 0;
    float frame = 0.0F;       ///< Where its server left it, in 256ths of the sequence (pev->frame).
    float advancedAgo = 0.0F; ///< How long before the last tick its server left it there, in seconds.
    float framerate = 1.0F;
    Vec3 lightDirection{.z = -1.0F};
    float ambient = 0.0F;
    Vec3 lightColor{.x = 1.0F, .y = 1.0F, .z = 1.0F};
    float shade = 0.0F;
    float moveShare = 0.0F;        ///< How far from where it started it shows at the last tick, 0 to 1.
    float moveSharePerTick = 1.0F; ///< How much farther it shows a tick later.
};

/// Where a frame's bodies of a crowd go: mapped memory, to write in order and not to read; and how many of
/// them are of each model. Already holding them when `isWritten`.
struct CrowdRoom {
    std::span<CrowdBody> bodies{};
    std::span<std::uint32_t> bodiesOfModel{};
    bool isWritten = false;
};

/// What the GPU works a frame's crowd out for: the view it may show in, and the moment.
struct CrowdFrame {
    std::span<const Plane, kFrustumPlanes> frustum;
    const assets::LeafSet* visibleLeaves = nullptr; ///< None: only the frustum hides a body.
    float between = 1.0F;       ///< How far the moment shown is from a tick to the next, 0 to 1.
    float sinceLastTick = 0.0F; ///< How long the moment shown is past the last tick, in seconds.
};

/// What a draw shows.
struct StudioFrame {
    Camera camera{};
    float aspect = 1.0F; ///< Of the target: width over height.
    std::span<const StudioInstance> instances{};
    /// The fog the models are seen through, but for what they add; none, clear (nx.render:fog).
    std::optional<Fog> fog{};
    /// Who writes the models for the GPU along with the calling thread; none, that thread alone.
    jobs::JobSystem* jobs = nullptr;
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

    /// Whether it was made with the crowd's shader.
    [[nodiscard]] inline auto drawsCrowds() const noexcept -> bool { return !m_crowdSlots.empty(); }
    /// Makes `model` one a crowd may be of, the next of the numbers CrowdBody::model counts, its tables'
    /// uploads recorded in `commands`. Errors: GpuFailed.
    [[nodiscard]] auto loadCrowdModel(rhi::CommandList& commands, const CrowdModelDesc& desc)
        -> Result<void, StudioError>;
    /// The tree of `map`, which hides what its visible leaves do not touch, its upload recorded in
    /// `commands`. Errors: GpuFailed.
    [[nodiscard]] auto setCrowdWorld(rhi::CommandList& commands, const assets::BspView& map)
        -> Result<void, StudioError>;
    /// The room of this frame's crowd, of `bodies` at most, the bodies of tick `tick`. Call once a frame,
    /// before recordCrowd. Errors: GpuFailed.
    [[nodiscard]] auto crowdRoom(std::size_t bodies, std::uint64_t tick) -> Result<CrowdRoom, StudioError>;
    /// Records the GPU's work on this frame's crowd, as many as the room's bodiesOfModel count, outside a
    /// render pass and before the frame's drawSolid, which draws them. Errors: GpuFailed.
    [[nodiscard]] auto recordCrowd(rhi::CommandList& commands, const CrowdFrame& frame)
        -> Result<void, StudioError>;
    /// How many draws the crowd of the last recordCrowd takes: each mesh of each of its models, one, however
    /// many bodies they are.
    [[nodiscard]] inline auto crowdDrawCount() const noexcept -> std::size_t {
        return m_crowdMeshCalls.size();
    }

    /// Writes `frame`'s models for the frame in flight, for drawInOrder to record with the other
    /// renderers of the frame before the next prepare; the models must last until then. Each instance
    /// gives its model's every bone. Errors: GpuFailed (room for more bones or models).
    [[nodiscard]] auto prepare(const StudioFrame& frame) -> Result<void, StudioError>;
    /// Draws `frame`'s models alone: prepares them, then records them (drawInOrder). Errors: those of
    /// prepare.
    [[nodiscard]] auto draw(rhi::CommandList& commands, const StudioFrame& frame)
        -> Result<void, StudioError>;

    /// How many calls the last prepare's models take: those drawn alike, solid, of one model, skin, body and
    /// facing, take one, however many they are (docs/05, section 9.3).
    [[nodiscard]] inline auto modelCallCount() const noexcept -> std::size_t { return m_modelCalls.size(); }
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
    /// Makes room in `slot` for `bones` bones and `instances` models, at least, and its group names all it
    /// has.
    [[nodiscard]] auto reserve(StudioFrameSlot& slot, std::size_t bones, std::size_t instances)
        -> Result<void, rhi::RhiError>;
    /// What models are drawn by this frame: `bones` and `instances`, and the rest as they are now.
    [[nodiscard]] auto frameBindingsOf(const StudioFrameSlot& slot, rhi::BufferHandle bones,
                                       rhi::BufferHandle instances) const
        -> std::array<rhi::Binding, kStudioFrameBindings>;
    /// What `instance` draws, blended as `blending` says: its meshes put in `meshes`.
    [[nodiscard]] static auto callOf(const StudioInstance& instance, StudioBlending blending,
                                     std::vector<StudioMeshCall>& meshes) -> StudioModelCall;
    /// The crowd's compute pipelines.
    [[nodiscard]] auto createCrowdPipelines(std::span<const std::byte> shaderCode)
        -> Result<void, rhi::RhiError>;
    /// Makes room in the crowd's buffers and slot for this frame.
    [[nodiscard]] auto reserveCrowd(StudioCrowdSlot& slot, std::size_t instances, std::size_t bones,
                                    std::size_t draws) -> Result<void, rhi::RhiError>;
    /// `slot`'s group for working the crowd out names the tables, the crowd's buffers and `slot`'s as they
    /// are.
    [[nodiscard]] auto bindCrowd(StudioCrowdSlot& slot) -> Result<void, rhi::RhiError>;
    /// `slot`'s group for drawing the crowd names the crowd's buffers as they are.
    [[nodiscard]] auto reserveCrowdGroup(StudioFrameSlot& slot) -> Result<void, rhi::RhiError>;
    /// The crowd's draws of the last recordCrowd, inside a render pass.
    void drawCrowd(rhi::CommandList& commands) const;
    /// Of m_modelCalls, the one `instance`, solid, is drawn in, made if none draws one alike.
    [[nodiscard]] auto solidCallOf(const StudioInstance& instance) -> std::uint32_t;
    /// Finds where each of `instances` goes (m_places), and the calls they take.
    void placeInstances(const StudioFrame& frame);
    /// The frame slot the last prepare wrote.
    [[nodiscard]] auto preparedSlot() const noexcept -> const StudioFrameSlot&;
    /// Records the model of m_modelCalls' `index`.
    void drawModel(rhi::CommandList& commands, std::size_t index) const;

    rhi::Device* m_device;
    ChannelTable m_textureGamma{};
    rhi::OwnedShader m_shader;
    rhi::OwnedBindGroupLayout m_frameLayout;
    rhi::OwnedBindGroupLayout m_skinLayout;
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
    std::vector<StudioModelCall> m_modelCalls;
    std::vector<StudioMeshCall> m_meshCalls;
    std::vector<BlendedDraw> m_blendedDraws; ///< Of m_modelCalls, by their index.
    std::vector<StudioPlace> m_places;       ///< Of the frame's instances, by their index.
    /// The solid ones of m_modelCalls, by their index, and the first instance each draws.
    std::vector<std::pair<std::uint32_t, const StudioInstance*>> m_solidCalls;

    // The crowd the GPU animates (docs/adr/0032-gpu-crowds.md): what runs, what it plays, once on the GPU,
    // and what a frame's work writes there, which only the GPU reads.
    rhi::OwnedShader m_crowdShader;
    rhi::OwnedBindGroupLayout m_crowdLayout;
    rhi::OwnedComputePipeline m_crowdReset;
    rhi::OwnedComputePipeline m_crowdPose;
    rhi::OwnedComputePipeline m_crowdDraws;
    std::vector<StudioCrowdModel> m_crowdModels;
    std::vector<CrowdBonePose> m_crowdPoseTable;
    std::vector<std::byte> m_crowdSequenceTable;
    std::vector<std::int32_t> m_crowdParentTable;
    rhi::OwnedBuffer m_crowdPoses;
    rhi::OwnedBuffer m_crowdSequences;
    rhi::OwnedBuffer m_crowdParents;
    rhi::OwnedBuffer m_crowdNodes;
    std::uint32_t m_crowdRootNode = 0;
    bool m_hasCrowdTree = false;
    rhi::OwnedBuffer m_crowdBones;
    std::size_t m_crowdBoneCapacity = 0;
    rhi::OwnedBuffer m_crowdInstances;
    std::size_t m_crowdInstanceCapacity = 0;
    rhi::OwnedBuffer m_crowdCounts;
    std::size_t m_crowdCountCapacity = 0;
    rhi::OwnedBuffer m_crowdDrawArguments;
    std::size_t m_crowdDrawCapacity = 0;
    bool areCrowdBuffersNew = true;
    std::vector<StudioCrowdSlot> m_crowdSlots;
    std::uint64_t m_crowdFrames = 0;
    std::vector<StudioModelCall> m_crowdCalls;
    std::vector<StudioMeshCall> m_crowdMeshCalls;
};

} // namespace nx::render
