// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Particles and streaks on the GPU (docs/05-rendering.md, section 10), drawn as the original's
/// CL_DrawParticles and CL_DrawTracers draw them, as Xash3D-FWGS reconstructs them (GPL, consulted:
/// docs/20 section 3), after everything else the frame draws, their depth tested but not written. A
/// particle is a quad facing the view, the engine's own texture of a dot in a corner of it, of its color,
/// as opaque as it is, blended over what is there; two units across, its dot a quarter of it, and larger
/// the farther it is past 20 units in front of the view, by 0.002 of its distance, so that the far ones
/// do not vanish. A streak is a quad along a line, as wide as it is across the line on the screen, the
/// first image of the engine's dot sprite along it, of its color, added as much as it is opaque.
export module nx.render:particle;

import std;
import nx.core;
import nx.math;
import nx.rhi;
import nx.assets;
import :camera;
import :fog;
import :gamma;
import :renderMode;

namespace nx::render {

// What the particle renderer keeps of a frame, which particle.cpp builds; not exported.

/// What one frame in flight writes its view and its particles' quads to.
struct ParticleFrameSlot {
    rhi::OwnedBuffer frame{};
    rhi::OwnedBindGroup group{};
    rhi::OwnedBuffer vertices{};
    std::size_t quadCapacity = 0;
};

} // namespace nx::render

export namespace nx::render {

/// Why a particle renderer could not be made, or a frame of particles put on the GPU.
enum class ParticleError : std::uint8_t {
    GpuFailed, ///< The GPU refused a resource (cause: the RhiError).
};

/// What a particle renderer draws with (ParticleRenderer::create): its shader, its target's format, the
/// original's picture, and how its colors reach the target.
struct ParticleRendererDesc {
    /// shaders/particle.slang, compiled for the device (bin/shaders/particle.spv).
    std::span<const std::byte> shaderCode{};
    rhi::Format colorFormat = rhi::Format::Undefined;
    GammaSettings gamma{}; ///< The texture gamma of the streaks' sprite.
    /// How its colors reach the target it draws to: as the original draws them, or as linear light.
    ColorEncoding encoding = ColorEncoding::Display;
};

/// A particle drawn in a frame: where it is, its color, and how opaque it is, of 255.
struct ParticleInstance {
    Vec3 origin{};
    RenderColor color{};
    std::uint8_t amount = 0;
};

/// A streak drawn in a frame: the line it is drawn along, half its width, its color, and how opaque it
/// is, of 255.
struct StreakInstance {
    Vec3 start{};
    Vec3 end{};
    HammerUnits halfWidth{};
    RenderColor color{};
    std::uint8_t amount = 0;
};

/// What a draw of particles shows: its particles, then its streaks.
struct ParticleDraw {
    Camera camera{};
    float aspect = 1.0F; ///< Of the target: width over height.
    std::span<const ParticleInstance> particles{};
    std::span<const StreakInstance> streaks{};
    /// The fog the particles are seen through, the streaks added through none; none, clear
    /// (nx.render:fog).
    std::optional<Fog> fog{};
};

/// What draws particles: their shader, pipeline, dot texture, and what each frame in flight writes.
class ParticleRenderer {
public:
    /// Records the upload of its dot in `commands`. Errors: GpuFailed.
    [[nodiscard]] static auto create(rhi::Device& device, rhi::CommandList& commands,
                                     const ParticleRendererDesc& desc)
        -> Result<ParticleRenderer, ParticleError>;

    ParticleRenderer(ParticleRenderer&& other) noexcept = default;
    auto operator=(ParticleRenderer&& other) -> ParticleRenderer& = delete;
    ParticleRenderer(const ParticleRenderer&) = delete;
    auto operator=(const ParticleRenderer&) -> ParticleRenderer& = delete;
    /// Its resources go with it, which the device keeps until the GPU is done with them.
    ~ParticleRenderer() = default;

    /// Puts the first image of `sprite`, the engine's dot (sprites/dot.spr), on the GPU, its uploads
    /// recorded in `commands`: what streaks are drawn with, none before, nor of a sprite of no image.
    /// Errors: GpuFailed.
    [[nodiscard]] auto loadStreakSprite(rhi::CommandList& commands, const assets::SpriteView& sprite)
        -> Result<void, ParticleError>;

    /// Writes `draw`'s particles and streaks for the frame in flight, for drawPrepared to record before
    /// the next prepare: a streak behind the view, or seen end on, is not drawn. Errors: GpuFailed (room
    /// for more).
    [[nodiscard]] auto prepare(const ParticleDraw& draw) -> Result<void, ParticleError>;
    /// Records the particles of the last prepare, inside a render pass with a color target of
    /// desc.colorFormat and a Depth32Float depth target (reversed depth), after everything else the frame
    /// draws in the world's depths.
    void drawPrepared(rhi::CommandList& commands) const;
    /// Draws `draw`'s particles: prepares them, then records them. Errors: those of prepare.
    [[nodiscard]] auto draw(rhi::CommandList& commands, const ParticleDraw& draw)
        -> Result<void, ParticleError>;

private:
    inline explicit ParticleRenderer(rhi::Device& device) noexcept : m_device{&device} {}
    /// The shader, the bind group layouts, the sampler and the pipelines.
    [[nodiscard]] auto createPipeline(std::span<const std::byte> shaderCode, rhi::Format colorFormat)
        -> Result<void, rhi::RhiError>;
    /// Puts the dot on the GPU, its upload recorded in `commands`.
    [[nodiscard]] auto createDot(rhi::CommandList& commands) -> Result<void, rhi::RhiError>;
    /// Makes room in `slot` for `quads` quads, at least.
    [[nodiscard]] auto reserve(ParticleFrameSlot& slot, std::size_t quads) -> Result<void, rhi::RhiError>;

    rhi::Device* m_device;
    ColorEncoding m_encoding = ColorEncoding::Display;
    ChannelTable m_textureGamma{};
    rhi::OwnedShader m_shader;
    rhi::OwnedBindGroupLayout m_frameLayout;
    rhi::OwnedBindGroupLayout m_imageLayout;
    rhi::OwnedSampler m_sampler;
    rhi::OwnedRenderPipeline m_pipeline;
    rhi::OwnedRenderPipeline streakPipeline;
    rhi::OwnedTexture m_dot;
    rhi::OwnedBindGroup m_dotGroup;
    rhi::OwnedTexture streakImage;
    rhi::OwnedBindGroup streakGroup;
    std::vector<ParticleFrameSlot> m_frameSlots;
    std::uint64_t m_drawCount = 0;
    std::uint32_t m_preparedQuads = 0; ///< Of the last prepare's particles.
    std::uint32_t preparedStreaks = 0; ///< Of the last prepare.
};

} // namespace nx::render
