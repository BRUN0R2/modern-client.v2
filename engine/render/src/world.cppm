// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The world of a BSP map on the GPU (docs/05-rendering.md, section 8): every face of the world and
/// of its brush entities in one vertex and one index buffer, drawn in batches by texture, and the
/// lightmaps packed into one atlas.
///
/// Each frame draws the faces that face the camera in the leaves its leaf may see (the potentially
/// visible set) that are in view, and the brush entities in view where the game puts them, as the
/// original culls them.
///
/// Today: the lightmaps of each face's light styles, opaque, masked (`{`) and animated (`+0`...)
/// textures, from the map or from its WADs, in the original's picture (nx.render:gamma), the sky,
/// the brush entities' render modes and alternate textures, water and its waves, brush entities
/// moved and turned by the game, and decals on the world's faces (nx.render:decals).
export module nx.render:world;

import std;
import nx.core;
import nx.math;
import nx.rhi;
import nx.assets;
import :camera;
import :gamma;
import :lightStyle;
import :renderMode;
import :blendedOrder;
import :decals;

namespace nx::render {

// What the world keeps of the map to draw it, which world.cpp builds; not exported.

/// The bucket of the sky's faces, which no texture's bucket is.
inline constexpr std::uint32_t kSkyBucket = std::numeric_limits<std::uint32_t>::max();

/// A face of the map: its triangle fan in the vertex buffer, and where its triangles go.
struct FaceDraw {
    std::uint32_t firstIndex = 0; ///< Its triangles, in BspWorld::m_faceIndices.
    std::uint32_t indexCount = 0; ///< 0: never drawn (triggers and tool textures).
    std::uint32_t bucket = 0;     ///< Its texture's in BspWorld::m_bucketFrames, or kSkyBucket.
    Plane plane{};
    bool isBackSide = false; ///< Drawn from behind its plane.
    Vec3 middle{};           ///< Of its bounds, in its model's space.
};

/// A brush entity's model: its faces, its bounds in its own space, where the map's entity of it
/// puts it, and how it is drawn.
struct ModelDraw {
    std::uint32_t firstFace = 0;
    std::uint32_t faceCount = 0;
    Aabb bounds{};
    Vec3 origin{};
    Vec4 color{}; ///< Its `rendercolor`, and its `renderamt` in w, from 0 to 1.
    RenderMode mode = RenderMode::Normal;
    bool isDrawn = true; ///< False for a mode other than Normal at a `renderamt` of 0.
};

/// The textures of a bucket of faces: each it shows in turn, one unless animated; and those the faces
/// of a brush entity whose frame is not 0 show instead, its alternate sequence, none without one.
struct BucketTextures {
    std::vector<rhi::TextureHandle> frames{};
    std::vector<rhi::TextureHandle> alternates{};
};

/// A bind group for each of a bucket's BucketTextures.
struct BucketFrames {
    std::vector<rhi::OwnedBindGroup> frames{};
    std::vector<rhi::OwnedBindGroup> alternates{};
};

/// Decals of one image that follow one another in the order they are drawn: their triangles, in a
/// frame's decal vertices.
struct DecalRun {
    std::uint32_t texture = 0;
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
};

/// What one frame in flight writes its uniform, its indices, its brush models' uniforms and its
/// decals' triangles to.
struct FrameSlot {
    rhi::OwnedBuffer buffer{};
    rhi::OwnedBindGroup group{};
    rhi::OwnedBuffer indices{};
    rhi::OwnedBuffer entities{};
    std::vector<rhi::OwnedBindGroup> entityGroups{}; ///< Each model's uniform, by its index.
    rhi::OwnedBuffer decalVertices{};                ///< Grown as the decals need.
    std::uint64_t decalCapacity = 0;                 ///< Its bytes.
    std::optional<std::uint64_t> decalRevision{};    ///< Of the decals it holds: none, none yet.
    std::vector<DecalRun> decalRuns{};
};

} // namespace nx::render

