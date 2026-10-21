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

constexpr log::Category kLog{"render"};

/// Map units per lightmap sample: GoldSrc's luxel.
constexpr double kLuxelUnits = 16.0;
/// The width of the lightmap atlas; its height grows with the map.
constexpr std::uint32_t kAtlasWidth = 1024;
/// The checkerboard of a texture no WAD has: its size and its squares.
constexpr std::uint32_t kCheckerSize = 16;
constexpr std::uint32_t kCheckerSquare = 8;
/// The palette index a masked (`{`) texture draws as transparent.
constexpr std::uint8_t kTransparentIndex = 255;

/// The textures compilers leave on brush entities for the game, never drawn: the triggers' and the
/// tool textures that survive compiling.
constexpr auto kInvisibleTextures =
    std::to_array<std::string_view>({"aaatrigger", "clip", "origin", "null", "skip", "hint", "bevel"});

struct WorldVertex {
    Vec3 position{};
    float u = 0.0F;
    float v = 0.0F;
    float lightmapU = 0.0F;
    float lightmapV = 0.0F;
};
static_assert(sizeof(WorldVertex) == 7 * sizeof(float), "the vertex layout of the world pipeline");

/// The frame's uniform: the rows of the view-projection matrix, and in `lighting.x` what a
/// lightmap sample is multiplied by to index the light table (lightTableScale).
struct FrameUniform {
    Mat4 viewProjection{};
    Vec4 lighting{};
};

/// How a face is drawn, from its texture's name.
enum class SurfaceKind : std::uint8_t {
    Opaque,
    Masked,    ///< `{`: palette index 255 is transparent.
    Sky,       ///< Drawn by the sky pass (to come), not here.
    Invisible, ///< Triggers and tool textures.
};

[[nodiscard]] auto surfaceKindOf(std::string_view name) noexcept -> SurfaceKind {
    if (equalsIgnoringCase(name, "sky")) {
        return SurfaceKind::Sky;
    }
    if (std::ranges::any_of(kInvisibleTextures, [name](std::string_view invisible) {
            return equalsIgnoringCase(name, invisible);
        })) {
        return SurfaceKind::Invisible;
    }
    return name.starts_with('{') ? SurfaceKind::Masked : SurfaceKind::Opaque;
}

/// The RGBA texels of the mip levels of `texture`, level after level.
struct TexelLevels {
    std::vector<std::byte> bytes;
    std::array<std::size_t, assets::kMipLevels> offsets{};
};

[[nodiscard]] auto texelsOf(const assets::MipTexture& texture, bool isMasked,
                            const std::array<std::uint8_t, 256>& textureGamma) -> TexelLevels {
    TexelLevels result;
    for (std::size_t level = 0; level < assets::kMipLevels; ++level) {
        result.offsets.at(level) = result.bytes.size();
        for (const std::byte index : texture.levels.at(level)) {
            const auto entry = std::to_integer<std::size_t>(index);
            const bool isTransparent = isMasked && entry == kTransparentIndex;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const auto value = std::to_integer<std::size_t>(texture.palette[(entry * 3) + channel]);
                result.bytes.push_back(isTransparent ? std::byte{0} : std::byte{textureGamma[value]});
            }
            result.bytes.push_back(isTransparent ? std::byte{0} : std::byte{255});
        }
    }
    return result;
}

/// A magenta and black checkerboard: the texture of a name no WAD has.
[[nodiscard]] auto checkerTexels() -> std::vector<std::byte> {
    std::vector<std::byte> texels;
    for (std::uint32_t y = 0; y < kCheckerSize; ++y) {
        for (std::uint32_t x = 0; x < kCheckerSize; ++x) {
            const bool isMagenta = ((x / kCheckerSquare) + (y / kCheckerSquare)) % 2 == 0;
            const std::byte level = isMagenta ? std::byte{255} : std::byte{0};
            texels.insert(texels.end(), {level, std::byte{0}, level, std::byte{255}});
        }
    }
    return texels;
}

