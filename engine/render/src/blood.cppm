// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The enhanced look's blood on the GPU (docs/05-rendering.md, section 10): the droplets and the mist a
/// client makes of a hit, drawn at once, each an instance the shader makes a quad of, with no vertex of
/// the CPU's. A droplet is round, as wide as it is, and drawn along the way it flies across the line of
/// sight as far as it flies in the draw's streak time, its head where it is, as the eye blurs a fast
/// drop: lit fully along its middle, darker toward its rim, a glint near its head. A mist is a puff of
/// smoke, as thick as it is through its core, thinning to none at its edge, its density uneven by its
/// seed. A splat lies flat on what it landed on, a little off it, its edge uneven by its seed, darker
/// where it pools. Each is of its display color times the light
/// where it is, as opaque as it is, blended over what is there, its depth tested but not written, seen
/// through the fog. They are not sorted: drops of blood overlap little, and alike.
export module nx.render:blood;

import std;
import nx.core;
import nx.math;
import nx.rhi;
import :camera;
import :fog;
import :gamma;
import :renderMode;

namespace nx::render {

// What the blood renderer keeps of a frame, which blood.cpp builds; not exported.

/// What one frame in flight writes its view and its drops to.
struct BloodFrameSlot {
    rhi::OwnedBuffer frame{};
    rhi::OwnedBuffer drops{};
    rhi::OwnedBindGroup group{};
    std::size_t dropCapacity = 0;
};

} // namespace nx::render

export namespace nx::render {

/// Why a blood renderer could not be made, or a frame of blood put on the GPU.
enum class BloodError : std::uint8_t {
    GpuFailed, ///< The GPU refused a resource (cause: the RhiError).
};

/// What a blood renderer draws with (BloodRenderer::create): its shader, its target's format, and how its
/// colors reach the target.
struct BloodRendererDesc {
    /// shaders/blood.slang, compiled for the device (bin/shaders/blood.spv).
    std::span<const std::byte> shaderCode{};
    rhi::Format colorFormat = rhi::Format::Undefined;
    /// How its colors reach the target it draws to: as a display's colors, or as linear light.
    ColorEncoding encoding = ColorEncoding::Display;
};

/// What a drop of blood is drawn as.
enum class BloodShape : std::uint8_t {
    Droplet, ///< Round, drawn along its flight.
    Mist,    ///< A puff of smoke, its density uneven.
    Splat,   ///< Flat on what it landed on, its edge uneven.
};

/// A drop of blood drawn in a frame: where it is and how fast it flies, its radius, its display color,
/// the light where it is (1 its color as it is), how opaque it is (from 0 to 1), what it is drawn as,
/// for a splat the way what it lies on faces, and a share from 0 to 1 that shapes a mist's smoke and a
/// splat's lobes, the same each frame for a drop that keeps it.
struct BloodInstance {
    Vec3 position{};
    Vec3 velocity{};
    HammerUnits radius{};
    RenderColor color{};
    float light = 1.0F;
    float opacity = 1.0F;
    BloodShape shape = BloodShape::Droplet;
    Vec3 normal{};
    float seed = 0.0F;
};

/// What a draw of blood shows: its drops, seen through `camera` on a target of `aspect`; how long of
/// its flight a droplet is drawn along (none: round); and the fog they are seen through, none clear
/// (nx.render:fog).
struct BloodDraw {
    Camera camera{};
    float aspect = 1.0F; ///< Of the target: width over height.
    std::span<const BloodInstance> drops{};
    Seconds streakTime{};
    std::optional<Fog> fog{};
};

/// What draws blood: its shader, its pipeline, and what each frame in flight writes.
class BloodRenderer {
public:
    /// Errors: GpuFailed.
    [[nodiscard]] static auto create(rhi::Device& device, const BloodRendererDesc& desc)
        -> Result<BloodRenderer, BloodError>;

    BloodRenderer(BloodRenderer&& other) noexcept = default;
    auto operator=(BloodRenderer&& other) -> BloodRenderer& = delete;
    BloodRenderer(const BloodRenderer&) = delete;
    auto operator=(const BloodRenderer&) -> BloodRenderer& = delete;
    /// Its resources go with it, which the device keeps until the GPU is done with them.
    ~BloodRenderer() = default;

    /// Writes `draw`'s drops for the frame in flight, for drawPrepared to record before the next prepare.
    /// Errors: GpuFailed (room for more).
    [[nodiscard]] auto prepare(const BloodDraw& draw) -> Result<void, BloodError>;
    /// Records the drops of the last prepare, inside a render pass with a color target of
    /// desc.colorFormat and a Depth32Float depth target (reversed depth), in the world's depths.
    void drawPrepared(rhi::CommandList& commands) const;
    /// Draws `draw`'s drops: prepares them, then records them. Errors: those of prepare.
    [[nodiscard]] auto draw(rhi::CommandList& commands, const BloodDraw& draw) -> Result<void, BloodError>;

private:
    inline BloodRenderer(rhi::Device& device, ColorEncoding colorEncoding) noexcept
        : gpu{&device}, encoding{colorEncoding} {}
    /// The shader, the bind group layout and the pipeline.
    [[nodiscard]] auto createPipeline(const BloodRendererDesc& desc) -> Result<void, rhi::RhiError>;
    /// Makes room in `slot` for `drops` drops, at least.
    [[nodiscard]] auto reserve(BloodFrameSlot& slot, std::size_t drops) -> Result<void, rhi::RhiError>;

    rhi::Device* gpu;
    ColorEncoding encoding;
    rhi::OwnedShader shader;
    rhi::OwnedBindGroupLayout frameLayout;
    rhi::OwnedRenderPipeline pipeline;
    std::vector<BloodFrameSlot> frameSlots;
    std::uint64_t drawCount = 0;
    std::uint32_t dropCount = 0; ///< Of the last prepare.
};

} // namespace nx::render