export namespace nx::render {

/// Why the world could not be put on the GPU.
enum class WorldError : std::uint8_t {
    GpuFailed, ///< The GPU refused a resource (cause: the RhiError).
    BadSky,    ///< The sky is not six images of one size.
};

/// The sides of a sky, in the order the original loads them: the suffixes of its files,
/// `gfx/env/<skyname><suffix>.tga`.
inline constexpr std::array<std::string_view, 6> kSkySuffixes{"rt", "bk", "lf", "ft", "up", "dn"};

/// The sky of a map whose worldspawn has no `skyname`, and the one the original falls back to.
inline constexpr std::string_view kDefaultSkyName = "desert";

struct BspWorldDesc {
    /// Read every frame for its tree and visibility: it must outlive the world.
    const assets::BspView& map;
    /// The WADs of the map's `wad` key, in its order: a texture without pixels in the map is the
    /// first of them that has its name.
    std::span<const assets::WadView> wads{};
    /// shaders/world.slang, compiled for the device (bin/shaders/world.spv).
    std::span<const std::byte> shaderCode{};
    rhi::Format colorFormat = rhi::Format::Undefined;
    GammaSettings gamma{};
    /// The sides of the sky, in the order of kSkySuffixes, all of one size; none: a gray sky. Drawn as
    /// their files have them: unlike the palettes of the textures, the original puts them through no
    /// gamma curve.
    std::span<const assets::RgbaImage> sky{};
    /// The decals the world can show, by their index (shootDecal's): images of decals.wad, made as
    /// decalImageOf makes them.
    std::span<const assets::MipTexture> decals{};
    /// The most decals on faces the world keeps, its oldest going first (DecalPool::kMostDecals at
    /// most): `r_decals`, which a multiplayer game holds to `mp_decals`.
    std::size_t decalLimit = DecalPool::kMostDecals;
};

/// What the world holds, for the log and r_speeds.
struct WorldStats {
    std::size_t faces = 0;
    std::size_t triangles = 0;
    std::size_t textures = 0;                   ///< Of the map and of its WADs.
    std::vector<std::string> missingTextures{}; ///< In no WAD: drawn as a checkerboard.
    rhi::Extent2d lightmapAtlas{};
};

/// Where the game puts brush model `model` (not the world) of the map, how it turns it about there, how
/// high the waves of its turbulent faces rise, and which of its textures' sequences it shows.
struct ModelPlacement {
    assets::ModelIndex model{};
    Vec3 origin{};
    EulerAngles angles{};
    /// The entity's scale (func_water's an eighth of its "WaveHeight"): its turbulent faces rise and
    /// fall by up to about 29 times it, as the original's EmitWaterPolys raises them; 0 for none.
    float waveHeight = 0.0F;
    /// Its frame is not 0: each of its faces shows its texture's alternate sequence (+a... for +0...,
    /// and back) when there is one (R_TextureAnimation).
    bool showsAlternateTextures = false;
};

/// What a draw shows.
struct WorldFrame {
    Camera camera{};
    float aspect = 1.0F; ///< Of the target: width over height.
    Seconds time{};      ///< Since the map started: animated textures play from it.
    LightStyleValues lightStyles{};
    /// Where the game puts brush models; any other is where the map's entity of it says.
    std::span<const ModelPlacement> placements{};
};

/// What the last draw drew, for r_speeds.
struct FrameStats {
    std::size_t leaves = 0; ///< Potentially visible from the camera's leaf, and in view.
    std::size_t faces = 0;
    std::size_t triangles = 0;
};

class BspWorld {
public:
    /// Builds the world of `desc.map` and records its uploads in `commands`: draw it from the same
    /// frame on. Errors: GpuFailed, BadSky.
    [[nodiscard]] static auto create(rhi::Device& device, rhi::CommandList& commands,
                                     const BspWorldDesc& desc) -> Result<BspWorld, WorldError>;

    BspWorld(BspWorld&& other) noexcept = default;
    auto operator=(BspWorld&& other) -> BspWorld& = delete;
    BspWorld(const BspWorld&) = delete;
    auto operator=(const BspWorld&) -> BspWorld& = delete;
    /// Its resources go with it, which the device keeps until the GPU is done with them.
    ~BspWorld() = default;

    /// Writes what `view` shows for the frame in flight, for drawInOrder to record with the other
    /// renderers of the frame before the next prepare.
    void prepare(const WorldFrame& view);
    /// Draws `view` alone: prepares it, then records it (drawInOrder).
    void draw(rhi::CommandList& commands, const WorldFrame& view);

