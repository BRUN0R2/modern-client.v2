// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

#include <cstddef> // offsetof

module nx.render;

import std;
import nx.core;
import nx.rhi;
import nx.assets;

namespace nx::render {

namespace {

// The bind groups of the screen's pipelines, their bindings and the vertex attributes, as
// shaders/screen.slang declares them.
constexpr std::uint32_t kFrameGroup = 0;
constexpr std::uint32_t kImageGroup = 1;
constexpr std::uint32_t kFrameUniformBinding = 0;
constexpr std::uint32_t kImageBinding = 0;
constexpr std::uint32_t kImageSamplerBinding = 1;

/// The bytes of a palette color, red, green and blue; of a texel, red, green, blue and alpha.
constexpr std::size_t kRgbBytes = 3;
constexpr std::size_t kRgbaBytes = 4;
/// An opaque alpha, and the palette index a masked picture shows through at.
constexpr std::byte kOpaque{255};
constexpr std::size_t kMaskedIndex = 255;
constexpr float kChannelTop = 255.0F;

/// The vertices of a rectangle: two triangles.
constexpr std::uint32_t kQuadVertices = 6;
/// The room a frame starts with, which grows as the frames need, in rectangles.
constexpr std::size_t kFirstQuadCapacity = 256;

/// A corner of a rectangle: where it is in pixels, its texture coordinates, and its color.
struct ScreenVertex {
    float x = 0.0F;
    float y = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;
    float alpha = 0.0F;
};
/// The 4-byte words of a vertex: the position's two, the texture's two and the color's four.
constexpr std::size_t kScreenVertexWords = 8;
static_assert(sizeof(ScreenVertex) == kScreenVertexWords * sizeof(float), "the vertex layout of the screen");

constexpr std::array kScreenVertexAttributes{
    rhi::VertexAttribute{.location = 0, .format = rhi::VertexFormat::Float2, .offset = 0},
    rhi::VertexAttribute{
        .location = 1, .format = rhi::VertexFormat::Float2, .offset = offsetof(ScreenVertex, u)}, // NOLINT
    rhi::VertexAttribute{
        .location = 2, .format = rhi::VertexFormat::Float4, .offset = offsetof(ScreenVertex, red)}}; // NOLINT

/// The frame's uniform: the target's size, which turns pixels into clip space.
struct FrameUniform {
    float width = 0.0F;
    float height = 0.0F;
    float padding0 = 0.0F;
    float padding1 = 0.0F;
};

/// The RGBA texels of `pixels`, palette indices of `palette`, through `textureGamma`; index 255
/// transparent when `mask` says so.
[[nodiscard]] auto paletteTexels(std::span<const std::byte> pixels, std::span<const std::byte> palette,
                                 PictureMask mask, const ChannelTable& textureGamma)
    -> std::vector<std::byte> {
    std::vector<std::byte> texels;
    texels.reserve(pixels.size() * kRgbaBytes);
    for (const std::byte pixel : pixels) {
        const auto index = std::to_integer<std::size_t>(pixel);
        if (mask == PictureMask::Masked && index == kMaskedIndex) {
            texels.insert(texels.end(), kRgbaBytes, std::byte{0});
            continue;
        }
        for (std::size_t which = 0; which < kRgbBytes; ++which) {
            texels.push_back(
                std::byte{textureGamma[std::to_integer<std::size_t>(palette[(index * kRgbBytes) + which])]});
        }
        texels.push_back(kOpaque);
    }
    return texels;
}

/// Appends the two triangles of `quad`, its picture's `source` part, or its color alone.
/// The part of `clip` within `area`; empty when they do not meet.
[[nodiscard]] auto within(const rhi::Rect& clip, const rhi::Rect& area) noexcept -> rhi::Rect {
    const std::int64_t left = std::max<std::int64_t>(clip.x, area.x);
    const std::int64_t top = std::max<std::int64_t>(clip.y, area.y);
    const std::int64_t right =
        std::min(std::int64_t{clip.x} + clip.size.width, std::int64_t{area.x} + area.size.width);
    const std::int64_t bottom =
        std::min(std::int64_t{clip.y} + clip.size.height, std::int64_t{area.y} + area.size.height);
    if (right <= left || bottom <= top) {
        return {};
    }
    return {.x = narrow<std::int32_t>(left),
            .y = narrow<std::int32_t>(top),
            .size = {.width = narrow<std::uint32_t>(right - left),
                     .height = narrow<std::uint32_t>(bottom - top)}};
}

[[nodiscard]] constexpr auto isSameRect(const rhi::Rect& a, const rhi::Rect& b) noexcept -> bool {
    return a.x == b.x && a.y == b.y && a.size.width == b.size.width && a.size.height == b.size.height;
}

void addQuad(std::vector<ScreenVertex>& vertices, const ScreenQuad& quad) {
    const ScreenRect& target = quad.target;
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;
    if (quad.image != nullptr) {
        const auto width = static_cast<float>(quad.image->width());
        const auto height = static_cast<float>(quad.image->height());
        left = static_cast<float>(quad.source.x) / width;
        top = static_cast<float>(quad.source.y) / height;
        right = static_cast<float>(quad.source.x + quad.source.width) / width;
        bottom = static_cast<float>(quad.source.y + quad.source.height) / height;
    }
    const auto corner = [&quad](float x, float y, float u, float v) {
        return ScreenVertex{.x = x,
                            .y = y,
                            .u = u,
                            .v = v,
                            .red = static_cast<float>(quad.color.red) / kChannelTop,
                            .green = static_cast<float>(quad.color.green) / kChannelTop,
                            .blue = static_cast<float>(quad.color.blue) / kChannelTop,
                            .alpha = static_cast<float>(quad.color.alpha) / kChannelTop};
    };
    const ScreenVertex topLeft = corner(target.x, target.y, left, top);
    const ScreenVertex topRight = corner(target.x + target.width, target.y, right, top);
    const ScreenVertex bottomLeft = corner(target.x, target.y + target.height, left, bottom);
    const ScreenVertex bottomRight = corner(target.x + target.width, target.y + target.height, right, bottom);
    vertices.insert(vertices.end(), {topLeft, bottomLeft, bottomRight, topLeft, bottomRight, topRight});
}

} // namespace

auto ScreenFont::widthOf(std::string_view text) const noexcept -> std::uint32_t {
    std::uint32_t width = 0;
    for (const char character : text) {
        width += glyph(character).width;
    }
    return width;
}

auto appendText(std::vector<ScreenQuad>& quads, const ScreenFont& font, std::string_view text, float x,
                float y, ScreenColor color, ScreenBlend blend) -> float {
    for (const char character : text) {
        const assets::FontGlyph& glyph = font.glyph(character);
        quads.push_back(ScreenQuad{
            .image = &font.image(),
            .source = {.x = glyph.x, .y = glyph.y, .width = glyph.width, .height = font.rowHeight()},
            .target = {.x = x,
                       .y = y,
                       .width = static_cast<float>(glyph.width),
                       .height = static_cast<float>(font.rowHeight())},
            .color = color,
            .blend = blend});
        x += static_cast<float>(glyph.width);
    }
    return x;
}

auto spriteQuad(const ScreenImage& image, assets::SpriteRect source, float x, float y, ScreenColor color,
                ScreenBlend blend) -> ScreenQuad {
    return ScreenQuad{.image = &image,
                      .source = source,
                      .target = {.x = x,
                                 .y = y,
                                 .width = static_cast<float>(source.width + 1),
                                 .height = static_cast<float>(source.height + 1)},
                      .color = color,
                      .blend = blend,
                      .filter = ScreenFilter::Linear};
}

auto ScreenRenderer::create(rhi::Device& device, const ScreenRendererDesc& desc)
    -> Result<ScreenRenderer, ScreenError> {
    ScreenRenderer renderer{device};
    renderer.m_textureGamma = textureGammaTable(desc.gamma);
    if (auto made = renderer.createPipelines(desc); !made) {
        return fail(ScreenError::GpuFailed, std::move(made.error()));
    }
    renderer.m_frameSlots.reserve(rhi::kMaxFramesInFlight);
    for (std::uint32_t slot = 0; slot < rhi::kMaxFramesInFlight; ++slot) {
        if (auto reserved = renderer.reserve(renderer.m_frameSlots.emplace_back(), kFirstQuadCapacity);
            !reserved) {
            return fail(ScreenError::GpuFailed, std::move(reserved.error()));
        }
    }
    return renderer;
}

auto ScreenRenderer::createPipelines(const ScreenRendererDesc& desc) -> Result<void, rhi::RhiError> {
    rhi::Device& device = *m_device;
    constexpr Flags<rhi::ShaderStage> kVertex{rhi::ShaderStage::Vertex};
    constexpr Flags<rhi::ShaderStage> kFragment{rhi::ShaderStage::Fragment};
    const std::array frameBindings{rhi::BindingLayout{
        .binding = kFrameUniformBinding, .type = rhi::BindingType::UniformBuffer, .stages = kVertex}};
    const std::array imageBindings{
        rhi::BindingLayout{
            .binding = kImageBinding, .type = rhi::BindingType::SampledTexture, .stages = kFragment},
        rhi::BindingLayout{
            .binding = kImageSamplerBinding, .type = rhi::BindingType::Sampler, .stages = kFragment}};
    auto shader = device.createShader({.label = "screen", .code = desc.shaderCode});
    auto frameLayout = device.createBindGroupLayout({.label = "screen.frame", .bindings = frameBindings});
    auto imageLayout = device.createBindGroupLayout({.label = "screen.image", .bindings = imageBindings});
    if (!shader || !frameLayout || !imageLayout) {
        return std::unexpected(!shader        ? std::move(shader.error())
                               : !frameLayout ? std::move(frameLayout.error())
                                              : std::move(imageLayout.error()));
    }
    m_shader = std::move(*shader);
    m_frameLayout = std::move(*frameLayout);
    m_imageLayout = std::move(*imageLayout);
    using Filters = EnumArray<ScreenFilter, rhi::Filter>;
    const Filters filters{Filters::Storage{rhi::Filter::Nearest, rhi::Filter::Linear}};
    for (const ScreenFilter filter : Filters::keys()) {
        auto sampler = device.createSampler({.label = std::format("screen.image.{}", enumName(filter)),
                                             .magnify = filters[filter],
                                             .minify = filters[filter],
                                             .mipmap = rhi::Filter::Nearest,
                                             .address = rhi::AddressMode::ClampToEdge});
        if (!sampler) {
            return std::unexpected{std::move(sampler.error())};
        }
        m_samplers[filter] = std::move(*sampler);
    }

    // Over everything the frame drew, its depth neither tested nor written.
    const std::array vertexBuffers{
        rhi::VertexBufferLayout{.stride = sizeof(ScreenVertex), .attributes = kScreenVertexAttributes}};
    const std::optional<rhi::DepthState> depth =
        desc.depthFormat == rhi::Format::Undefined
            ? std::nullopt
            : std::optional{rhi::DepthState{
                  .format = desc.depthFormat, .isWriteEnabled = false, .compare = rhi::CompareOp::Always}};
    // As it is; added, as much as its alpha says (GL_SRC_ALPHA, GL_ONE); blended by its alpha. The
    // target's alpha is kept.
    using Blends = EnumArray<ScreenBlend, std::optional<rhi::BlendState>>;
    const Blends blends{
        Blends::Storage{std::nullopt,
                        rhi::BlendState{.sourceColor = rhi::BlendFactor::SourceAlpha,
                                        .destinationColor = rhi::BlendFactor::One,
                                        .sourceAlpha = rhi::BlendFactor::Zero,
                                        .destinationAlpha = rhi::BlendFactor::One},
                        rhi::BlendState{.sourceColor = rhi::BlendFactor::SourceAlpha,
                                        .destinationColor = rhi::BlendFactor::OneMinusSourceAlpha,
                                        .sourceAlpha = rhi::BlendFactor::Zero,
                                        .destinationAlpha = rhi::BlendFactor::One}}};
    const std::array pictureLayouts{m_frameLayout.get(), m_imageLayout.get()};
    const std::array colorLayouts{m_frameLayout.get()};
    for (const ScreenBlend blend : Blends::keys()) {
        const std::array targets{rhi::ColorTarget{.format = desc.colorFormat, .blend = blends[blend]}};
        const auto pipeline = [&](std::string_view label, std::string_view fragment,
                                  std::span<const rhi::BindGroupLayoutHandle> layouts) {
            return device.createRenderPipeline(
                {.label = label,
                 .vertex = {.shader = m_shader.get(), .entryPoint = "vertexMain"},
                 .fragment = {.shader = m_shader.get(), .entryPoint = fragment},
                 .vertexBuffers = vertexBuffers,
                 .colorTargets = targets,
                 .depth = depth,
                 .bindGroupLayouts = layouts});
        };
        auto picture =
            pipeline(std::format("screen.picture.{}", enumName(blend)), "fragmentMain", pictureLayouts);
        auto color =
            pipeline(std::format("screen.color.{}", enumName(blend)), "colorFragmentMain", colorLayouts);
        if (!picture || !color) {
            return std::unexpected(!picture ? std::move(picture.error()) : std::move(color.error()));
        }
        m_picturePipelines[blend] = std::move(*picture);
        m_colorPipelines[blend] = std::move(*color);
    }
    return {};
}

auto ScreenRenderer::reserve(ScreenFrameSlot& slot, std::size_t quads) -> Result<void, rhi::RhiError> {
    rhi::Device& device = *m_device;
    if (slot.frame.get().isNull()) {
        auto frame = device.createBuffer({.label = "screen.frame",
                                          .size = sizeof(FrameUniform),
                                          .usage = {rhi::BufferUsage::Uniform},
                                          .location = rhi::MemoryLocation::Upload});
        if (!frame) {
            return std::unexpected{std::move(frame.error())};
        }
        const std::array bindings{rhi::Binding{.binding = kFrameUniformBinding,
                                               .resource = rhi::BufferBinding{.buffer = frame->get()}}};
        auto group = device.createBindGroup(
            {.label = "screen.frame", .layout = m_frameLayout.get(), .bindings = bindings});
        if (!group) {
            return std::unexpected{std::move(group.error())};
        }
        slot.frame = std::move(*frame);
        slot.group = std::move(*group);
    }
    if (quads > slot.quadCapacity) {
        const std::size_t capacity = std::max({quads, slot.quadCapacity * 2, kFirstQuadCapacity});
        auto vertices = device.createBuffer({.label = "screen.vertices",
                                             .size = capacity * kQuadVertices * sizeof(ScreenVertex),
                                             .usage = {rhi::BufferUsage::Vertex},
                                             .location = rhi::MemoryLocation::Upload});
        if (!vertices) {
            return std::unexpected{std::move(vertices.error())};
        }
        slot.vertices = std::move(*vertices);
        slot.quadCapacity = capacity;
    }
    return {};
}

auto ScreenRenderer::upload(rhi::CommandList& commands, std::span<const std::byte> texels,
                            std::uint32_t width, std::uint32_t height, std::string_view label)
    -> Result<ScreenImage, ScreenError> {
    Uploader uploader{*m_device, commands};
    const std::array offsets{std::size_t{0}};
    auto texture = uploader.texture(label, {.width = width, .height = height}, 1, 1, texels, offsets);
    if (!texture) {
        return fail(ScreenError::GpuFailed, std::string{label}, std::move(texture.error()));
    }
    ScreenImage image;
    for (const ScreenFilter filter : EnumArray<ScreenFilter, rhi::OwnedSampler>::keys()) {
        const std::array bindings{
            rhi::Binding{.binding = kImageBinding, .resource = texture->get()},
            rhi::Binding{.binding = kImageSamplerBinding, .resource = m_samplers[filter].get()}};
        auto group = m_device->createBindGroup({.label = std::format("{}.{}", label, enumName(filter)),
                                                .layout = m_imageLayout.get(),
                                                .bindings = bindings});
        if (!group) {
            return fail(ScreenError::GpuFailed, std::string{label}, std::move(group.error()));
        }
        image.m_groups[filter] = std::move(*group);
    }
    image.m_texture = std::move(*texture);
    image.m_width = width;
    image.m_height = height;
    return image;
}

auto ScreenRenderer::loadSprite(rhi::CommandList& commands, const assets::SpriteView& sprite,
                                std::string_view label) -> Result<std::vector<ScreenImage>, ScreenError> {
    std::vector<ScreenImage> images;
    images.reserve(sprite.frames.size());
    for (const assets::SpriteFrame& frame : sprite.frames) {
        const assets::SpriteImage& first = frame.images.front();
        auto image = upload(commands, spriteTexels(sprite, first, m_textureGamma), first.width, first.height,
                            std::format("screen.{}.{}", label, images.size()));
        if (!image) {
            return std::unexpected{std::move(image.error())};
        }
        images.push_back(std::move(*image));
    }
    return images;
}

auto ScreenRenderer::loadPicture(rhi::CommandList& commands, const assets::WadPicture& picture,
                                 PictureMask mask, std::string_view label)
    -> Result<ScreenImage, ScreenError> {
    return upload(commands, paletteTexels(picture.pixels, picture.palette, mask, m_textureGamma),
                  picture.width, picture.height, std::format("screen.{}", label));
}

auto ScreenRenderer::loadFont(rhi::CommandList& commands, const assets::FontView& font,
                              std::string_view label) -> Result<ScreenFont, ScreenError> {
    auto image = loadPicture(commands, font.picture, PictureMask::Masked, label);
    if (!image) {
        return std::unexpected{std::move(image.error())};
    }
    return ScreenFont{std::move(*image), font.rowHeight, font.glyphs};
}

auto ScreenRenderer::loadImage(rhi::CommandList& commands, std::span<const std::byte> texels,
                               std::uint32_t width, std::uint32_t height, std::string_view label)
    -> Result<ScreenImage, ScreenError> {
    expects(texels.size() == std::size_t{width} * height * 4,
            "ScreenRenderer::loadImage: RGBA texels of its size");
    return upload(commands, texels, width, height, std::format("screen.{}", label));
}

auto ScreenRenderer::prepare(const ScreenDraw& draw) -> Result<void, ScreenError> {
    m_batches.clear();
    m_targetSize = draw.size;
    // A ring of kMaxFramesInFlight: the slot written now was last read frames in flight ago.
    ScreenFrameSlot& slot = m_frameSlots[m_drawCount++ % m_frameSlots.size()];
    if (draw.quads.empty()) {
        return {};
    }
    if (auto reserved = reserve(slot, draw.quads.size()); !reserved) {
        return fail(ScreenError::GpuFailed, std::move(reserved.error()));
    }
    const FrameUniform uniform{.width = static_cast<float>(draw.size.width),
                               .height = static_cast<float>(draw.size.height)};
    std::ranges::copy(std::as_bytes(std::span{&uniform, 1}), m_device->mappedBytes(slot.frame.get()).begin());

    // Each rectangle in its turn, those in a row drawn alike in one batch.
    std::vector<ScreenVertex> vertices;
    vertices.reserve(draw.quads.size() * kQuadVertices);
    const rhi::Rect whole{.x = 0, .y = 0, .size = draw.size};
    for (const ScreenQuad& quad : draw.quads) {
        const rhi::Rect scissor = quad.clip ? within(*quad.clip, whole) : whole;
        if (scissor.size.width == 0 || scissor.size.height == 0) {
            continue; // clipped away
        }
        const ScreenBatch next{.pipeline = quad.image != nullptr ? m_picturePipelines[quad.blend].get()
                                                                 : m_colorPipelines[quad.blend].get(),
                               .image = quad.image != nullptr ? quad.image->m_groups[quad.filter].get()
                                                              : rhi::BindGroupHandle{},
                               .scissor = scissor,
                               .firstQuad = narrow<std::uint32_t>(vertices.size() / kQuadVertices),
                               .quadCount = 1};
        if (!m_batches.empty() && m_batches.back().pipeline == next.pipeline &&
            m_batches.back().image == next.image && isSameRect(m_batches.back().scissor, next.scissor)) {
            ++m_batches.back().quadCount;
        } else {
            m_batches.push_back(next);
        }
        addQuad(vertices, quad);
    }
    std::ranges::copy(std::as_bytes(std::span{vertices}), m_device->mappedBytes(slot.vertices.get()).begin());
    return {};
}

void ScreenRenderer::record(rhi::CommandList& commands) const {
    if (m_batches.empty()) {
        return;
    }
    expects(m_drawCount > 0, "ScreenRenderer: prepared before it is recorded");
    const ScreenFrameSlot& slot = m_frameSlots[(m_drawCount - 1) % m_frameSlots.size()];
    commands.setVertexBuffer(0, slot.vertices.get(), 0);
    for (const ScreenBatch& batch : m_batches) {
        commands.setScissor(batch.scissor);
        commands.setPipeline(batch.pipeline);
        commands.setBindGroup(kFrameGroup, slot.group.get());
        if (!batch.image.isNull()) {
            commands.setBindGroup(kImageGroup, batch.image);
        }
        commands.draw(batch.quadCount * kQuadVertices, 1, batch.firstQuad * kQuadVertices, 0);
    }
    commands.setScissor({.x = 0, .y = 0, .size = m_targetSize});
}

auto ScreenRenderer::draw(rhi::CommandList& commands, const ScreenDraw& draw) -> Result<void, ScreenError> {
    if (auto prepared = prepare(draw); !prepared) {
        return std::unexpected{std::move(prepared.error())};
    }
    record(commands);
    return {};
}

} // namespace nx::render
