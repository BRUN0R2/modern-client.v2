// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

#include <cstddef> // offsetof

module nx.render;

import std;
import nx.core;
import nx.math;
import nx.rhi;
import nx.assets;

namespace nx::render {

namespace {

// The bind groups of the sprite pipelines, their bindings and the vertex attributes, as
// shaders/sprite.slang declares them.
constexpr std::uint32_t kFrameGroup = 0;
constexpr std::uint32_t kImageGroup = 1;
constexpr std::uint32_t kFrameUniformBinding = 0;
constexpr std::uint32_t kImageBinding = 0;
constexpr std::uint32_t kImageSamplerBinding = 1;

/// The palette index an alpha-tested sprite draws as transparent, and the color an index-alpha one
/// is drawn in: the last of its palette.
constexpr std::size_t kLastIndex = 255;
constexpr float kChannelTop = 255.0F;

/// The vertices of a quad: two triangles.
constexpr std::uint32_t kQuadVertices = 6;
/// The room a frame starts with, which grows as the frames need, in quads.
constexpr std::size_t kFirstQuadCapacity = 64;

/// The original's alpha tests: what has an alpha above them is drawn (DEFAULT_ALPHATEST, and that of
/// a sprite lit by the world).
constexpr float kDefaultAlphaTest = 0.0F;
constexpr float kLitAlphaTest = 1.0F / 3.0F;
/// An alpha-tested sprite is lit when its `renderamt` is above this.
constexpr std::uint8_t kMostUnlitAmount = 127;
/// How far down the light of a sprite's origin is looked for (R_LightPoint).
constexpr float kLightReach = 2048.0F;

/// A glowing sprite (R_SpriteGlowBlend): its brightness, GLARE_FALLOFF over its squared distance,
/// held from kLeastGlare to 1; its scale, its distance over kGlowScaleDistance; not drawn when its
/// blend is kLeastGlowBlend or less.
constexpr float kGlareFalloff = 19000.0F;
constexpr float kLeastGlare = 0.05F;
constexpr float kGlowScaleDistance = 200.0F;
constexpr float kLeastGlowBlend = 0.01F;
/// A parallel upright sprite is not drawn when the view looks up or down within a degree of the
/// vertical: the cosine of 1 degree.
constexpr float kNearlyVertical = 0.999848F;
/// An oriented sprite is drawn this far in front of its origin, against z-fighting.
constexpr float kOrientedOffset = 0.01F;

/// A corner of a sprite's quad: where it is, its texture coordinates, its color and opacity, what its
/// fragments' alpha must be above, and its brightness.
struct SpriteVertex {
    Vec3 position{};
    float u = 0.0F;
    float v = 0.0F;
    Vec4 color{};
    float alphaTest = 0.0F;
    float brightness = 1.0F;
};
/// The 4-byte words of a vertex: the position's three, the texture's two, the color's four, the alpha
/// test and the brightness.
constexpr std::size_t kSpriteVertexWords = 11;
static_assert(sizeof(SpriteVertex) == kSpriteVertexWords * sizeof(float),
              "the vertex layout of the sprite pipelines");

constexpr std::array kSpriteVertexAttributes{
    rhi::VertexAttribute{.location = 0, .format = rhi::VertexFormat::Float3, .offset = 0},
    rhi::VertexAttribute{
        .location = 1, .format = rhi::VertexFormat::Float2, .offset = offsetof(SpriteVertex, u)}, // NOLINT
    rhi::VertexAttribute{.location = 2,
                         .format = rhi::VertexFormat::Float4,
                         .offset = offsetof(SpriteVertex, color)}, // NOLINT
    rhi::VertexAttribute{.location = 3,
                         .format = rhi::VertexFormat::Float,
                         .offset = offsetof(SpriteVertex, alphaTest)}, // NOLINT
    rhi::VertexAttribute{.location = 4,
                         .format = rhi::VertexFormat::Float,
                         .offset = offsetof(SpriteVertex, brightness)}}; // NOLINT

/// The frame's uniform: the rows of the view-projection matrix, and how colors reach the target.
struct FrameUniform {
    Mat4 viewProjection{};
    std::uint32_t colorEncoding = 0; ///< ColorEncoding's.
};

/// The image of frame `frame` of `frames`, cut to a whole number and held to them, at `time`: the image
/// of a group whose interval has not passed in its loop (R_GetSpriteFrame).
[[nodiscard]] auto imageOf(std::span<const SpriteFrameDraw> frames, float frame, Seconds time) noexcept
    -> std::uint32_t {
    const std::size_t last = frames.size() - 1;
    std::size_t index = 0; // before the first frame, and not a number, are the first
    if (frame >= 1.0F) {
        index = frame >= static_cast<float>(last) ? last : static_cast<std::size_t>(frame);
    }
    const SpriteFrameDraw& chosen = frames[index];
    if (chosen.intervals.empty()) {
        return chosen.firstImage;
    }
    const double loop = static_cast<double>(chosen.intervals.back());
    const double now = std::max(static_cast<double>(time.count()), 0.0);
    const double into = now - (std::trunc(now / loop) * loop);
    std::uint32_t image = 0;
    while (image + 1 < chosen.imageCount && static_cast<double>(chosen.intervals[image]) <= into) {
        ++image;
    }
    return chosen.firstImage + image;
}

/// Where a sprite's quad is and which ways its right and up go.
struct QuadAxes {
    Vec3 origin{};
    Vec3 right{};
    Vec3 up{};
};

/// The axes of `instance`'s quad as its sprite's `orientation` turns it for `camera`; none when the
/// original does not draw it.
[[nodiscard]] auto axesOf(assets::SpriteOrientation orientation, const SpriteInstance& instance,
                          const Camera& camera) -> std::optional<QuadAxes> {
    using assets::SpriteOrientation;
    constexpr Vec3 kVertical{.z = 1.0F};
    const AngleVectors view = angleVectors(camera.angles);
    // A parallel sprite with a roll is rolled.
    if (instance.angles.roll.value() != 0.0F && orientation == SpriteOrientation::Parallel) {
        orientation = SpriteOrientation::ParallelOriented;
    }
    switch (orientation) {
    case SpriteOrientation::Oriented: {
        const AngleVectors own = angleVectors(instance.angles);
        return QuadAxes{
            .origin = instance.origin - (own.forward * kOrientedOffset), .right = own.right, .up = own.up};
    }
    case SpriteOrientation::FacingUpright: {
        const Vec3 across{.x = instance.origin.y - camera.position.y,
                          .y = -(instance.origin.x - camera.position.x)};
        if (lengthSquared(across) == 0.0F) {
            return std::nullopt; // seen from straight above or below its origin: no width
        }
        return QuadAxes{.origin = instance.origin, .right = normalized(across), .up = kVertical};
    }
    case SpriteOrientation::ParallelUpright:
        if (std::abs(view.forward.z) > kNearlyVertical) {
            return std::nullopt;
        }
        return QuadAxes{.origin = instance.origin,
                        .right = normalized(Vec3{.x = view.forward.y, .y = -view.forward.x}),
                        .up = kVertical};
    case SpriteOrientation::ParallelOriented: {
        const auto [sine, cosine] = detail::sineCosine(instance.angles.roll);
        return QuadAxes{.origin = instance.origin,
                        .right = (view.right * cosine) + (view.up * sine),
                        .up = (view.right * -sine) + (view.up * cosine)};
    }
    case SpriteOrientation::Parallel:
    case SpriteOrientation::Count:
        break;
    }
    return QuadAxes{.origin = instance.origin, .right = view.right, .up = view.up};
}

/// How a quad of a sprite is colored: its color and opacity, what its fragments' alpha must be above,
/// and its brightness.
struct QuadShade {
    Vec4 color{};
    float alphaTest = 0.0F;
    float brightness = 1.0F;
};

/// Appends the quad of `image` on `axes`, `scale` times its size, shaded as `shade` (R_DrawSpriteQuad).
void addQuad(std::vector<SpriteVertex>& vertices, const SpriteImageDraw& image, const QuadAxes& axes,
             float scale, const QuadShade& shade) {
    const auto corner = [&](float along, float across, float u, float v) {
        const Vec3 point = axes.origin + (axes.up * (along * scale)) + (axes.right * (across * scale));
        return SpriteVertex{.position = point,
                            .u = u,
                            .v = v,
                            .color = shade.color,
                            .alphaTest = shade.alphaTest,
                            .brightness = shade.brightness};
    };
    const SpriteVertex downLeft = corner(image.down, image.left, 0.0F, 1.0F);
    const SpriteVertex upLeft = corner(image.up, image.left, 0.0F, 0.0F);
    const SpriteVertex upRight = corner(image.up, image.right, 1.0F, 0.0F);
    const SpriteVertex downRight = corner(image.down, image.right, 1.0F, 1.0F);
    vertices.insert(vertices.end(), {downLeft, upLeft, upRight, downLeft, upRight, downRight});
}

/// Whether the original draws an entity of `mode` in its pass of solid entities.
[[nodiscard]] auto isSolid(RenderMode mode) noexcept -> bool {
    return mode == RenderMode::Normal;
}

} // namespace

auto spriteTexels(const assets::SpriteView& sprite, const assets::SpriteImage& image,
                  const ChannelTable& textureGamma) -> std::vector<std::byte> {
    // texgamma is what GoldSrc gives the palettes of its 8-bit images: an image compressed as BC3
    // reaches the GPU as its author made it.
    if (image.format == assets::SpritePixels::Bc3) {
        return assets::decodeBc3(image.pixels, image.width, image.height).pixels;
    }
    std::vector<std::byte> texels;
    texels.reserve(image.pixels.size() * assets::kRgbaBytes);
    for (const std::byte pixel : image.pixels) {
        const auto index = std::to_integer<std::size_t>(pixel);
        const auto channel = [&sprite](std::size_t entry, std::size_t which) {
            return sprite.palette[(entry * assets::kRgbBytes) + which];
        };
        switch (sprite.rendering) {
        case assets::SpriteRendering::IndexAlpha:
            texels.insert(texels.end(),
                          {channel(kLastIndex, 0), channel(kLastIndex, 1), channel(kLastIndex, 2), pixel});
            break;
        case assets::SpriteRendering::AlphaTest:
            if (index == kLastIndex) {
                texels.insert(texels.end(), assets::kRgbaBytes, std::byte{0});
            } else {
                texels.insert(texels.end(), {channel(index, 0), channel(index, 1), channel(index, 2),
                                             assets::kFullChannel});
            }
            break;
        case assets::SpriteRendering::Normal:
        case assets::SpriteRendering::Additive:
        case assets::SpriteRendering::Count:
            for (std::size_t which = 0; which < assets::kRgbBytes; ++which) {
                texels.push_back(
                    std::byte{textureGamma[std::to_integer<std::size_t>(channel(index, which))]});
            }
            texels.push_back(assets::kFullChannel);
            break;
        }
    }
    return texels;
}

auto SpriteRenderer::create(rhi::Device& device, const SpriteRendererDesc& desc)
    -> Result<SpriteRenderer, SpriteError> {
    SpriteRenderer renderer{device};
    renderer.m_encoding = desc.encoding;
    renderer.m_textureGamma = textureGammaTable(desc.gamma);
    if (auto made = renderer.createPipelines(desc.shaderCode, desc.colorFormat); !made) {
        return fail(SpriteError::GpuFailed, std::move(made.error()));
    }
    renderer.m_frameSlots.reserve(rhi::kMaxFramesInFlight);
    for (std::uint32_t slot = 0; slot < rhi::kMaxFramesInFlight; ++slot) {
        if (auto reserved = renderer.reserve(renderer.m_frameSlots.emplace_back(), kFirstQuadCapacity);
            !reserved) {
            return fail(SpriteError::GpuFailed, std::move(reserved.error()));
        }
    }
    return renderer;
}

auto SpriteRenderer::createPipelines(std::span<const std::byte> shaderCode, rhi::Format colorFormat)
    -> Result<void, rhi::RhiError> {
    rhi::Device& device = *m_device;
    constexpr Flags<rhi::ShaderStage> kBothStages{rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment};
    constexpr Flags<rhi::ShaderStage> kFragment{rhi::ShaderStage::Fragment};
    const std::array frameBindings{rhi::BindingLayout{
        .binding = kFrameUniformBinding, .type = rhi::BindingType::UniformBuffer, .stages = kBothStages}};
    const std::array imageBindings{
        rhi::BindingLayout{
            .binding = kImageBinding, .type = rhi::BindingType::SampledTexture, .stages = kFragment},
        rhi::BindingLayout{
            .binding = kImageSamplerBinding, .type = rhi::BindingType::Sampler, .stages = kFragment}};
    auto shader = device.createShader({.label = "sprite", .code = shaderCode});
    auto frameLayout = device.createBindGroupLayout({.label = "sprite.frame", .bindings = frameBindings});
    auto imageLayout = device.createBindGroupLayout({.label = "sprite.image", .bindings = imageBindings});
    auto sampler = device.createSampler({.label = "sprite.image"});
    if (!shader || !frameLayout || !imageLayout || !sampler) {
        return std::unexpected(!shader        ? std::move(shader.error())
                               : !frameLayout ? std::move(frameLayout.error())
                               : !imageLayout ? std::move(imageLayout.error())
                                              : std::move(sampler.error()));
    }
    m_shader = std::move(*shader);
    m_frameLayout = std::move(*frameLayout);
    m_imageLayout = std::move(*imageLayout);
    m_sampler = std::move(*sampler);

    // As R_DrawSpriteModel sets each render mode: solid sprites opaque; textures and colors blended,
    // writing their depth but for TransAlpha; added ones added, their depth tested but not written,
    // glows over everything. The light multiplies what a lit sprite drew, where it drew it. The
    // triangles the original culls (GL_FRONT) are culled: an oriented sprite seen from behind.
    const std::array vertexBuffers{
        rhi::VertexBufferLayout{.stride = sizeof(SpriteVertex), .attributes = kSpriteVertexAttributes}};
    const std::array layouts{m_frameLayout.get(), m_imageLayout.get()};
    const rhi::BlendState alphaBlend{.sourceColor = rhi::BlendFactor::SourceAlpha,
                                     .destinationColor = rhi::BlendFactor::OneMinusSourceAlpha,
                                     .sourceAlpha = rhi::BlendFactor::SourceAlpha,
                                     .destinationAlpha = rhi::BlendFactor::OneMinusSourceAlpha};
    const rhi::BlendState additiveBlend{.sourceColor = rhi::BlendFactor::SourceAlpha,
                                        .destinationColor = rhi::BlendFactor::One,
                                        .sourceAlpha = rhi::BlendFactor::SourceAlpha,
                                        .destinationAlpha = rhi::BlendFactor::One};
    const rhi::BlendState lightBlend{.sourceColor = rhi::BlendFactor::Zero,
                                     .destinationColor = rhi::BlendFactor::SourceColor,
                                     .sourceAlpha = rhi::BlendFactor::Zero,
                                     .destinationAlpha = rhi::BlendFactor::One};
    const auto pipeline = [&](std::string_view label, std::string_view fragment,
                              std::optional<rhi::BlendState> blend, rhi::DepthState depth) {
        const std::array targets{rhi::ColorTarget{.format = colorFormat, .blend = blend}};
        return device.createRenderPipeline({.label = label,
                                            .vertex = {.shader = m_shader.get(), .entryPoint = "vertexMain"},
                                            .fragment = {.shader = m_shader.get(), .entryPoint = fragment},
                                            .vertexBuffers = vertexBuffers,
                                            .cullMode = rhi::CullMode::Front,
                                            .frontFace = rhi::FrontFace::CounterClockwise,
                                            .colorTargets = targets,
                                            .depth = depth,
                                            .bindGroupLayouts = layouts});
    };
    const rhi::DepthState written{.isWriteEnabled = true, .compare = rhi::CompareOp::Greater};
    const rhi::DepthState tested{.isWriteEnabled = false, .compare = rhi::CompareOp::Greater};
    const rhi::DepthState untested{.isWriteEnabled = false, .compare = rhi::CompareOp::Always};
    const rhi::DepthState onTheSprite{.isWriteEnabled = false, .compare = rhi::CompareOp::Equal};
    auto solid = pipeline("sprite", "fragmentMain", std::nullopt, written);
    auto blended = pipeline("sprite.blended", "fragmentMain", alphaBlend, written);
    auto blendedUnwritten = pipeline("sprite.transAlpha", "fragmentMain", alphaBlend, tested);
    auto glow = pipeline("sprite.glow", "fragmentMain", additiveBlend, untested);
    auto added = pipeline("sprite.additive", "fragmentMain", additiveBlend, tested);
    auto light = pipeline("sprite.light", "lightFragmentMain", lightBlend, onTheSprite);
    for (auto* const created : {&solid, &blended, &blendedUnwritten, &glow, &added, &light}) {
        if (!*created) {
            return std::unexpected{std::move(created->error())};
        }
    }
    m_pipelines[RenderMode::Normal] = std::move(*solid);
    m_pipelines[RenderMode::TransColor] = std::move(*blended);
    m_pipelines[RenderMode::TransAlpha] = std::move(*blendedUnwritten);
    m_pipelines[RenderMode::Glow] = std::move(*glow);
    m_pipelines[RenderMode::TransAdd] = std::move(*added);
    m_lightPipeline = std::move(*light);
    return {};
}

auto SpriteRenderer::pipelineOf(RenderMode mode) const noexcept -> rhi::RenderPipelineHandle {
    // TransTexture blends as TransColor does: one pipeline for both.
    return m_pipelines[mode == RenderMode::TransTexture ? RenderMode::TransColor : mode].get();
}

auto SpriteRenderer::reserve(SpriteFrameSlot& slot, std::size_t quads) -> Result<void, rhi::RhiError> {
    rhi::Device& device = *m_device;
    if (slot.frame.get().isNull()) {
        auto frame = device.createBuffer({.label = "sprite.frame",
                                          .size = sizeof(FrameUniform),
                                          .usage = {rhi::BufferUsage::Uniform},
                                          .location = rhi::MemoryLocation::Upload});
        if (!frame) {
            return std::unexpected{std::move(frame.error())};
        }
        const std::array bindings{rhi::Binding{.binding = kFrameUniformBinding,
                                               .resource = rhi::BufferBinding{.buffer = frame->get()}}};
        auto group = device.createBindGroup(
            {.label = "sprite.frame", .layout = m_frameLayout.get(), .bindings = bindings});
        if (!group) {
            return std::unexpected{std::move(group.error())};
        }
        slot.frame = std::move(*frame);
        slot.group = std::move(*group);
    }
    if (quads > slot.quadCapacity) {
        const std::size_t capacity = std::max({quads, slot.quadCapacity * 2, kFirstQuadCapacity});
        auto vertices = device.createBuffer({.label = "sprite.vertices",
                                             .size = capacity * kQuadVertices * sizeof(SpriteVertex),
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

auto SpriteRenderer::load(rhi::CommandList& commands, const assets::SpriteView& sprite,
                          std::string_view label) -> Result<SpriteModel, SpriteError> {
    Uploader uploader{*m_device, commands};
    SpriteModel model;
    model.m_orientation = sprite.orientation;
    model.m_rendering = sprite.rendering;
    model.m_halfWidth = static_cast<float>(sprite.maxWidth) / 2.0F;
    model.m_halfHeight = static_cast<float>(sprite.maxHeight) / 2.0F;
    for (const assets::SpriteFrame& frame : sprite.frames) {
        SpriteFrameDraw& drawn = model.m_frames.emplace_back(
            SpriteFrameDraw{.firstImage = narrow<std::uint32_t>(model.m_images.size()),
                            .imageCount = narrow<std::uint32_t>(frame.images.size())});
        std::ranges::transform(frame.intervals, std::back_inserter(drawn.intervals),
                               [](Seconds interval) { return interval.count(); });
        for (const assets::SpriteImage& image : frame.images) {
            const MipChain texels = colorMipChainOf(spriteTexels(sprite, image, m_textureGamma), image.width,
                                                    image.height, m_encoding);
            const std::string name = std::format("sprite.{}.{}", label, model.m_images.size());
            auto texture = uploader.texture(name, {.width = image.width, .height = image.height},
                                            narrow<std::uint32_t>(texels.offsets.size()), 1, texels.texels,
                                            texels.offsets, colorFormatOf(m_encoding));
            if (!texture) {
                return fail(SpriteError::GpuFailed, std::move(texture.error()));
            }
            const std::array bindings{
                rhi::Binding{.binding = kImageBinding, .resource = texture->get()},
                rhi::Binding{.binding = kImageSamplerBinding, .resource = m_sampler.get()}};
            auto group = m_device->createBindGroup(
                {.label = name, .layout = m_imageLayout.get(), .bindings = bindings});
            if (!group) {
                return fail(SpriteError::GpuFailed, std::move(group.error()));
            }
            // The image's place about the sprite's origin (Mod_LoadSpriteFrame).
            model.m_images.push_back(SpriteImageDraw{
                .texture = std::move(*texture),
                .group = std::move(*group),
                .left = static_cast<float>(image.originX),
                .right = static_cast<float>(image.originX) + static_cast<float>(image.width),
                .up = static_cast<float>(image.originY),
                .down = static_cast<float>(image.originY) - static_cast<float>(image.height)});
        }
    }
    return model;
}

auto SpriteRenderer::prepare(const SpriteDraw& draw, const WorldLight& light) -> Result<void, SpriteError> {
    m_quadCalls.clear();
    m_spriteCalls.clear();
    m_blendedDraws.clear();
    // A ring of kMaxFramesInFlight: the slot written now was last read frames in flight ago.
    SpriteFrameSlot& slot = m_frameSlots[m_drawCount++ % m_frameSlots.size()];
    const FrameUniform uniform{.viewProjection =
                                   inLayer(viewProjectionMatrix(draw.camera, draw.aspect), draw.depth),
                               .colorEncoding = std::to_underlying(m_encoding)};
    std::ranges::copy(std::as_bytes(std::span{&uniform, 1}), m_device->mappedBytes(slot.frame.get()).begin());

    // Each sprite's quad, and its light's after it when the world lights it: the solid ones drawn with
    // the solid things of the frame, after the models (R_DrawEntitiesOnList); the others among
    // everything drawn blended, by their origin, the middle of their bounds (R_TransEntityCompare).
    std::vector<SpriteVertex> vertices;
    const auto frustum = frustumPlanes(draw.camera, draw.aspect);
    const Mat4 toClip = viewProjectionMatrix(draw.camera, draw.aspect);
    for (const SpriteInstance& instance : draw.instances) {
        const SpriteModel& model = instance.model;
        if (model.m_frames.empty()) {
            continue;
        }
        float scale = instance.scale > 0.0F ? instance.scale : 1.0F;
        float blend = isSolid(instance.mode) ? 1.0F : static_cast<float>(instance.amount) / kChannelTop;
        if (!isSolid(instance.mode) && instance.amount == 0) {
            continue; // invisible (R_AddEntity)
        }
        if (instance.mode == RenderMode::Glow) {
            // R_SpriteOccluded: on the screen, seen by the game, and dimmer and larger with distance.
            const Vec4 clip = toClip * extend(instance.origin, 1.0F);
            if (clip.w <= 0.0F || std::abs(clip.x) > clip.w || std::abs(clip.y) > clip.w ||
                instance.isHidden) {
                continue;
            }
            if (instance.effect != RenderEffect::NoDissipation) {
                const float distance = length(instance.origin - draw.camera.position);
                blend *= std::clamp(kGlareFalloff / (distance * distance), kLeastGlare, 1.0F);
                scale *= distance * (1.0F / kGlowScaleDistance);
            }
            if (blend <= kLeastGlowBlend) {
                continue;
            }
        } else {
            const Vec3 half{.x = model.m_halfWidth * scale,
                            .y = model.m_halfWidth * scale,
                            .z = model.m_halfHeight * scale};
            const Aabb box = Aabb::around(instance.origin, half);
            if (std::ranges::any_of(
                    frustum, [&box](const Plane& plane) { return plane.sideOf(box) == PlaneSide::Back; })) {
                continue;
            }
        }
        const std::optional<QuadAxes> axes = axesOf(model.m_orientation, instance, draw.camera);
        if (!axes) {
            continue;
        }
        const bool isLit = model.m_rendering == assets::SpriteRendering::AlphaTest &&
                           instance.amount > kMostUnlitAmount &&
                           (instance.mode == RenderMode::Normal || instance.mode == RenderMode::TransAlpha ||
                            instance.mode == RenderMode::TransTexture);
        const RenderColor& color = instance.color;
        const Vec4 tint = color == RenderColor{} ? Vec4{.x = 1.0F, .y = 1.0F, .z = 1.0F, .w = blend}
                                                 : Vec4{.x = static_cast<float>(color.red) / kChannelTop,
                                                        .y = static_cast<float>(color.green) / kChannelTop,
                                                        .z = static_cast<float>(color.blue) / kChannelTop,
                                                        .w = blend};
        const SpriteImageDraw& image = model.m_images[imageOf(model.m_frames, instance.frame, draw.time)];
        const auto firstQuad = narrow<std::uint32_t>(m_quadCalls.size());
        addQuad(vertices, image, *axes, scale,
                {.color = tint,
                 .alphaTest = isLit ? kLitAlphaTest : kDefaultAlphaTest,
                 .brightness = instance.brightness});
        m_quadCalls.push_back(
            SpriteQuadCall{.pipeline = pipelineOf(instance.mode), .image = image.group.get()});
        if (isLit) {
            const LightColor lit =
                light.lightAlong(instance.origin, instance.origin - Vec3{.z = kLightReach}, draw.lightStyles);
            addQuad(vertices, image, *axes, scale,
                    {.color = Vec4{.x = static_cast<float>(lit.red) / kChannelTop,
                                   .y = static_cast<float>(lit.green) / kChannelTop,
                                   .z = static_cast<float>(lit.blue) / kChannelTop,
                                   .w = blend},
                     .alphaTest = kDefaultAlphaTest,
                     .brightness = 1.0F});
            m_quadCalls.push_back(
                SpriteQuadCall{.pipeline = m_lightPipeline.get(), .image = image.group.get()});
        }
        if (!isSolid(instance.mode)) {
            m_blendedDraws.push_back(
                BlendedDraw{.distance = lengthSquared(draw.camera.position - instance.origin),
                            .mode = instance.mode,
                            .index = narrow<std::uint32_t>(m_spriteCalls.size())});
        }
        m_spriteCalls.push_back(SpriteCall{.firstQuad = firstQuad,
                                           .quadCount = narrow<std::uint32_t>(m_quadCalls.size()) - firstQuad,
                                           .isSolid = isSolid(instance.mode)});
    }
    if (m_quadCalls.empty()) {
        return {};
    }
    if (auto reserved = reserve(slot, m_quadCalls.size()); !reserved) {
        m_quadCalls.clear();
        m_spriteCalls.clear();
        m_blendedDraws.clear();
        return fail(SpriteError::GpuFailed, std::move(reserved.error()));
    }
    std::ranges::copy(std::as_bytes(std::span{vertices}), m_device->mappedBytes(slot.vertices.get()).begin());
    return {};
}

auto SpriteRenderer::draw(rhi::CommandList& commands, const SpriteDraw& draw, const WorldLight& light)
    -> Result<void, SpriteError> {
    if (auto prepared = prepare(draw, light); !prepared) {
        return std::unexpected{std::move(prepared.error())};
    }
    drawInOrder(commands, *this);
    return {};
}

void SpriteRenderer::drawSolid(rhi::CommandList& commands) const {
    for (const SpriteCall& sprite : m_spriteCalls) {
        if (sprite.isSolid) {
            drawSprite(commands, sprite);
        }
    }
}

void SpriteRenderer::drawBlended(rhi::CommandList& commands, std::uint32_t index) const {
    if (index >= m_spriteCalls.size()) {
        expects(false, "SpriteRenderer::drawBlended: one of blendedDraws()'s");
        return;
    }
    drawSprite(commands, m_spriteCalls[index]);
}

auto SpriteRenderer::preparedSlot() const noexcept -> const SpriteFrameSlot& {
    expects(m_drawCount > 0, "SpriteRenderer: prepared before it is drawn");
    return m_frameSlots[(m_drawCount - 1) % m_frameSlots.size()];
}

void SpriteRenderer::drawSprite(rhi::CommandList& commands, const SpriteCall& sprite) const {
    const SpriteFrameSlot& slot = preparedSlot();
    commands.setVertexBuffer(0, slot.vertices.get(), 0);
    for (std::uint32_t quad = sprite.firstQuad; quad < sprite.firstQuad + sprite.quadCount; ++quad) {
        commands.setPipeline(m_quadCalls[quad].pipeline);
        commands.setBindGroup(kFrameGroup, slot.group.get());
        commands.setBindGroup(kImageGroup, m_quadCalls[quad].image);
        commands.draw(kQuadVertices, 1, quad * kQuadVertices, 0);
    }
}

} // namespace nx::render
