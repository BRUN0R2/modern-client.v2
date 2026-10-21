// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Sprites on the GPU (docs/05-rendering.md, section 10), drawn as the original's R_DrawSpriteModel
/// draws them, as Xash3D-FWGS reconstructs it (GPL, consulted: docs/20 section 3): each image of a
/// sprite a texture, its palette as its format makes it, with its mip levels; each sprite a quad turned
/// as its orientation says, its frame's image, or the image of a group its time gives, times the
/// entity's color, as opaque or as added as its render mode makes it. Glowing sprites show over
/// everything, dimmer and larger the farther they are, unless the game hid them; alpha-tested ones
/// drawn opaque enough are lit by the world's light at their origin.
///
/// Today: the sprites of the map's entities, drawn after the world and the studio models, the solid
/// ones first and the others from the farthest. The render effects but NoDissipation, sprites that
/// follow an entity and the HUD's come with their consumers.
export module nx.render:sprite;

import std;
import nx.core;
import nx.math;
import nx.rhi;
import nx.assets;
import :camera;
import :gamma;
import :light;
import :lightStyle;
import :renderMode;

namespace nx::render {

// What the sprite renderer keeps of a sprite and of a frame, which sprite.cpp builds; not exported.

/// An image of a sprite on the GPU, and its extent about the sprite's origin, in texels: left and down
/// negative where the image reaches past the origin that way.
struct SpriteImageDraw {
    rhi::OwnedTexture texture{};
    rhi::OwnedBindGroup group{};
    float left = 0.0F;
    float right = 0.0F;
    float up = 0.0F;
    float down = 0.0F;
};

/// A frame of a sprite: its image, or the images of its group, each shown until its interval of the
/// group's loop has passed.
struct SpriteFrameDraw {
    std::uint32_t firstImage = 0;
    std::uint32_t imageCount = 0;
    std::vector<float> intervals{}; ///< In seconds, rising; empty for one image.
};

/// What one frame in flight writes its view and its sprites' quads to.
struct SpriteFrameSlot {
    rhi::OwnedBuffer frame{};
    rhi::OwnedBindGroup group{};
    rhi::OwnedBuffer vertices{};
    std::size_t quadCapacity = 0;
};

} // namespace nx::render

export namespace nx::render {

/// Why a sprite, or what draws them, could not be put on the GPU.
enum class SpriteError : std::uint8_t {
    GpuFailed, ///< The GPU refused a resource (cause: the RhiError).
};

struct SpriteRendererDesc {
    /// shaders/sprite.slang, compiled for the device (bin/shaders/sprite.spv).
    std::span<const std::byte> shaderCode{};
    rhi::Format colorFormat = rhi::Format::Undefined;
    GammaSettings gamma{};
};

/// A sprite on the GPU, which a SpriteRenderer loads and draws.
class SpriteModel {
public:
    SpriteModel(SpriteModel&& other) noexcept = default;
    auto operator=(SpriteModel&& other) noexcept -> SpriteModel& = default;
    SpriteModel(const SpriteModel&) = delete;
    auto operator=(const SpriteModel&) -> SpriteModel& = delete;
    /// Its resources go with it, which the device keeps until the GPU is done with them.
    ~SpriteModel() = default;

    /// Its frames: what an entity's `frame` counts.
    [[nodiscard]] inline auto frameCount() const noexcept -> std::size_t { return m_frames.size(); }

private:
    friend class SpriteRenderer;
    SpriteModel() = default;