/// The extent of a face's lightmap, in luxels, and the texture coordinates of its first luxel.
struct LightmapExtent {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    double minS = 0.0; ///< In texels: the first luxel's s is minS, the next minS + 16.
    double minT = 0.0;
};

/// Packs rectangles in rows of the atlas width, each row as tall as its tallest rectangle.
class ShelfPacker {
public:
    /// The top-left corner of a free `width` x `height` area.
    [[nodiscard]] auto place(std::uint32_t width, std::uint32_t height) noexcept
        -> std::array<std::uint32_t, 2> {
        if (m_x + width > kAtlasWidth) {
            m_y += m_rowHeight;
            m_x = 0;
            m_rowHeight = 0;
        }
        const std::array corner{m_x, m_y};
        m_x += width;
        m_rowHeight = std::max(m_rowHeight, height);
        return corner;
    }

    [[nodiscard]] auto height() const noexcept -> std::uint32_t { return m_y + m_rowHeight; }

private:
    std::uint32_t m_x = 0;
    std::uint32_t m_y = 0;
    std::uint32_t m_rowHeight = 0;
};

/// The three numbers of an entity's vector value ("-512 1024 36"); nullopt for other text.
[[nodiscard]] auto vectorOf(std::string_view text) -> std::optional<Vec3> {
    std::array<float, 3> values{};
    const char* next = text.data();
    const char* const end = text.data() + text.size();
    for (float& value : values) {
        while (next != end && *next == ' ') {
            ++next;
        }
        const auto [stop, error] = std::from_chars(next, end, value);
        if (error != std::errc{}) {
            return std::nullopt;
        }
        next = stop;
    }
    return Vec3{.x = values[0], .y = values[1], .z = values[2]};
}

/// The translation of each model, from the `origin` of the entity that uses it ("model" "*3").
[[nodiscard]] auto modelOrigins(const assets::BspView& map) -> std::vector<Vec3> {
    std::vector<Vec3> origins(map.models().size());
    for (const assets::BspEntity& entity : map.entities()) {
        const auto model = entity.find("model");
        const auto origin = entity.find("origin");
        if (!model || !origin || !model->starts_with('*')) {
            continue;
        }
        std::size_t index = 0;
        const auto [stop, error] = std::from_chars(model->data() + 1, model->data() + model->size(), index);
        const std::optional<Vec3> position = vectorOf(*origin);
        if (error != std::errc{} || index >= origins.size() || !position) {
            continue;
        }
        origins[index] = *position;
    }
    return origins;
}

/// Writes `bytes` into a new Upload buffer, to copy from in this frame.
[[nodiscard]] auto stage(rhi::Device& device, std::string_view label, std::span<const std::byte> bytes)
    -> Result<rhi::BufferHandle, rhi::RhiError> {
    auto buffer = device.createBuffer({.label = label,
                                       .size = std::max<std::uint64_t>(bytes.size(), 1),
                                       .usage = {rhi::BufferUsage::CopySource},
                                       .location = rhi::MemoryLocation::Upload});
    if (buffer) {
        std::ranges::copy(bytes, device.mappedBytes(*buffer).begin());
    }
    return buffer;
}

} // namespace

