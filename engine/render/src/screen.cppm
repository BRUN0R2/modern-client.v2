// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The screen (docs/12-ui-hud-and-console.md, section 2): the pictures and text the classic HUD and
/// the console draw over the frame, in pixels of the target from its top left, as the original's
/// SPR_Draw, SPR_DrawAdditive, FillRGBA and FillRGBABlend draw them: a rectangle of a picture, or
/// of a color alone, times a color, drawn as it is, added to what is there, or blended over it.
/// The pictures are the images of sprites (the HUD's), and the pictures and console fonts of the
/// WADs, their palettes through the texture gamma, as the original's textures go. A rectangle of a
/// HUD sprite is drawn as the original draws it (spriteQuad).
export module nx.render:screen;

import std;
import nx.core;
import nx.rhi;
import nx.assets;
import :gamma;

namespace nx::render {

// What the screen keeps of a frame in flight, which screen.cpp builds; not exported.

/// What one frame in flight writes the target's size and its rectangles' vertices to.
struct ScreenFrameSlot {
    rhi::OwnedBuffer frame{};
    rhi::OwnedBindGroup group{};
    rhi::OwnedBuffer vertices{};
    std::size_t quadCapacity = 0;
};

/// Rectangles in a row that are drawn alike: with one pipeline, one picture (none for colors) and
/// one scissor.
struct ScreenBatch {
    rhi::RenderPipelineHandle pipeline{};
    rhi::BindGroupHandle image{};
    rhi::Rect scissor{};
    std::uint32_t firstQuad = 0;
    std::uint32_t quadCount = 0;
};

} // namespace nx::render

export namespace nx::render {

/// How a picture's texels become the pixels a rectangle of it covers.
enum class ScreenFilter : std::uint8_t {
    Nearest, ///< The texel under each pixel, which keeps a picture sharp at any whole scale.
    Linear,  ///< The four texels about each pixel, each as much as it is near: the original's sprites.
    Count,
};

/// Why a picture, or what draws the screen, could not be put on the GPU.
enum class ScreenError : std::uint8_t {
    GpuFailed, ///< The GPU refused a resource (cause: the RhiError).
};

/// What the screen's renderer draws with (ScreenRenderer::create): its shader, its pass's formats
/// and the original's picture its pictures go through.
struct ScreenRendererDesc {
    /// shaders/screen.slang, compiled for the device (bin/shaders/screen.spv).
    std::span<const std::byte> shaderCode{};
    rhi::Format colorFormat = rhi::Format::Undefined;
    /// The depth attachment of the render passes the screen is drawn in, which it neither tests nor
    /// writes; Undefined for passes without one.
    rhi::Format depthFormat = rhi::Format::Depth32Float;
    GammaSettings gamma{};
};

/// A picture on the GPU that the screen draws rectangles of.
class ScreenImage {
public:
    ScreenImage(ScreenImage&& other) noexcept = default;
    auto operator=(ScreenImage&& other) noexcept -> ScreenImage& = default;
    ScreenImage(const ScreenImage&) = delete;
    auto operator=(const ScreenImage&) -> ScreenImage& = delete;
    /// Its resources go with it, which the device keeps until the GPU is done with them.
    ~ScreenImage() = default;

    [[nodiscard]] inline auto width() const noexcept -> std::uint32_t { return m_width; }
    [[nodiscard]] inline auto height() const noexcept -> std::uint32_t { return m_height; }

private:
    friend class ScreenRenderer;
    ScreenImage() = default;

    rhi::OwnedTexture m_texture;
    EnumArray<ScreenFilter, rhi::OwnedBindGroup> m_groups; ///< The texture with each filter's sampler.
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
};

/// A console font on the GPU: its picture, and where each of its characters is in it.
class ScreenFont {
public:
    ScreenFont(ScreenFont&& other) noexcept = default;
    auto operator=(ScreenFont&& other) noexcept -> ScreenFont& = default;
    ScreenFont(const ScreenFont&) = delete;
    auto operator=(const ScreenFont&) -> ScreenFont& = delete;
    ~ScreenFont() = default;

    [[nodiscard]] inline auto image() const noexcept -> const ScreenImage& { return m_image; }
    /// The height of each character.
    [[nodiscard]] inline auto rowHeight() const noexcept -> std::uint32_t { return m_rowHeight; }
    /// Where `character`, a byte of the font's code page, is in the picture.
    [[nodiscard]] inline auto glyph(char character) const noexcept -> const assets::FontGlyph& {
        return m_glyphs[static_cast<unsigned char>(character)];
    }
    /// How wide `text` is in pixels: its characters' widths together.
    [[nodiscard]] auto widthOf(std::string_view text) const noexcept -> std::uint32_t;

private:
    friend class ScreenRenderer;
    inline ScreenFont(ScreenImage image, std::uint32_t rowHeight,
                      const std::array<assets::FontGlyph, assets::kFontGlyphs>& glyphs) noexcept
        : m_image{std::move(image)}, m_rowHeight{rowHeight}, m_glyphs{glyphs} {}

