// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The world of a BSP map on the GPU (docs/05-rendering.md, section 8): every face of the world and
/// of its brush entities in one vertex and one index buffer, drawn in batches by texture, and the
/// lightmaps packed into one atlas.
///
/// Today: lightstyle 0 of each face, opaque and masked (`{`) textures, from the map or from its
/// WADs, in the original's picture (nx.render:gamma), and the sky. Visibility (PVS and frustum),
/// the four lightstyles, water, animated textures and render modes come next (TODO.md, Rendering).
export module nx.render:world;

import std;
import nx.core;
import nx.math;
import nx.rhi;
import nx.assets;
import :camera;
import :gamma;

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

    /// Draws every face as `camera` sees it on a target of `aspect` (width over height), inside a
    /// render pass with a color target of desc.colorFormat and a Depth32Float depth target cleared
    /// to 0 (reversed depth).
    void draw(rhi::CommandList& commands, const Camera& camera, float aspect);

    [[nodiscard]] inline auto stats() const noexcept -> const WorldStats& { return m_stats; }

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

    /// What one frame in flight writes its uniform to.
    struct FrameSlot {
        rhi::BufferHandle buffer{};
        rhi::BindGroupHandle group{};
    };

    explicit BspWorld(rhi::Device& device) noexcept : m_device{&device} {}
    void release() noexcept;

    rhi::Device* m_device;
    rhi::BufferHandle m_vertices;
    rhi::BufferHandle m_indices;
    std::vector<rhi::TextureHandle> m_textures; ///< Every texture, the atlas and the sky's included.
    rhi::SamplerHandle m_textureSampler;
    rhi::SamplerHandle m_lightmapSampler;
    rhi::SamplerHandle m_skySampler;
    rhi::ShaderHandle m_shader;
    rhi::BindGroupLayoutHandle m_frameLayout;
    rhi::BindGroupLayoutHandle m_textureLayout;
    rhi::RenderPipelineHandle m_pipeline;
    rhi::RenderPipelineHandle m_skyPipeline;
    std::vector<rhi::BindGroupHandle> m_textureGroups;
    std::vector<FrameSlot> m_frameSlots;
    std::vector<Batch> m_batches;
    IndexRange m_skyFaces;
    std::uint64_t m_drawCount = 0;
    float m_lightScale = 0.0F; ///< lightTableScale of the settings it was made with.
    WorldStats m_stats;
};

} // namespace nx::render