auto BspWorld::create(rhi::Device& device, rhi::CommandList& commands, const BspWorldDesc& desc)
    -> Result<BspWorld, WorldError> {
    const assets::BspView& map = desc.map;
    BspWorld world{device};
    const auto gpuFailure = [](Failure<rhi::RhiError> failure) {
        return fail(WorldError::GpuFailed, std::move(failure));
    };
    std::vector<rhi::BufferHandle> staging; // destroyed once recorded; the device keeps them for the copies
    const auto uploadTexture =
        [&](std::string_view label, rhi::Extent2d extent, std::uint32_t mipLevels,
            std::span<const std::byte> texels,
            std::span<const std::size_t> levelOffsets) -> Result<rhi::TextureHandle, rhi::RhiError> {
        auto texture =
            device.createTexture({.label = label,
                                  .format = rhi::Format::Rgba8Unorm,
                                  .extent = extent,
                                  .mipLevels = mipLevels,
                                  .usage = {rhi::TextureUsage::Sampled, rhi::TextureUsage::CopyDestination}});
        if (!texture) {
            return texture;
        }
        auto source = stage(device, label, texels);
        if (!source) {
            device.destroy(*texture);
            return std::unexpected{std::move(source.error())};
        }
        staging.push_back(*source);
        commands.transition(*texture, rhi::TextureState::Undefined, rhi::TextureState::CopyDestination);
        for (std::uint32_t level = 0; level < mipLevels; ++level) {
            commands.copyBufferToTexture({.buffer = *source,
                                          .bufferOffset = levelOffsets[level],
                                          .texture = *texture,
                                          .mipLevel = level,
                                          .extent = {.width = std::max(extent.width >> level, 1U),
                                                     .height = std::max(extent.height >> level, 1U)}});
        }
        commands.transition(*texture, rhi::TextureState::CopyDestination, rhi::TextureState::ShaderRead);
        world.m_textures.push_back(*texture);
        return texture;
    };

    // The textures: the map's, or the first WAD's that has the name, through `texgamma`; a
    // checkerboard otherwise.
    const std::array<std::uint8_t, 256> textureGamma = textureGammaTable(desc.gamma);
    const std::vector<std::byte> checker = checkerTexels();
    const std::array<std::size_t, 1> oneLevel{0}; // the level offsets of a texture without mips
    auto checkerTexture =
        uploadTexture("world.missing", {.width = kCheckerSize, .height = kCheckerSize}, 1, checker, oneLevel);
    if (!checkerTexture) {
        return gpuFailure(std::move(checkerTexture.error()));
    }
    struct TextureUse {
        rhi::TextureHandle texture{};
        SurfaceKind kind = SurfaceKind::Opaque;
        float width = 1.0F;
        float height = 1.0F;
    };
    const TextureUse missing{.texture = *checkerTexture,
                             .width = static_cast<float>(kCheckerSize),
                             .height = static_cast<float>(kCheckerSize)};
    std::vector<TextureUse> textureUses(map.textures().size(), missing);
    for (std::size_t index = 0; index < map.textures().size(); ++index) {
        const std::optional<assets::MipTexture>& slot = map.textures()[index];
        if (!slot) {
            continue;
        }
        TextureUse& use = textureUses[index];
        use.kind = surfaceKindOf(slot->name);
        use.width = static_cast<float>(slot->width);
        use.height = static_cast<float>(slot->height);
        if (use.kind == SurfaceKind::Sky || use.kind == SurfaceKind::Invisible) {
            continue;
        }
        std::optional<assets::MipTexture> pixels = slot->hasPixels() ? slot : std::nullopt;
        for (std::size_t wad = 0; wad < desc.wads.size() && !pixels; ++wad) {
            if (const assets::WadEntry* entry = desc.wads[wad].find(slot->name)) {
                if (auto read = assets::readTexture(*entry); read && read->hasPixels()) {
                    pixels = std::move(*read);
                }
            }
        }
        if (!pixels) {
            world.m_stats.missingTextures.push_back(slot->name);
            continue;
        }
        const TexelLevels texels = texelsOf(*pixels, use.kind == SurfaceKind::Masked, textureGamma);
        auto texture = uploadTexture(std::format("world.texture.{}", slot->name),
                                     {.width = pixels->width, .height = pixels->height},
                                     narrow<std::uint32_t>(assets::kMipLevels), texels.bytes, texels.offsets);
        if (!texture) {
            return gpuFailure(std::move(texture.error()));
        }
        use.texture = *texture;
        ++world.m_stats.textures;
    }

    // The faces to draw: the world's, and those of the brush entities where their entities put them.
    const std::vector<Vec3> origins = modelOrigins(map);
    struct FaceToDraw {
        const assets::BspFace* face = nullptr;
        Vec3 origin{};
        std::size_t texture = 0;
        std::optional<LightmapExtent> lightmap{};
    };
    std::vector<FaceToDraw> faces;
    for (std::size_t model = 0; model < map.models().size(); ++model) {
        const assets::BspModel& bspModel = map.models()[model];
        for (std::uint32_t offset = 0; offset < bspModel.faceCount; ++offset) {
            const assets::BspFace& face = map.faces()[bspModel.firstFace.value + offset];
            const assets::TextureInfo& info = map.textureInfos()[face.textureInfo.value];
            const std::size_t texture = info.texture.value;
            const SurfaceKind kind =
                texture < textureUses.size() ? textureUses[texture].kind : SurfaceKind::Opaque;
            if (kind == SurfaceKind::Sky || kind == SurfaceKind::Invisible) {
                continue;
            }
            faces.push_back(FaceToDraw{.face = &face, .origin = origins[model], .texture = texture});
        }
    }

    // The lightmaps: each lit face's extent in luxels, computed in double precision as the
    // compilers compute it, packed into the atlas after a white luxel for the unlit faces.
    const auto pointOf = [&map](const assets::BspFace& face, std::uint32_t edge) -> Vec3 {
        const assets::SurfaceEdge& surfaceEdge = map.surfaceEdges()[face.firstEdge.value + edge];
        const assets::BspEdge& bspEdge = map.edges()[surfaceEdge.edge.value];
        return map.vertices()[bspEdge.vertices[surfaceEdge.isReversed ? 1 : 0].value];
    };
    const auto coordinateOf = [](Vec3 point, Vec4 axis) {
        return (double{point.x} * double{axis.x}) + (double{point.y} * double{axis.y}) +
               (double{point.z} * double{axis.z}) + double{axis.w};
    };
    ShelfPacker packer;
    std::ignore = packer.place(1, 1); // the white luxel at (0, 0)
    std::vector<std::array<std::uint32_t, 2>> corners(faces.size());
    for (std::size_t index = 0; index < faces.size(); ++index) {
        FaceToDraw& entry = faces[index];
        const assets::TextureInfo& info = map.textureInfos()[entry.face->textureInfo.value];
        if (info.isSpecial || !entry.face->lightmapOffset) {
            continue;
        }
        double minS = std::numeric_limits<double>::max();
        double maxS = std::numeric_limits<double>::lowest();
        double minT = minS;
        double maxT = maxS;
        for (std::uint32_t edge = 0; edge < entry.face->edgeCount; ++edge) {
            const Vec3 point = pointOf(*entry.face, edge);
            minS = std::min(minS, coordinateOf(point, info.s));
            maxS = std::max(maxS, coordinateOf(point, info.s));
            minT = std::min(minT, coordinateOf(point, info.t));
            maxT = std::max(maxT, coordinateOf(point, info.t));
        }
        const double firstS = std::floor(minS / kLuxelUnits);
        const double firstT = std::floor(minT / kLuxelUnits);
        const auto width = tryRound<std::uint32_t>(std::ceil(maxS / kLuxelUnits) - firstS + 1.0);
        const auto height = tryRound<std::uint32_t>(std::ceil(maxT / kLuxelUnits) - firstT + 1.0);
        if (!width || !height || *width > kAtlasWidth || *height > kAtlasWidth) {
            continue; // an extent no compiler writes: drawn unlit
        }
        const LightmapExtent extent{
            .width = *width, .height = *height, .minS = firstS * kLuxelUnits, .minT = firstT * kLuxelUnits};
        const std::size_t sampleBytes = std::size_t{extent.width} * extent.height * 3;
        if (*entry.face->lightmapOffset + sampleBytes > map.lighting().size()) {
            continue; // samples the file does not hold: drawn unlit
        }
        entry.lightmap = extent;
        corners[index] = packer.place(extent.width, extent.height);
    }
    const rhi::Extent2d atlasExtent{.width = kAtlasWidth, .height = std::max(packer.height(), 1U)};
    std::vector<std::byte> atlas(std::size_t{atlasExtent.width} * atlasExtent.height * 4, std::byte{0});
    std::ranges::fill(std::span{atlas}.first(4), std::byte{255});
    for (std::size_t index = 0; index < faces.size(); ++index) {
        const FaceToDraw& entry = faces[index];
        if (!entry.lightmap) {
            continue;
        }
        const std::span<const std::byte> samples = map.lighting().subspan(*entry.face->lightmapOffset);
        for (std::uint32_t y = 0; y < entry.lightmap->height; ++y) {
            for (std::uint32_t x = 0; x < entry.lightmap->width; ++x) {
                const std::size_t from = (std::size_t{y} * entry.lightmap->width + x) * 3;
                const std::size_t to =
                    ((std::size_t{corners[index][1] + y} * atlasExtent.width) + corners[index][0] + x) * 4;
                atlas[to] = samples[from];
                atlas[to + 1] = samples[from + 1];
                atlas[to + 2] = samples[from + 2];
                atlas[to + 3] = std::byte{255};
            }
        }
    }
    auto atlasTexture = uploadTexture("world.lightmaps", atlasExtent, 1, atlas, oneLevel);
    if (!atlasTexture) {
        return gpuFailure(std::move(atlasTexture.error()));
    }

    // The light table: what the summed light of a sample is drawn as (nx.render:gamma).
    const std::array<std::uint8_t, kLightTableSize> levels = lightTable(desc.gamma);
    std::vector<std::byte> lightTexels;
    lightTexels.reserve(levels.size() * 4);
    for (const std::uint8_t level : levels) {
        lightTexels.insert(lightTexels.end(),
                           {std::byte{level}, std::byte{level}, std::byte{level}, std::byte{255}});
    }
    auto lightTableTexture =
        uploadTexture("world.lightTable", {.width = narrow<std::uint32_t>(kLightTableSize), .height = 1}, 1,
                      lightTexels, oneLevel);
    if (!lightTableTexture) {
        return gpuFailure(std::move(lightTableTexture.error()));
    }
    world.m_lightScale = lightTableScale(desc.gamma);

    // The geometry: each face a triangle fan, its triangles grouped by texture.
    std::vector<WorldVertex> vertices;
    std::vector<std::vector<std::uint32_t>> indicesByTexture(textureUses.size() + 1);
    for (std::size_t index = 0; index < faces.size(); ++index) {
        const FaceToDraw& entry = faces[index];
        const assets::TextureInfo& info = map.textureInfos()[entry.face->textureInfo.value];
        const TextureUse& use = entry.texture < textureUses.size() ? textureUses[entry.texture] : missing;
        const auto first = narrow<std::uint32_t>(vertices.size());
        for (std::uint32_t edge = 0; edge < entry.face->edgeCount; ++edge) {
            const Vec3 point = pointOf(*entry.face, edge);
            const double s = coordinateOf(point, info.s);
            const double t = coordinateOf(point, info.t);
            WorldVertex vertex{.position = point + entry.origin,
                               .u = static_cast<float>(s / double{use.width}),
                               .v = static_cast<float>(t / double{use.height}),
                               .lightmapU = 0.5F / static_cast<float>(atlasExtent.width),
                               .lightmapV = 0.5F / static_cast<float>(atlasExtent.height)};
            if (entry.lightmap) {
                // Luxel centers: the first luxel's s is minS, 16 texels apart.
                vertex.lightmapU = static_cast<float>(
                    ((s - entry.lightmap->minS) / kLuxelUnits + 0.5 + corners[index][0]) / atlasExtent.width);
                vertex.lightmapV =
                    static_cast<float>(((t - entry.lightmap->minT) / kLuxelUnits + 0.5 + corners[index][1]) /
                                       atlasExtent.height);
            }
            vertices.push_back(vertex);
        }
        std::vector<std::uint32_t>& indices = indicesByTexture[std::min(entry.texture, textureUses.size())];
        for (std::uint32_t corner = 2; corner < entry.face->edgeCount; ++corner) {
            indices.insert(indices.end(), {first, first + corner - 1, first + corner});
        }
        ++world.m_stats.faces;
    }

    // The pipeline and its bind groups: per frame, the camera, the lightmaps and the light table; per
    // batch, its texture.
    auto shader = device.createShader({.label = "world", .code = desc.shaderCode});
    if (!shader) {
        return gpuFailure(std::move(shader.error()));
    }
    world.m_shader = *shader;
    const std::array frameBindings{
        rhi::BindingLayout{
            .binding = 0,
            .type = rhi::BindingType::UniformBuffer,
            .stages = {rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment}}, // the matrix; the light scale
        rhi::BindingLayout{
            .binding = 1, .type = rhi::BindingType::SampledTexture, .stages = {rhi::ShaderStage::Fragment}},
        rhi::BindingLayout{
            .binding = 2, .type = rhi::BindingType::Sampler, .stages = {rhi::ShaderStage::Fragment}},
        rhi::BindingLayout{
            .binding = 3, .type = rhi::BindingType::SampledTexture, .stages = {rhi::ShaderStage::Fragment}}};
    const std::array textureBindings{
        rhi::BindingLayout{
            .binding = 0, .type = rhi::BindingType::SampledTexture, .stages = {rhi::ShaderStage::Fragment}},
        rhi::BindingLayout{
            .binding = 1, .type = rhi::BindingType::Sampler, .stages = {rhi::ShaderStage::Fragment}}};
    auto frameLayout = device.createBindGroupLayout({.label = "world.frame", .bindings = frameBindings});
    auto textureLayout =
        device.createBindGroupLayout({.label = "world.texture", .bindings = textureBindings});
    auto textureSampler = device.createSampler({.label = "world.texture"});
    auto lightmapSampler = device.createSampler({.label = "world.lightmap",
                                                 .mipmap = rhi::Filter::Nearest,
                                                 .address = rhi::AddressMode::ClampToEdge});
    if (!frameLayout || !textureLayout || !textureSampler || !lightmapSampler) {
        return gpuFailure(!frameLayout      ? std::move(frameLayout.error())
                          : !textureLayout  ? std::move(textureLayout.error())
                          : !textureSampler ? std::move(textureSampler.error())
                                            : std::move(lightmapSampler.error()));
    }
    world.m_frameLayout = *frameLayout;
    world.m_textureLayout = *textureLayout;
    world.m_textureSampler = *textureSampler;
    world.m_lightmapSampler = *lightmapSampler;

    const std::array attributes{
        rhi::VertexAttribute{.location = 0, .format = rhi::VertexFormat::Float3, .offset = 0},
        rhi::VertexAttribute{
            .location = 1, .format = rhi::VertexFormat::Float2, .offset = offsetof(WorldVertex, u)}, // NOLINT
        rhi::VertexAttribute{.location = 2,
                             .format = rhi::VertexFormat::Float2,
                             .offset = offsetof(WorldVertex, lightmapU)}}; // NOLINT
    const std::array vertexBuffers{
        rhi::VertexBufferLayout{.stride = sizeof(WorldVertex), .attributes = attributes}};
    const std::array targets{rhi::ColorTarget{.format = desc.colorFormat}};
    const std::array layouts{world.m_frameLayout, world.m_textureLayout};
    auto pipeline = device.createRenderPipeline(
        {.label = "world",
         .vertex = {.shader = world.m_shader, .entryPoint = "vertexMain"},
         .fragment = {.shader = world.m_shader, .entryPoint = "fragmentMain"},
         .vertexBuffers = vertexBuffers,
         .colorTargets = targets,
         .depth = rhi::DepthState{.isWriteEnabled = true, .compare = rhi::CompareOp::Greater},
         .bindGroupLayouts = layouts});
    if (!pipeline) {
        return gpuFailure(std::move(pipeline.error()));
    }
    world.m_pipeline = *pipeline;

    for (std::uint32_t slot = 0; slot < rhi::kMaxFramesInFlight; ++slot) {
        auto buffer = device.createBuffer({.label = "world.frame",
                                           .size = sizeof(FrameUniform),
                                           .usage = {rhi::BufferUsage::Uniform},
                                           .location = rhi::MemoryLocation::Upload});
        if (!buffer) {
            return gpuFailure(std::move(buffer.error()));
        }
        const std::array bindings{
            rhi::Binding{.binding = 0, .resource = rhi::BufferBinding{.buffer = *buffer}},
            rhi::Binding{.binding = 1, .resource = *atlasTexture},
            rhi::Binding{.binding = 2, .resource = world.m_lightmapSampler},
            rhi::Binding{.binding = 3, .resource = *lightTableTexture}};
        auto group = device.createBindGroup(
            {.label = "world.frame", .layout = world.m_frameLayout, .bindings = bindings});
        if (!group) {
            device.destroy(*buffer);
            return gpuFailure(std::move(group.error()));
        }
        world.m_frameSlots.push_back(FrameSlot{.buffer = *buffer, .group = *group});
    }

    // The buffers, and a batch for each texture that has triangles.
    std::vector<std::uint32_t> indices;
    for (std::size_t texture = 0; texture < indicesByTexture.size(); ++texture) {
        if (indicesByTexture[texture].empty()) {
            continue;
        }
        const rhi::TextureHandle image =
            texture < textureUses.size() ? textureUses[texture].texture : *checkerTexture;
        const std::array bindings{rhi::Binding{.binding = 0, .resource = image},
                                  rhi::Binding{.binding = 1, .resource = world.m_textureSampler}};
        auto group = device.createBindGroup(
            {.label = "world.texture", .layout = world.m_textureLayout, .bindings = bindings});
        if (!group) {
            return gpuFailure(std::move(group.error()));
        }
        world.m_textureGroups.push_back(*group);
        world.m_batches.push_back(Batch{.firstIndex = narrow<std::uint32_t>(indices.size()),
                                        .indexCount = narrow<std::uint32_t>(indicesByTexture[texture].size()),
                                        .textures = *group});
        indices.insert(indices.end(), indicesByTexture[texture].begin(), indicesByTexture[texture].end());
    }
    world.m_stats.triangles = indices.size() / 3;
    world.m_stats.lightmapAtlas = atlasExtent;

    const auto uploadBuffer =
        [&](std::string_view label, rhi::BufferUsage usage, rhi::BufferState state,
            std::span<const std::byte> bytes) -> Result<rhi::BufferHandle, rhi::RhiError> {
        auto buffer = device.createBuffer({.label = label,
                                           .size = std::max<std::uint64_t>(bytes.size(), 1),
                                           .usage = {usage, rhi::BufferUsage::CopyDestination},
                                           .location = rhi::MemoryLocation::Device});
        if (!buffer) {
            return buffer;
        }
        auto source = stage(device, label, bytes);
        if (!source) {
            device.destroy(*buffer);
            return std::unexpected{std::move(source.error())};
        }
        staging.push_back(*source);
        commands.transition(*buffer, rhi::BufferState::Undefined, rhi::BufferState::CopyDestination);
        commands.copyBuffer(*source, 0, *buffer, 0, bytes.size());
        commands.transition(*buffer, rhi::BufferState::CopyDestination, state);
        return buffer;
    };
    auto vertexBuffer = uploadBuffer("world.vertices", rhi::BufferUsage::Vertex, rhi::BufferState::Vertex,
                                     std::as_bytes(std::span{vertices}));
    if (!vertexBuffer) {
        return gpuFailure(std::move(vertexBuffer.error()));
    }
    world.m_vertices = *vertexBuffer;
    auto indexBuffer = uploadBuffer("world.indices", rhi::BufferUsage::Index, rhi::BufferState::Index,
                                    std::as_bytes(std::span{indices}));
    if (!indexBuffer) {
        return gpuFailure(std::move(indexBuffer.error()));
    }
    world.m_indices = *indexBuffer;

    for (const rhi::BufferHandle buffer : staging) {
        device.destroy(buffer); // kept by the device until this frame's copies are done
    }
    log::info(kLog, "world: {} faces, {} triangles, {} textures, lightmaps {}x{}, {} missing",
              world.m_stats.faces, world.m_stats.triangles, world.m_stats.textures, atlasExtent.width,
              atlasExtent.height, world.m_stats.missingTextures.size());
    return world;
}

