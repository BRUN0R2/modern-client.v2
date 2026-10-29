// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the tools draw over the scene (docs/05-rendering.md, section 7.1): a mesh kept on the GPU from one
/// frame to the next, triangles and lines in the map's space, each vertex of its own color, blended by its
/// alpha (a navigation mesh, its polygons and edges); and arrows along ways (the agents'), as a beam of the
/// original draws a sprite of arrows (TE_BEAMPOINTS with sprites/arrow1.spr): on a band facing the view
/// about each stretch of a way, an arrow every spacing back from the stretch's end, pointing to it, added
/// to what is there; the arrows' shape the shader's own, the sprite's proportions, its head and its shaft
/// each of its own color. What the scene drew in front of them hides them; they never hide it. In a target
/// of linear light, the enhanced look's, each gives its light times its brightness, so that one brighter
/// than white glows through the bloom.
export module nx.render:overlay;

import std;
import nx.core;
import nx.math;
import nx.rhi;
import :camera;
import :gamma;

namespace nx::render {

// What the overlay keeps of a frame in flight, which overlay.cpp builds; not exported.

/// What one frame in flight writes its view and its arrows to.
struct OverlayFrameSlot {
    rhi::OwnedBuffer frame{};
    rhi::OwnedBuffer arrows{};
    rhi::OwnedBindGroup group{};
    std::size_t arrowCapacity = 0;
};

} // namespace nx::render

export namespace nx::render {

/// Why what draws the overlay could not be made, or a draw prepared.
enum class OverlayError : std::uint8_t {
    GpuFailed, ///< The GPU refused a resource (cause: the RhiError).
};

/// A display color, and how opaque it is drawn.
struct OverlayColor {
    std::uint8_t red = 255;
    std::uint8_t green = 255;
    std::uint8_t blue = 255;
    std::uint8_t alpha = 255;

    [[nodiscard]] inline auto operator==(const OverlayColor&) const noexcept -> bool = default;
};

/// A corner of a kept triangle or an end of a kept line: where it is, its color, and how many times its
/// color's light it gives in a target of linear light; a display target draws its color as it is.
struct OverlayVertex {
    Vec3 position{};
    OverlayColor color{};
    float brightness = 1.0F;
};

/// What the overlay keeps on the GPU (OverlayRenderer::keep): triangles, every three vertices one, and
/// lines, every two one, that stay as they are from one frame to the next.
struct OverlayMesh {
    std::span<const OverlayVertex> triangles{};
    std::span<const OverlayVertex> lines{};
};

/// A stretch of a way drawn as arrows pointing from `start` to `end`: the display colors of their heads and
/// of their shafts, and how many times their light they give in a target of linear light.
struct OverlayArrows {
    Vec3 start{};
    Vec3 end{};
    OverlayColor headColor{};
    OverlayColor shaftColor{};
    float brightness = 1.0F;
};

/// How arrows are drawn: on a band `width` across, an arrow every `spacing` back from a stretch's end.
struct OverlayArrowStyle {
    HammerUnits width{};
    HammerUnits spacing{};
};

/// What the overlay's renderer draws with (OverlayRenderer::create): its shader and its pass's format.
struct OverlayRendererDesc {
    /// shaders/overlay.slang, compiled for the device (bin/shaders/overlay.spv).
    std::span<const std::byte> shaderCode{};
    rhi::Format colorFormat = rhi::Format::Undefined;
    /// How its colors reach the target: as they are, or as linear light for the enhanced look's.
    ColorEncoding encoding = ColorEncoding::Display;
};

/// What a frame draws: as `camera` sees it on a target of `aspect`, the mesh kept when `isMeshShown`, then
/// `arrows` in `arrowStyle`.
struct OverlayDraw {
    Camera camera{};
    float aspect = 1.0F;
    bool isMeshShown = false;
    std::span<const OverlayArrows> arrows{};
    OverlayArrowStyle arrowStyle{};
};

/// What draws the overlay: its shader, its pipelines, the mesh it keeps, and what each frame in flight
/// writes.
class OverlayRenderer {
public:
    /// Errors: GpuFailed.
    [[nodiscard]] static auto create(rhi::Device& device, const OverlayRendererDesc& desc)
        -> Result<OverlayRenderer, OverlayError>;

    OverlayRenderer(OverlayRenderer&& other) noexcept = default;
    auto operator=(OverlayRenderer&& other) -> OverlayRenderer& = delete;
    OverlayRenderer(const OverlayRenderer&) = delete;
    auto operator=(const OverlayRenderer&) -> OverlayRenderer& = delete;
    /// Its resources go with it, which the device keeps until the GPU is done with them.
    ~OverlayRenderer() = default;

    /// Keeps `mesh` in GPU memory in place of the one it kept, its uploads recorded in `commands` outside
    /// a render pass: a frame that shows the mesh draws it with no upload. Errors: GpuFailed.
    [[nodiscard]] auto keep(rhi::CommandList& commands, const OverlayMesh& mesh)
        -> Result<void, OverlayError>;

    /// Writes `draw`'s view and arrows for the frame in flight, for drawPrepared to record before the next
    /// prepare. Errors: GpuFailed (room for more arrows).
    [[nodiscard]] auto prepare(const OverlayDraw& draw) -> Result<void, OverlayError>;
    /// Records the last prepare's mesh, blended over what is there, and arrows, added to it, inside a render
    /// pass with a color target of desc.colorFormat and a Depth32Float depth target (reversed depth), after
    /// the scene: their depth tested, not written.
    void drawPrepared(rhi::CommandList& commands) const;
    /// Draws `draw`: prepares it, then records it. Errors: those of prepare.
    [[nodiscard]] auto draw(rhi::CommandList& commands, const OverlayDraw& draw)
        -> Result<void, OverlayError>;

private:
    inline OverlayRenderer(rhi::Device& device, ColorEncoding encoding) noexcept
        : m_device{&device}, m_encoding{encoding} {}
    /// The shader, the frame's bind group layout, and the pipelines of the mesh's triangles and lines and
    /// of the arrows.
    [[nodiscard]] auto createPipelines(const OverlayRendererDesc& desc) -> Result<void, rhi::RhiError>;
    /// Makes room in `slot` for `arrows` stretches, at least.
    [[nodiscard]] auto reserve(OverlayFrameSlot& slot, std::size_t arrows) -> Result<void, rhi::RhiError>;

    rhi::Device* m_device;
    ColorEncoding m_encoding;
    rhi::OwnedShader m_shader;
    rhi::OwnedBindGroupLayout m_frameLayout;
    rhi::OwnedRenderPipeline m_trianglePipeline;
    rhi::OwnedRenderPipeline m_linePipeline;
    rhi::OwnedRenderPipeline m_arrowPipeline;
    rhi::OwnedBuffer m_meshTriangles; ///< The kept mesh's, in GPU memory.
    rhi::OwnedBuffer m_meshLines;     ///< The kept mesh's, in GPU memory.
    std::uint32_t m_meshTriangleVertices = 0;
    std::uint32_t m_meshLineVertices = 0;
    std::vector<OverlayFrameSlot> m_frameSlots;
    bool m_isMeshDrawn = false;     ///< By the last prepare.
    std::uint32_t m_arrowCount = 0; ///< Of the last prepare.
    std::uint64_t m_drawCount = 0;  ///< Prepares so far: the frame slot of the next is this one's.
};

} // namespace nx::render