    ScreenImage m_image;
    std::uint32_t m_rowHeight = 0;
    std::array<assets::FontGlyph, assets::kFontGlyphs> m_glyphs{};
};

/// Whether palette index 255 of a picture shows what is behind it, as the masked pictures of the
/// original do; the console's background is opaque.
enum class PictureMask : std::uint8_t {
    Opaque,
    Masked,
};

/// How a rectangle is drawn over what is there.
enum class ScreenBlend : std::uint8_t {
    Normal,      ///< As it is, but its transparent texels: SPR_Draw, and a font's characters.
    Additive,    ///< Added to what is there, as much as its alpha says: SPR_DrawAdditive, FillRGBA.
    Translucent, ///< Over what is there, as opaque as its alpha: FillRGBABlend.
    Count,
};

/// The color a picture is multiplied by, or a rectangle filled with; and how opaque it is drawn.
struct ScreenColor {
    std::uint8_t red = 255;
    std::uint8_t green = 255;
    std::uint8_t blue = 255;
    std::uint8_t alpha = 255;

    [[nodiscard]] inline auto operator==(const ScreenColor&) const noexcept -> bool = default;
};

/// A rectangle of the target, in pixels from its top left: right and down.
struct ScreenRect {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

/// A rectangle the screen draws: a part of a picture stretched over `target`, times `color`; or, with
/// no picture, `color` alone. Only the pixels of `clip` are drawn, when there is one: those of the
/// panel a text scrolls in.
struct ScreenQuad {
    const ScreenImage* image = nullptr;
    assets::SpriteRect source{}; ///< Of the picture, in texels.
    ScreenRect target{};
    ScreenColor color{};
    ScreenBlend blend = ScreenBlend::Normal;
    ScreenFilter filter = ScreenFilter::Nearest;
    std::optional<rhi::Rect> clip{}; ///< In pixels of the target; none for all of it.
};

/// The rectangle `source` of `image` as the original draws a rectangle of a HUD sprite
/// (SPR_DrawAdditive, as Counter-Strike 1.6 shows it: docs/12-ui-hud-and-console.md, section 3.2):
/// from `x`, `y` over a pixel more than its size each way, its texels filtered linearly; in
/// `color`, drawn as `blend` says.
[[nodiscard]] auto spriteQuad(const ScreenImage& image, assets::SpriteRect source, float x, float y,
                              ScreenColor color, ScreenBlend blend) -> ScreenQuad;

/// Appends to `quads` the characters of `text`, bytes of `font`'s code page, the first with its top
/// left at `x`, `y`, each after the one before it by its width, in `color`, drawn as `blend` says.
/// Returns where the next character would be.
auto appendText(std::vector<ScreenQuad>& quads, const ScreenFont& font, std::string_view text, float x,
                float y, ScreenColor color, ScreenBlend blend = ScreenBlend::Normal) -> float;

/// The quads made of what something on the screen shows, its `Shown`, such as the numbers of a HUD,
/// kept from a frame to the next: made again only when what it shows changes, appended again as they
/// were while it does not. A HUD laid out and its text written each frame would do again work whose
/// result changes about once a second.
template <std::equality_comparable Shown>
class RetainedQuads {
public:
    /// Appends to `quads` those `make` appends to the vector it is given for `shown`, called only when
    /// `shown` differs from what the last call showed, or on the first call.
    template <std::invocable<std::vector<ScreenQuad>&> Make>
    inline void append(std::vector<ScreenQuad>& quads, const Shown& shown, Make&& make) {
        if (m_shown != shown) {
            m_quads.clear(); // its memory kept
            std::forward<Make>(make)(m_quads);
            m_shown = shown;
        }
        quads.insert(quads.end(), m_quads.begin(), m_quads.end());
    }

private:
    std::optional<Shown> m_shown; ///< None before the first append.
    std::vector<ScreenQuad> m_quads;
};

/// What a draw shows: `quads`, in their order, over a target of `size`.
struct ScreenDraw {
    rhi::Extent2d size{};
    std::span<const ScreenQuad> quads{};
};

/// What draws the screen: its shader, pipelines and samplers, and what each frame in flight writes.
class ScreenRenderer {
public:
    /// Errors: GpuFailed.
    [[nodiscard]] static auto create(rhi::Device& device, const ScreenRendererDesc& desc)
        -> Result<ScreenRenderer, ScreenError>;