    assets::SpriteOrientation m_orientation = assets::SpriteOrientation::ParallelUpright;
    assets::SpriteRendering m_rendering = assets::SpriteRendering::Normal;
    std::vector<SpriteImageDraw> m_images;
    std::vector<SpriteFrameDraw> m_frames;
    /// Its bounds about its origin: half its widest image across, half its tallest up and down.
    float m_halfWidth = 0.0F;
    float m_halfHeight = 0.0F;
};

/// A sprite drawn in a frame: where its entity is, and how the game asks it to be drawn.
struct SpriteInstance {
    const SpriteModel& model;
    Vec3 origin{};
    EulerAngles angles{}; ///< Turn an oriented sprite; the roll turns a parallel one.
    float frame = 0.0F;   ///< The entity's `frame`: its whole part is the frame shown.
    float scale = 0.0F;   ///< 0 is 1.
    RenderMode mode = RenderMode::Normal;
    RenderEffect effect = RenderEffect::None;
    std::uint8_t amount = 0; ///< `renderamt`.
    RenderColor color{};     ///< `rendercolor`; black is white, as the original takes it.
    /// A glowing sprite the view's line to stops 8 units or more short of (the original's trace,
    /// through all but glass and models), which the game finds.
    bool isHidden = false;
};

/// What a draw shows.
struct SpriteDraw {
    Camera camera{};
    float aspect = 1.0F; ///< Of the target: width over height.
    Seconds time{};      ///< Since the map started: groups of images play from it.
    LightStyleValues lightStyles{};
    std::span<const SpriteInstance> instances{};
};

/// What draws sprites: their shader, pipelines and sampler, and what each frame in flight writes.
class SpriteRenderer {
public:
    /// Errors: GpuFailed.
    [[nodiscard]] static auto create(rhi::Device& device, const SpriteRendererDesc& desc)
        -> Result<SpriteRenderer, SpriteError>;

    SpriteRenderer(SpriteRenderer&& other) noexcept = default;
    auto operator=(SpriteRenderer&& other) -> SpriteRenderer& = delete;
    SpriteRenderer(const SpriteRenderer&) = delete;
    auto operator=(const SpriteRenderer&) -> SpriteRenderer& = delete;
    /// Its resources go with it, which the device keeps until the GPU is done with them.
    ~SpriteRenderer() = default;

    /// Puts `sprite`, named `label` to the debug tools, on the GPU, recording its uploads in
    /// `commands`: draw it from the same frame on. Errors: GpuFailed.
    [[nodiscard]] auto load(rhi::CommandList& commands, const assets::SpriteView& sprite,
                            std::string_view label) -> Result<SpriteModel, SpriteError>;

    /// Draws `draw`'s sprites inside a render pass with a color target of desc.colorFormat and a
    /// Depth32Float depth target (reversed depth), after the world, lit by `light`. Errors: GpuFailed
    /// (room for more sprites).
    [[nodiscard]] auto draw(rhi::CommandList& commands, const SpriteDraw& draw, const WorldLight& light)
        -> Result<void, SpriteError>;

private:
    inline explicit SpriteRenderer(rhi::Device& device) noexcept : m_device{&device} {}
    /// The shader, the bind group layouts, the sampler and a pipeline for each way sprites are drawn.
    [[nodiscard]] auto createPipelines(std::span<const std::byte> shaderCode, rhi::Format colorFormat)
        -> Result<void, rhi::RhiError>;
    /// Makes room in `slot` for `quads` quads, at least.
    [[nodiscard]] auto reserve(SpriteFrameSlot& slot, std::size_t quads) -> Result<void, rhi::RhiError>;
    [[nodiscard]] auto pipelineOf(RenderMode mode) const noexcept -> rhi::RenderPipelineHandle;

    rhi::Device* m_device;
    ChannelTable m_textureGamma{};
    rhi::OwnedShader m_shader;
    rhi::OwnedBindGroupLayout m_frameLayout;
    rhi::OwnedBindGroupLayout m_imageLayout;
    rhi::OwnedSampler m_sampler;
    EnumArray<RenderMode, rhi::OwnedRenderPipeline> m_pipelines;
    rhi::OwnedRenderPipeline m_lightPipeline; ///< Multiplies what is there by the light.
    std::vector<SpriteFrameSlot> m_frameSlots;
    std::uint64_t m_drawCount = 0;
};

} // namespace nx::render
