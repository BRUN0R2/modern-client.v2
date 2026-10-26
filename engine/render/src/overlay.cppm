// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the tools draw over the scene (docs/05-rendering.md, section 7.1): triangles and lines in the
/// map's space, each vertex of its own color, blended by its alpha over what the scene drew, hidden by
/// what the scene drew in front of them, never hiding it: a navigation mesh, its polygons and edges.
export module nx.render:overlay;

import std;
import nx.core;
import nx.math;
import nx.rhi;
import :camera;
import :gamma;

namespace nx::render {

// What the overlay keeps of a frame in flight, which overlay.cpp builds; not exported.

/// What one frame in flight writes the view's matrix and its vertices to.
struct OverlayFrameSlot {
    rhi::OwnedBuffer frame{};
    rhi::OwnedBindGroup group{};
    rhi::OwnedBuffer vertices{};
    std::size_t vertexCapacity = 0;
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

/// A corner of an overlay's triangle or an end of its line.
struct OverlayVertex {
    Vec3 position{};
    OverlayColor color{};
};

/// What the overlay's renderer draws with (OverlayRenderer::create): its shader and its pass's formats.
struct OverlayRendererDesc {
    /// shaders/overlay.slang, compiled for the device (bin/shaders/overlay.spv).
    std::span<const std::byte> shaderCode{};
    rhi::Format colorFormat = rhi::Format::Undefined;
    rhi::Format depthFormat = rhi::Format::Depth32Float; ///< Of the scene's pass, tested, not written.
    /// How its colors reach the target: as they are, or as linear light for the enhanced look's.
    ColorEncoding encoding = ColorEncoding::Display;
};

/// What a draw shows: as `camera` sees it on a target of `aspect`, `triangles`, every three vertices
/// one, then `lines`, every two one.
struct OverlayDraw {
    Camera camera{};
    float aspect = 1.0F;
    std::span<const OverlayVertex> triangles{};
    std::span<const OverlayVertex> lines{};
};

/// What draws the overlay: its shader, its pipelines, and what each frame in flight writes.
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

    /// Writes `draw`'s vertices for the frame in flight, for record to draw before the next prepare.
    /// Errors: GpuFailed (room for more vertices).
    [[nodiscard]] auto prepare(const OverlayDraw& draw) -> Result<void, OverlayError>;
    /// Records the triangles and lines of the last prepare in a render pass with a color target of
    /// desc.colorFormat and the scene's depth target of desc.depthFormat, after the scene.
    void record(rhi::CommandList& commands) const;
    /// Draws `draw`: prepares it, then records it. Errors: those of prepare.
    [[nodiscard]] auto draw(rhi::CommandList& commands, const OverlayDraw& draw)
        -> Result<void, OverlayError>;

private:
    inline OverlayRenderer(rhi::Device& device, ColorEncoding encoding) noexcept
        : m_device{&device}, m_encoding{encoding} {}
    /// The shader, the frame's bind group layout, and a pipeline for the triangles and one for the lines.
    [[nodiscard]] auto createPipelines(const OverlayRendererDesc& desc) -> Result<void, rhi::RhiError>;
    /// Makes room in `slot` for `vertices` vertices, at least.
    [[nodiscard]] auto reserve(OverlayFrameSlot& slot, std::size_t vertices) -> Result<void, rhi::RhiError>;

    rhi::Device* m_device;
    ColorEncoding m_encoding;
    rhi::OwnedShader m_shader;
    rhi::OwnedBindGroupLayout m_frameLayout;
    rhi::OwnedRenderPipeline m_trianglePipeline;
    rhi::OwnedRenderPipeline m_linePipeline;
    std::vector<OverlayFrameSlot> m_frameSlots;
    std::uint32_t m_triangleVertices = 0; ///< Of the last prepare.
    std::uint32_t m_lineVertices = 0;     ///< Of the last prepare.
    std::uint64_t m_drawCount = 0;        ///< Prepares so far: the frame slot of the next is this one's.
};

} // namespace nx::render