    ScreenRenderer(ScreenRenderer&& other) noexcept = default;
    auto operator=(ScreenRenderer&& other) -> ScreenRenderer& = delete;
    ScreenRenderer(const ScreenRenderer&) = delete;
    auto operator=(const ScreenRenderer&) -> ScreenRenderer& = delete;
    /// Its resources go with it, which the device keeps until the GPU is done with them.
    ~ScreenRenderer() = default;

    /// A picture for each frame of `sprite`, the first image of a frame that is a group, as the HUD
    /// draws a sprite's frames; named `label` to the debug tools, its uploads recorded in `commands`.
    /// Errors: GpuFailed.
    [[nodiscard]] auto loadSprite(rhi::CommandList& commands, const assets::SpriteView& sprite,
                                  std::string_view label) -> Result<std::vector<ScreenImage>, ScreenError>;
    /// The picture `picture`, its index 255 transparent when `mask` says so. Errors: GpuFailed.
    [[nodiscard]] auto loadPicture(rhi::CommandList& commands, const assets::WadPicture& picture,
                                   PictureMask mask, std::string_view label)
        -> Result<ScreenImage, ScreenError>;
    /// The console font `font`, its index 255 transparent. Errors: GpuFailed.
    [[nodiscard]] auto loadFont(rhi::CommandList& commands, const assets::FontView& font,
                                std::string_view label) -> Result<ScreenFont, ScreenError>;
    /// The picture of `texels`, `width` by `height` RGBA texels row by row from the top, as they are:
    /// what the interface makes itself, such as its glyph atlases. Errors: GpuFailed.
    [[nodiscard]] auto loadImage(rhi::CommandList& commands, std::span<const std::byte> texels,
                                 std::uint32_t width, std::uint32_t height, std::string_view label)
        -> Result<ScreenImage, ScreenError>;

    /// Writes `draw`'s rectangles for the frame in flight, for record to draw before the next prepare;
    /// their pictures must last until then. Errors: GpuFailed (room for more rectangles).
    [[nodiscard]] auto prepare(const ScreenDraw& draw) -> Result<void, ScreenError>;
    /// Records the rectangles of the last prepare, in their order, in a render pass with a color target
    /// of desc.colorFormat (and a depth target of desc.depthFormat, untested): after the rest of the
    /// frame, over it. The scissor covers the whole target again after them.
    void record(rhi::CommandList& commands) const;
    /// Draws `draw`: prepares it, then records it. Errors: those of prepare.
    [[nodiscard]] auto draw(rhi::CommandList& commands, const ScreenDraw& draw) -> Result<void, ScreenError>;

private:
    inline explicit ScreenRenderer(rhi::Device& device) noexcept : m_device{&device} {}
    /// The shader, the bind group layouts, a sampler for each filter, and a pipeline for each way a
    /// rectangle is drawn, with a picture and with a color alone.
    [[nodiscard]] auto createPipelines(const ScreenRendererDesc& desc) -> Result<void, rhi::RhiError>;
    /// Makes room in `slot` for `quads` rectangles, at least.
    [[nodiscard]] auto reserve(ScreenFrameSlot& slot, std::size_t quads) -> Result<void, rhi::RhiError>;
    /// A picture of the RGBA rows `texels`, `width` by `height`.
    [[nodiscard]] auto upload(rhi::CommandList& commands, std::span<const std::byte> texels,
                              std::uint32_t width, std::uint32_t height, std::string_view label)
        -> Result<ScreenImage, ScreenError>;

    rhi::Device* m_device;
    ChannelTable m_textureGamma{};
    rhi::OwnedShader m_shader;
    rhi::OwnedBindGroupLayout m_frameLayout;
    rhi::OwnedBindGroupLayout m_imageLayout;
    EnumArray<ScreenFilter, rhi::OwnedSampler> m_samplers;
    EnumArray<ScreenBlend, rhi::OwnedRenderPipeline> m_picturePipelines;
    EnumArray<ScreenBlend, rhi::OwnedRenderPipeline> m_colorPipelines;
    std::vector<ScreenFrameSlot> m_frameSlots;
    std::vector<ScreenBatch> m_batches; ///< Of the last prepare.
    rhi::Extent2d m_targetSize{};       ///< Of the last prepare.
    std::uint64_t m_drawCount = 0;      ///< Prepares so far: the frame slot of the next is this one's.
};

} // namespace nx::render