BspWorld::BspWorld(BspWorld&& other) noexcept
    : m_device{other.m_device}, m_vertices{std::exchange(other.m_vertices, {})},
      m_indices{std::exchange(other.m_indices, {})}, m_textures{std::move(other.m_textures)},
      m_textureSampler{std::exchange(other.m_textureSampler, {})},
      m_lightmapSampler{std::exchange(other.m_lightmapSampler, {})},
      m_shader{std::exchange(other.m_shader, {})}, m_frameLayout{std::exchange(other.m_frameLayout, {})},
      m_textureLayout{std::exchange(other.m_textureLayout, {})},
      m_pipeline{std::exchange(other.m_pipeline, {})}, m_textureGroups{std::move(other.m_textureGroups)},
      m_frameSlots{std::move(other.m_frameSlots)}, m_batches{std::move(other.m_batches)},
      m_drawCount{other.m_drawCount}, m_lightScale{other.m_lightScale}, m_stats{std::move(other.m_stats)} {
    other.m_textures.clear();
    other.m_textureGroups.clear();
    other.m_frameSlots.clear();
}

BspWorld::~BspWorld() {
    release();
}

void BspWorld::release() noexcept {
    for (const FrameSlot& slot : m_frameSlots) {
        m_device->destroy(slot.group);
        m_device->destroy(slot.buffer);
    }
    for (const rhi::BindGroupHandle group : m_textureGroups) {
        m_device->destroy(group);
    }
    for (const rhi::TextureHandle texture : m_textures) {
        m_device->destroy(texture);
    }
    m_device->destroy(m_pipeline);
    m_device->destroy(m_frameLayout);
    m_device->destroy(m_textureLayout);
    m_device->destroy(m_shader);
    m_device->destroy(m_textureSampler);
    m_device->destroy(m_lightmapSampler);
    m_device->destroy(m_vertices);
    m_device->destroy(m_indices);
    m_frameSlots.clear();
    m_textureGroups.clear();
    m_textures.clear();
}

void BspWorld::draw(rhi::CommandList& commands, const Mat4& viewProjection) {
    // A ring of kMaxFramesInFlight: the slot written now was last read frames in flight ago.
    const FrameSlot& slot = m_frameSlots[m_drawCount++ % m_frameSlots.size()];
    const FrameUniform frame{.viewProjection = viewProjection, .lighting = {.x = m_lightScale}};
    std::ranges::copy(std::as_bytes(std::span{&frame, 1}), m_device->mappedBytes(slot.buffer).begin());

    commands.setPipeline(m_pipeline);
    commands.setVertexBuffer(0, m_vertices, 0);
    commands.setIndexBuffer(m_indices, 0, rhi::IndexFormat::Uint32);
    commands.setBindGroup(0, slot.group);
    for (const Batch& batch : m_batches) {
        commands.setBindGroup(1, batch.textures);
        commands.drawIndexed(batch.indexCount, 1, batch.firstIndex, 0, 0);
    }
}

} // namespace nx::render
