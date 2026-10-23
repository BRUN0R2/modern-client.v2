// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Reflection probes (docs/adr/0025-enhanced-rendering.md): what a metal reflects where it is, the map
/// itself. Each probe is the world drawn from its place toward its six sides, laid out as the sky's
/// sides are (shaders/skyDirection.slang), so that one lookup reads both; then each side's mip levels
/// prefiltered for a rougher surface each (shaders/probe.slang). They are baked as the map is played,
/// one a frame, the first the nearest the player starts at; a model reflects the nearest baked one.
/// Captures, prefiltering and stored mip levels all hold linear radiance, sampled without a display
/// transfer by the consuming material.
///
/// Today: the world and its sky, its brush entities as the game places them as each is baked; a
/// reflection as from far away. Box projection, the blend of two probes and their cache on disk by
/// the map's hash come next (ADR-0025).
export module nx.render:probes;

import std;
import nx.core;
import nx.math;
import nx.rhi;
import :camera;
import :world;

export namespace nx::render {

/// Why the probes could not be put on the GPU.
enum class ProbeError : std::uint8_t {
    GpuFailed, ///< The GPU refused a resource (cause: the RhiError).
};

/// The texels of a side of a probe, each way, at its sharpest.
inline constexpr std::uint32_t kProbeSideSize = 128;
/// The most probes of a map.
inline constexpr std::size_t kMostProbes = 32;
/// The views the world prepares a frame while probes are baked: the player's, and a probe's six sides.
inline constexpr std::uint32_t kBakingViewsPerFrame = 1 + rhi::kCubeFaces;

struct ReflectionProbesDesc {
    /// shaders/probe.slang, compiled for the device (bin/shaders/probe.spv).
    std::span<const std::byte> shaderCode{};
    /// The format the world draws linear radiance to: its pipelines' (BspWorldDesc::colorFormat).
    rhi::Format captureFormat = rhi::Format::Undefined;
    /// Where the probes are, in the order they are baked; kMostProbes at most count.
    std::span<const Vec3> positions{};
};

/// The probes of a map.
class ReflectionProbes {
public:
    /// Records the first transitions of its textures in `commands`. Errors: GpuFailed.
    [[nodiscard]] static auto create(rhi::Device& device, rhi::CommandList& commands,
                                     const ReflectionProbesDesc& desc)
        -> Result<ReflectionProbes, ProbeError>;

    ReflectionProbes(ReflectionProbes&& other) noexcept = default;
    auto operator=(ReflectionProbes&& other) -> ReflectionProbes& = delete;
    ReflectionProbes(const ReflectionProbes&) = delete;
    auto operator=(const ReflectionProbes&) -> ReflectionProbes& = delete;
    /// Its resources go with it, which the device keeps until the GPU is done with them.
    ~ReflectionProbes() = default;

    /// Whether every probe is baked.
    [[nodiscard]] inline auto isBaked() const noexcept -> bool { return m_baked == m_positions.size(); }
    /// Records the next probe's baking, outside a render pass: `world`, made with
    /// kBakingViewsPerFrame views a frame, drawn toward its six sides as `scene` shows the world (its
    /// time, its light styles, where the brush entities are), then its mip levels prefiltered. Nothing
    /// once every probe is baked. Each call is a frame's: the world prepares six views in it.
    void bakeNext(rhi::CommandList& commands, BspWorld& world, const WorldFrame& scene);

    /// The baked probe nearest `point`; none before the first is baked.
    [[nodiscard]] auto nearest(Vec3 point) const noexcept -> std::optional<std::uint32_t>;
    /// The probes' sides, layer probe * 6 + side, with their mip levels, for shaders to sample.
    [[nodiscard]] inline auto texture() const noexcept -> rhi::TextureHandle { return m_probes.get(); }
    [[nodiscard]] inline auto mipLevels() const noexcept -> std::uint32_t { return m_mipLevels; }

private:
    /// A pass prefiltering a side of a probe at a level: its uniform's bind group.
    struct PrefilterPass {
        std::uint32_t side = 0;
        std::uint32_t level = 0;
        rhi::OwnedBindGroup group{};
    };

    inline explicit ReflectionProbes(rhi::Device& device) noexcept : m_device{&device} {}

    rhi::Device* m_device;
    std::vector<Vec3> m_positions;
    std::size_t m_baked = 0;
    std::uint32_t m_mipLevels = 1;
    rhi::OwnedTexture m_capture;      ///< The six sides of the probe being baked, as drawn.
    rhi::OwnedTexture m_captureDepth; ///< Their depth.
    rhi::OwnedTexture m_probes;
    rhi::OwnedShader m_shader;
    rhi::OwnedSampler m_sampler;
    rhi::OwnedBindGroupLayout m_layout;
    rhi::OwnedRenderPipeline m_prefilter;
    rhi::OwnedBuffer m_uniforms;
    std::vector<PrefilterPass> m_passes; ///< Each side at each level.
};

/// The angles a camera looks toward side `side` of a probe with (+X, -X, +Y, -Y, +Z, -Z), the side then
/// drawn as the sky's is laid out: upright and unmirrored, the top and bottom ones facing +X.
[[nodiscard]] auto probeSideAngles(std::uint32_t side) noexcept -> EulerAngles;

} // namespace nx::render