    /// The brush entities the last prepare found that are drawn blended, the index of each its own.
    [[nodiscard]] inline auto blendedDraws() const noexcept -> std::span<const BlendedDraw> {
        return m_blendedDraws;
    }
    /// Records what the last prepare found of the world, its sky and its solid brush entities, inside a
    /// render pass with a color target of desc.colorFormat and a Depth32Float depth target cleared to 0
    /// (reversed depth).
    void drawSolid(rhi::CommandList& commands) const;
    /// Records the brush entity of `index`, one of blendedDraws()'s, in the same render pass.
    void drawBlended(rhi::CommandList& commands, std::uint32_t index) const;

    /// Stamps decal `decal`, of BspWorldDesc::decals, on the world's faces near `center`, as
    /// DecalPool::shoot does; drawn from the next prepare on, over the faces, lit as they are, after
    /// the world's faces as the original draws them. Returns the faces it was stamped on.
    auto shootDecal(std::uint32_t decal, Vec3 center) -> std::uint32_t;
    /// The world's decals.
    [[nodiscard]] inline auto decals() const noexcept -> const DecalPool& { return *m_decals; }

    [[nodiscard]] inline auto stats() const noexcept -> const WorldStats& { return m_stats; }
    [[nodiscard]] inline auto lastFrame() const noexcept -> const FrameStats& { return m_lastFrame; }

private:
    /// A range of the index buffer.
    struct IndexRange {
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
    };

    /// The faces of one texture.
    struct Batch {
        IndexRange indices{};
        rhi::BindGroupHandle textures{};
    };

    /// A brush entity drawn this frame, with the world or blended: its faces that face the camera, a
    /// range of m_entityFaces.
    struct EntityDraw {
        std::uint32_t model = 0; ///< In m_models.
        float distance = 0.0F;   ///< Squared, from the camera to its middle.
        std::uint32_t firstFace = 0;
        std::uint32_t faceCount = 0;
    };

    inline explicit BspWorld(rhi::Device& device, const assets::BspView& map) noexcept
        : m_device{&device}, m_map{&map} {}
    /// Where each brush model is drawn this frame, in m_placements: where `placements` puts it, else
    /// where its entity puts it, unturned.
    void place(std::span<const ModelPlacement> placements);
    /// Puts the triangles of what `camera` sees in m_bucketIndices and m_skyIndices, the brush entities
    /// it sees, where m_placements puts them, in m_entityDraws, and the blended ones in m_blendedDraws.
    void gather(const Camera& camera, float aspect);
    /// Whether face `face`, drawn, faces `eye` (in its model's space): a face of the world from the
    /// side of its plane the eye is on, the plane itself in front, as the original's
    /// R_RecursiveWorldNode; a brush entity's from at least BACKFACE_EPSILON past it, as its
    /// R_DrawBrushModel.
    [[nodiscard]] auto isSeen(std::uint32_t face, Vec3 eye, bool isOfAnEntity) const noexcept -> bool;
    /// Adds the triangles of face `face` of the world, once a frame, to its bucket when it is seen
    /// from `eye`.
    void addFace(std::uint32_t face, Vec3 eye);
    /// Puts a brush entity, its model placed by `placement`, in m_entityDraws, with its faces seen from
    /// `camera`, in the order the original draws them.
    void addEntity(std::uint32_t model, const Camera& camera, const ModelPlacement& placement);
    [[nodiscard]] auto pipelineOf(RenderMode mode) const noexcept -> rhi::RenderPipelineHandle;
    /// The frame slot the last prepare wrote.
    [[nodiscard]] auto preparedSlot() const noexcept -> const FrameSlot&;
    /// Records the faces of `entity`, with the vertex and index buffers bound, each with its texture's
    /// frame.
    void drawEntity(rhi::CommandList& commands, const EntityDraw& entity) const;
    /// The shader, the bind group layouts and the samplers, then a pipeline for each way faces are
    /// drawn.
    [[nodiscard]] auto createLayouts(std::span<const std::byte> shaderCode) -> Result<void, rhi::RhiError>;
    [[nodiscard]] auto createPipelines(rhi::Format colorFormat) -> Result<void, rhi::RhiError>;
    /// The frames in flight's buffers and bind groups: the frame's uniform, which binds `lightmaps`,
    /// `lightTable` and `sky`; its indices; its brush models' uniforms.
    [[nodiscard]] auto createFrameSlots(rhi::TextureHandle lightmaps, rhi::TextureHandle lightTable,
                                        rhi::TextureHandle sky) -> Result<void, rhi::RhiError>;
    /// A bind group for each texture each bucket of `bucketTextures` shows.
    [[nodiscard]] auto createBucketGroups(std::span<const BucketTextures> bucketTextures)
        -> Result<void, rhi::RhiError>;
    /// A bind group for each of `decals`, sampled clamped at its edges.
    [[nodiscard]] auto createDecalGroups(std::span<const rhi::OwnedTexture> decals)
        -> Result<void, rhi::RhiError>;
    /// Writes the decals' triangles to `slot` when it holds others, growing its buffer as they need.
    /// Errors: those of the buffer's creation.
    [[nodiscard]] auto writeDecals(FrameSlot& slot) -> Result<void, rhi::RhiError>;

