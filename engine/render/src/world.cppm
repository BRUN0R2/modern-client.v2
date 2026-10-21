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
/// the brush entities' render modes, water, and brush entities moved and turned by the game.
export module nx.render:world;

import std;
import nx.core;
import nx.math;
import nx.rhi;
import nx.assets;
import :camera;
import :gamma;
import :lightStyle;

namespace nx::render {

/// How a brush entity is drawn, its `rendermode`, numbered as the original numbers them.
enum class RenderMode : std::uint8_t {
    Normal,       ///< Lit and opaque, as the world.
    TransColor,   ///< Its `rendercolor` instead of its textures, unlit, as opaque as `renderamt` says.
    TransTexture, ///< Its textures, unlit, as opaque as `renderamt` says, its faces back to front.
    Glow,         ///< As TransTexture, its faces unsorted (meant for sprites; any unknown mode too).
    TransAlpha,   ///< "Solid": lit and opaque, its masked textures cut where their alpha is 0.25 or less.
    TransAdd,     ///< Its textures times `renderamt`, unlit, added to what is behind.
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
};

/// What the world holds, for the log and r_speeds.
struct WorldStats {
    std::size_t faces = 0;
    std::size_t triangles = 0;
    std::size_t textures = 0;                   ///< Of the map and of its WADs.
    std::vector<std::string> missingTextures{}; ///< In no WAD: drawn as a checkerboard.
    rhi::Extent2d lightmapAtlas{};
};

/// Where the game puts brush model `model` (1 or more) of the map, and how it turns it about there.
struct ModelPlacement {
    std::uint32_t model = 0;
    Vec3 origin{};
    EulerAngles angles{};
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

    BspWorld(BspWorld&& other) noexcept;
    auto operator=(BspWorld&& other) -> BspWorld& = delete;
    BspWorld(const BspWorld&) = delete;
    auto operator=(const BspWorld&) -> BspWorld& = delete;
    /// Destroys its resources, which the device keeps until the GPU is done with them.
    ~BspWorld();

    /// Draws `view`, inside a render pass with a color target of desc.colorFormat and a Depth32Float
    /// depth target cleared to 0 (reversed depth).
    void draw(rhi::CommandList& commands, const WorldFrame& view);

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

    /// What one frame in flight writes its uniform, its indices and its brush models' uniforms to.
    struct FrameSlot {
        rhi::BufferHandle buffer{};
        rhi::BindGroupHandle group{};
        rhi::BufferHandle indices{};
        rhi::BufferHandle entities{};
        std::vector<rhi::BindGroupHandle> entityGroups{}; ///< Each model's uniform, by its index.
    };

    /// A face of the map: its triangle fan in the vertex buffer, and where its triangles go.
    struct FaceDraw {
        std::uint32_t firstIndex = 0; ///< Its triangles, in m_faceIndices.
        std::uint32_t indexCount = 0; ///< 0: never drawn (triggers and tool textures).
        std::uint32_t bucket = 0;     ///< Its texture's in m_bucketFrames, or kSkyBucket.
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

    /// A brush entity drawn this frame, after the world: its faces that face the camera, a range of
    /// m_entityFaces.
    struct EntityDraw {
        std::uint32_t model = 0; ///< In m_models.
        float distance = 0.0F;   ///< Squared, from the camera to its middle.
        std::uint32_t firstFace = 0;
        std::uint32_t faceCount = 0;
    };

    static constexpr std::uint32_t kSkyBucket = std::numeric_limits<std::uint32_t>::max();

    explicit BspWorld(rhi::Device& device, const assets::BspView& map) noexcept
        : m_device{&device}, m_map{&map} {}
    void release() noexcept;
    /// Where each brush model is drawn this frame, in m_placements: where `placements` puts it, else
    /// where its entity puts it, unturned.
    void place(std::span<const ModelPlacement> placements);
    /// Puts the triangles of what `camera` sees in m_bucketIndices and m_skyIndices, and the brush
    /// entities it sees, where m_placements puts them, in m_entityDraws.
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
    /// The shader, the bind group layouts, the samplers and a pipeline for each way faces are drawn.
    [[nodiscard]] auto createPipelines(std::span<const std::byte> shaderCode, rhi::Format colorFormat)
        -> Result<void, rhi::RhiError>;

    rhi::Device* m_device;
    const assets::BspView* m_map;
    rhi::BufferHandle m_vertices;
    std::vector<rhi::TextureHandle> m_textures; ///< Every texture, the atlas and the sky's included.
    rhi::SamplerHandle m_textureSampler;
    rhi::SamplerHandle m_skySampler;
    rhi::ShaderHandle m_shader;
    rhi::BindGroupLayoutHandle m_frameLayout;
    rhi::BindGroupLayoutHandle m_textureLayout;
    rhi::RenderPipelineHandle m_pipeline;
    rhi::RenderPipelineHandle m_skyPipeline;
    rhi::BindGroupLayoutHandle m_entityLayout;
    rhi::RenderPipelineHandle m_solidPipeline;    ///< TransAlpha.
    rhi::RenderPipelineHandle m_texturePipeline;  ///< TransTexture and Glow.
    rhi::RenderPipelineHandle m_colorPipeline;    ///< TransColor.
    rhi::RenderPipelineHandle m_additivePipeline; ///< TransAdd.
    /// For each bucket of faces, a bind group for each texture it shows in turn: one, unless animated.
    std::vector<std::vector<rhi::BindGroupHandle>> m_bucketFrames;
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
    std::vector<EntityDraw> m_entityDraws;
    std::vector<std::uint32_t> m_entityFaces;
    std::vector<std::uint32_t> m_entityIndices;
    std::vector<IndexRange> m_entityRanges; ///< Of each of m_entityFaces, in m_entityIndices.
    FrameStats m_lastFrame;
};

} // namespace nx::render
