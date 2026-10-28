// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Particles on the GPU (docs/05-rendering.md, section 10), drawn as the original's CL_DrawParticles
/// draws them, as Xash3D-FWGS reconstructs it (GPL, consulted: docs/20 section 3): each a quad facing
/// the view, the engine's own texture of a dot in a corner of it, of its color, as opaque as it is,
/// blended over what is there after everything else the frame draws, its depth tested but not written.
/// A quad is two units across, its dot a quarter of it, and larger the farther it is past 20 units in
/// front of the view, by 0.002 of its distance, so that the far ones do not vanish.
export module nx.render:particle;

import std;
import nx.core;
import nx.math;
import nx.rhi;
import :camera;
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

/// What a particle renderer draws with (ParticleRenderer::create): its shader, its target's format, and
/// how its colors reach the target.
struct ParticleRendererDesc {
    /// shaders/particle.slang, compiled for the device (bin/shaders/particle.spv).
    std::span<const std::byte> shaderCode{};
    rhi::Format colorFormat = rhi::Format::Undefined;
    /// How its colors reach the target it draws to: as the original draws them, or as linear light.
    ColorEncoding encoding = ColorEncoding::Display;
};

/// A particle drawn in a frame: where it is, its color, and how opaque it is, of 255.
struct ParticleInstance {
    Vec3 origin{};
    RenderColor color{};
    std::uint8_t amount = 0;
};

/// What a draw of particles shows.
struct ParticleDraw {
    Camera camera{};
    float aspect = 1.0F; ///< Of the target: width over height.
    std::span<const ParticleInstance> particles{};
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

    /// Writes `draw`'s particles for the frame in flight, for drawPrepared to record before the next
    /// prepare. Errors: GpuFailed (room for more particles).
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
    /// The shader, the bind group layouts, the sampler and the pipeline.
    [[nodiscard]] auto createPipeline(std::span<const std::byte> shaderCode, rhi::Format colorFormat)
        -> Result<void, rhi::RhiError>;
    /// Puts the dot on the GPU, its upload recorded in `commands`.
    [[nodiscard]] auto createDot(rhi::CommandList& commands) -> Result<void, rhi::RhiError>;
    /// Makes room in `slot` for `quads` quads, at least.
    [[nodiscard]] auto reserve(ParticleFrameSlot& slot, std::size_t quads) -> Result<void, rhi::RhiError>;

    rhi::Device* m_device;
    ColorEncoding m_encoding = ColorEncoding::Display;
    rhi::OwnedShader m_shader;
    rhi::OwnedBindGroupLayout m_frameLayout;
    rhi::OwnedBindGroupLayout m_imageLayout;
    rhi::OwnedSampler m_sampler;
    rhi::OwnedRenderPipeline m_pipeline;
    rhi::OwnedTexture m_dot;
    rhi::OwnedBindGroup m_dotGroup;
    std::vector<ParticleFrameSlot> m_frameSlots;
    std::uint64_t m_drawCount = 0;
    std::uint32_t m_preparedQuads = 0; ///< Of the last prepare.
};

} // namespace nx::render