    rhi::Device* m_device;
    const assets::BspView* m_map;
    rhi::OwnedBuffer m_vertices;
    std::vector<rhi::OwnedTexture> m_textures; ///< Every texture, the atlas and the sky's included.
    rhi::OwnedSampler m_textureSampler;
    rhi::OwnedSampler m_skySampler;
    rhi::OwnedShader m_shader;
    rhi::OwnedBindGroupLayout m_frameLayout;
    rhi::OwnedBindGroupLayout m_textureLayout;
    rhi::OwnedBindGroupLayout m_entityLayout;
    rhi::OwnedRenderPipeline m_pipeline;
    rhi::OwnedRenderPipeline m_skyPipeline;
    rhi::OwnedRenderPipeline m_solidPipeline;    ///< TransAlpha.
    rhi::OwnedRenderPipeline m_texturePipeline;  ///< TransTexture and Glow.
    rhi::OwnedRenderPipeline m_colorPipeline;    ///< TransColor.
    rhi::OwnedRenderPipeline m_additivePipeline; ///< TransAdd.
    rhi::OwnedRenderPipeline m_decalPipeline;
    rhi::OwnedSampler m_decalSampler;
    std::vector<rhi::OwnedBindGroup> m_decalGroups; ///< Of each decal's image, by its index.
    std::vector<DecalSize> m_decalSizes;
    std::optional<DecalPool> m_decals; ///< Made with the faces, once their lightmaps are placed.
    /// For each bucket of faces, a bind group for each texture it shows in turn, and for each of its
    /// alternate sequence.
    std::vector<BucketFrames> m_bucketFrames;
    std::vector<FrameSlot> m_frameSlots;
    std::vector<FaceDraw> m_faces;            ///< Every face of the map, by its index.
    std::vector<std::uint32_t> m_faceIndices; ///< The triangles of every face, in the vertex buffer.
    std::vector<ModelDraw> m_models;
    Vec4 m_lighting{}; ///< The lightScale of the settings it was made with, and the lightmaps' size.
    WorldStats m_stats;

    // What each frame gathers, kept to reuse the memory.
    std::uint64_t m_drawCount = 0;
    std::vector<std::uint64_t> m_faceGathered; ///< The draw each face was last looked at in.
    std::vector<ModelPlacement> m_placements;  ///< Of each of m_models, this frame.
    std::optional<assets::LeafIndex> m_visibleFrom;
    std::vector<assets::LeafIndex> m_visibleLeaves; ///< Those of m_visibleFrom.
    std::vector<std::vector<std::uint32_t>> m_bucketIndices;
    std::vector<std::uint32_t> m_skyIndices;
    std::vector<Batch> m_batches;
    IndexRange m_sky{};                   ///< The sky's triangles, in the slot's indices.
    std::uint32_t m_firstEntityIndex = 0; ///< Where m_entityIndices start in the slot's indices.
    std::size_t m_animationStep = 0;      ///< The frame of each animated texture shown: this one of them.
    std::vector<EntityDraw> m_entityDraws;
    std::vector<BlendedDraw> m_blendedDraws; ///< Of m_entityDraws, by their index.
    std::vector<std::uint32_t> m_entityFaces;
    std::vector<std::uint32_t> m_entityIndices;
    std::vector<IndexRange> m_entityRanges; ///< Of each of m_entityFaces, in m_entityIndices.
    FrameStats m_lastFrame;
};

} // namespace nx::render
