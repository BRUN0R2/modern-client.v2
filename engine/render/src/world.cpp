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

/// The light styles of a face without a lightmap, and after the last of a face's lightmaps.
constexpr std::uint8_t kNoStyle = assets::kNoLightStyle;

struct WorldVertex {
    Vec3 position{};
    float u = 0.0F;
    float v = 0.0F;
    /// In luxels of the lightmaps, their centers at halves; negative without lightmaps.
    float lightmapX = -1.0F;
    float lightmapY = -1.0F;
    std::array<std::uint8_t, assets::kFaceLightStyles> styles{kNoStyle, kNoStyle, kNoStyle, kNoStyle};
};
static_assert(sizeof(WorldVertex) == 8 * sizeof(float), "the vertex layout of the world pipeline");

/// The frame's uniform: the rows of the view-projection matrix; in `lighting`, the original's
/// lightscale and the size of the lightmaps; the camera's position; the light styles' brightness.
struct FrameUniform {
    Mat4 viewProjection{};
    Vec4 lighting{};
    Vec4 eye{};
    std::array<float, kLightStyleCount> styles{}; ///< Four to a float4 in the shader.
};

/// The side of the sky the original draws toward +X, -X, +Y, -Y, +Z and -Z, by its place in
/// kSkySuffixes (r_skyTexOrder): the layers of the sky's texture are in this order.
constexpr std::array<std::size_t, kSkySuffixes.size()> kSkySideOfAxis{0, 2, 1, 3, 4, 5};
/// The sky of a world without one: the original's stand-in, gray.
constexpr std::byte kNoSkyLevel{128};
/// The frames an animated texture shows in a second.
constexpr float kAnimationFramesPerSecond = 10.0F;
/// How far past a brush entity's face the eye must be for it to be drawn: the original's
/// BACKFACE_EPSILON.
constexpr float kBackfaceEpsilon = 0.01F;

/// How a face is drawn, from its texture's name.
enum class SurfaceKind : std::uint8_t {
    Opaque,
    Masked,    ///< `{`: palette index 255 is transparent.
    Sky,       ///< Shows the side of the sky it is seen toward.
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

/// A texture of an animation: '+', its frame ('0' to '9' in the main sequence, 'a' to 'j', of any
/// case, in the alternate one), then the name its frames share.
struct AnimationFrame {
    bool isAlternate = false;
    std::uint32_t number = 0;
    std::string_view sequence{};
};

[[nodiscard]] auto animationFrameOf(std::string_view name) noexcept -> std::optional<AnimationFrame> {
    if (name.size() < 3 || name[0] != '+') {
        return std::nullopt;
    }
    const auto frame = static_cast<char>(std::tolower(static_cast<unsigned char>(name[1])));
    if (frame >= '0' && frame <= '9') {
        return AnimationFrame{.number = static_cast<std::uint32_t>(frame - '0'), .sequence = name.substr(2)};
    }
    if (frame >= 'a' && frame <= 'j') {
        return AnimationFrame{.isAlternate = true,
                              .number = static_cast<std::uint32_t>(frame - 'a'),
                              .sequence = name.substr(2)};
    }
    return std::nullopt;
}

/// The textures `texture` shows in turn: the frames of its sequence, by number, when it is one (the
/// first texture of a number when the map repeats it); itself otherwise. The original refuses a
/// sequence that misses a number; here the frames it has play in order.
[[nodiscard]] auto framesOf(std::span<const std::optional<assets::MipTexture>> textures, std::size_t texture)
    -> std::vector<std::size_t> {
    const std::optional<AnimationFrame> self =
        textures[texture] ? animationFrameOf(textures[texture]->name) : std::nullopt;
    if (!self) {
        return {texture};
    }
    std::map<std::uint32_t, std::size_t> frames;
    for (std::size_t index = 0; index < textures.size(); ++index) {
        const std::optional<AnimationFrame> frame =
            textures[index] ? animationFrameOf(textures[index]->name) : std::nullopt;
        if (frame && frame->isAlternate == self->isAlternate &&
            equalsIgnoringCase(frame->sequence, self->sequence)) {
            frames.try_emplace(frame->number, index);
        }
    }
    return frames | std::views::values | std::ranges::to<std::vector>();
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

/// The extent of a face's lightmaps, in luxels, the texture coordinates of their first luxel, and
/// how many it has: one for each of its styles.
struct LightmapExtent {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t maps = 0;
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

/// The layers of the sky's texture, in the order of kSkySideOfAxis.
struct SkyLayers {
    rhi::Extent2d extent{};
    std::vector<std::byte> texels{};
    std::array<std::size_t, kSkySuffixes.size()> offsets{}; ///< Where each layer starts in texels.
};

/// The layers of `sky`, its sides in the order of kSkySuffixes; without sides, a gray texel each.
/// Errors: BadSky.
[[nodiscard]] auto skyLayersOf(std::span<const assets::RgbaImage> sky) -> Result<SkyLayers, WorldError> {
    SkyLayers layers;
    if (sky.empty()) {
        layers.extent = {.width = 1, .height = 1};
        for (std::size_t& offset : layers.offsets) {
            offset = layers.texels.size();
            layers.texels.insert(layers.texels.end(),
                                 {kNoSkyLevel, kNoSkyLevel, kNoSkyLevel, std::byte{255}});
        }
        return layers;
    }
    const auto isOfTheFirstsSize = [&sky](const assets::RgbaImage& side) {
        return side.width == sky.front().width && side.height == sky.front().height &&
               side.pixels.size() == std::size_t{side.width} * side.height * 4;
    };
    if (sky.size() != kSkySuffixes.size() || sky.front().width == 0 || sky.front().height == 0 ||
        !std::ranges::all_of(sky, isOfTheFirstsSize)) {
        return fail(WorldError::BadSky, std::format("{} sides, the first of {}x{}", sky.size(),
                                                    sky.front().width, sky.front().height));
    }
    layers.extent = {.width = sky.front().width, .height = sky.front().height};
    for (std::size_t axis = 0; axis < kSkySideOfAxis.size(); ++axis) {
        layers.offsets.at(axis) = layers.texels.size();
        const std::vector<std::byte>& pixels = sky[kSkySideOfAxis.at(axis)].pixels;
        layers.texels.insert(layers.texels.end(), pixels.begin(), pixels.end());
    }
    return layers;
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
    auto skyLayers = skyLayersOf(desc.sky);
    if (!skyLayers) {
        return std::unexpected{std::move(skyLayers.error())};
    }
    BspWorld world{device, map};
    const auto gpuFailure = [](Failure<rhi::RhiError> failure) {
        return fail(WorldError::GpuFailed, std::move(failure));
    };
    std::vector<rhi::BufferHandle> staging; // destroyed once recorded; the device keeps them for the copies
    // A texture of `layers` layers of `mipLevels` levels; `offsets` has where each level of each layer
    // starts in `texels`, layer after layer.
    const auto uploadTexture =
        [&](std::string_view label, rhi::Extent2d extent, std::uint32_t mipLevels, std::uint32_t layers,
            std::span<const std::byte> texels,
            std::span<const std::size_t> offsets) -> Result<rhi::TextureHandle, rhi::RhiError> {
        auto texture =
            device.createTexture({.label = label,
                                  .format = rhi::Format::Rgba8Unorm,
                                  .extent = extent,
                                  .mipLevels = mipLevels,
                                  .arrayLayers = layers,
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
        for (std::uint32_t layer = 0; layer < layers; ++layer) {
            for (std::uint32_t level = 0; level < mipLevels; ++level) {
                commands.copyBufferToTexture(
                    {.buffer = *source,
                     .bufferOffset = offsets[(std::size_t{layer} * mipLevels) + level],
                     .texture = *texture,
                     .mipLevel = level,
                     .arrayLayer = layer,
                     .extent = {.width = std::max(extent.width >> level, 1U),
                                .height = std::max(extent.height >> level, 1U)}});
            }
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
    auto checkerTexture = uploadTexture("world.missing", {.width = kCheckerSize, .height = kCheckerSize}, 1,
                                        1, checker, oneLevel);
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
        auto texture = uploadTexture(
            std::format("world.texture.{}", slot->name), {.width = pixels->width, .height = pixels->height},
            narrow<std::uint32_t>(assets::kMipLevels), 1, texels.bytes, texels.offsets);
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
        std::uint32_t index = 0; ///< In map.faces().
        Vec3 origin{};
        std::size_t texture = 0;
        bool isSky = false;
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
            if (kind == SurfaceKind::Invisible) {
                continue;
            }
            faces.push_back(FaceToDraw{.face = &face,
                                       .index = bspModel.firstFace.value + offset,
                                       .origin = origins[model],
                                       .texture = texture,
                                       .isSky = kind == SurfaceKind::Sky});
        }
    }

    // The lightmaps: each lit face's extent in luxels, computed in double precision as the
    // compilers compute it, packed into the atlas, its lightmaps at the same place of each layer.
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
    std::vector<std::array<std::uint32_t, 2>> corners(faces.size());
    for (std::size_t index = 0; index < faces.size(); ++index) {
        FaceToDraw& entry = faces[index];
        const assets::TextureInfo& info = map.textureInfos()[entry.face->textureInfo.value];
        if (entry.isSky || info.isSpecial || !entry.face->lightmapOffset) {
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
        // A lightmap for each style, until the first unused one, as the original counts them.
        const auto maps = static_cast<std::uint32_t>(std::ranges::distance(
            entry.face->lightStyles |
            std::views::take_while([](std::uint8_t style) { return style != kNoStyle; })));
        const LightmapExtent extent{.width = *width,
                                    .height = *height,
                                    .maps = maps,
                                    .minS = firstS * kLuxelUnits,
                                    .minT = firstT * kLuxelUnits};
        const std::size_t sampleBytes = std::size_t{extent.width} * extent.height * 3;
        if (*entry.face->lightmapOffset + (sampleBytes * maps) > map.lighting().size()) {
            continue; // samples the file does not hold: drawn unlit
        }
        entry.lightmap = extent;
        corners[index] = packer.place(extent.width, extent.height);
    }
    const rhi::Extent2d atlasExtent{.width = kAtlasWidth, .height = std::max(packer.height(), 1U)};
    const std::size_t layerBytes = std::size_t{atlasExtent.width} * atlasExtent.height * 4;
    std::vector<std::byte> atlas(layerBytes * assets::kFaceLightStyles, std::byte{0});
    for (std::size_t index = 0; index < faces.size(); ++index) {
        const FaceToDraw& entry = faces[index];
        if (!entry.lightmap) {
            continue;
        }
        // The lightmaps of a face follow one another in the file, a layer each here.
        const std::span<const std::byte> samples = map.lighting().subspan(*entry.face->lightmapOffset);
        const std::size_t mapSamples = std::size_t{entry.lightmap->width} * entry.lightmap->height;
        for (std::size_t lightmap = 0; lightmap < entry.lightmap->maps; ++lightmap) {
            for (std::uint32_t y = 0; y < entry.lightmap->height; ++y) {
                for (std::uint32_t x = 0; x < entry.lightmap->width; ++x) {
                    const std::size_t from =
                        ((lightmap * mapSamples) + (std::size_t{y} * entry.lightmap->width) + x) * 3;
                    const std::size_t to =
                        (lightmap * layerBytes) +
                        (((std::size_t{corners[index][1] + y} * atlasExtent.width) + corners[index][0] + x) *
                         4);
                    atlas[to] = samples[from];
                    atlas[to + 1] = samples[from + 1];
                    atlas[to + 2] = samples[from + 2];
                    atlas[to + 3] = std::byte{255};
                }
            }
        }
    }
    const std::array<std::size_t, assets::kFaceLightStyles> layerOffsets{0, layerBytes, layerBytes * 2,
                                                                         layerBytes * 3};
    auto atlasTexture = uploadTexture("world.lightmaps", atlasExtent, 1,
                                      narrow<std::uint32_t>(assets::kFaceLightStyles), atlas, layerOffsets);
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
                      1, lightTexels, oneLevel);
    if (!lightTableTexture) {
        return gpuFailure(std::move(lightTableTexture.error()));
    }
    world.m_lighting = {.x = static_cast<float>(lightScale(desc.gamma)),
                        .y = static_cast<float>(atlasExtent.width),
                        .z = static_cast<float>(atlasExtent.height)};

    // The sky: its sides, the layers of one texture, in the order the shader picks them.
    auto skyTexture =
        uploadTexture("world.sky", skyLayers->extent, 1, narrow<std::uint32_t>(kSkySuffixes.size()),
                      skyLayers->texels, skyLayers->offsets);
    if (!skyTexture) {
        return gpuFailure(std::move(skyTexture.error()));
    }

    // The geometry: each face a triangle fan, whose triangles each frame puts in its texture's bucket
    // (the checkerboard's last), or the sky's, when the camera sees it.
    std::vector<WorldVertex> vertices;
    std::vector<std::optional<std::uint32_t>> bucketOfTexture(textureUses.size() + 1);
    std::vector<std::vector<rhi::TextureHandle>> bucketFrames; // the textures each bucket shows in turn
    std::size_t mostIndices = 0;
    world.m_faces.resize(map.faces().size());
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
                               .v = static_cast<float>(t / double{use.height})};
            if (entry.lightmap) {
                // Luxel centers: the first luxel's s is minS, 16 texels apart.
                vertex.lightmapX =
                    static_cast<float>(((s - entry.lightmap->minS) / kLuxelUnits) + 0.5 + corners[index][0]);
                vertex.lightmapY =
                    static_cast<float>(((t - entry.lightmap->minT) / kLuxelUnits) + 0.5 + corners[index][1]);
                vertex.styles = entry.face->lightStyles;
            }
            vertices.push_back(vertex);
        }
        std::uint32_t bucket = kSkyBucket;
        if (!entry.isSky) {
            std::optional<std::uint32_t>& slot = bucketOfTexture[std::min(entry.texture, textureUses.size())];
            if (!slot) {
                slot = narrow<std::uint32_t>(bucketFrames.size());
                bucketFrames.push_back(entry.texture < textureUses.size()
                                           ? framesOf(map.textures(), entry.texture) |
                                                 std::views::transform([&textureUses](std::size_t frame) {
                                                     return textureUses[frame].texture;
                                                 }) |
                                                 std::ranges::to<std::vector>()
                                           : std::vector{*checkerTexture});
            }
            bucket = *slot;
        }
        world.m_faces[entry.index] = FaceDraw{.firstVertex = first,
                                              .vertexCount = entry.face->edgeCount,
                                              .bucket = bucket,
                                              .plane = map.planes()[entry.face->plane.value].plane,
                                              .isBackSide = entry.face->isBackSide};
        mostIndices += (std::size_t{entry.face->edgeCount} - 2) * 3;
        ++world.m_stats.faces;
    }
    for (std::size_t model = 1; model < map.models().size(); ++model) {
        const assets::BspModel& bspModel = map.models()[model];
        world.m_models.push_back(ModelDraw{.firstFace = bspModel.firstFace.value,
                                           .faceCount = bspModel.faceCount,
                                           .bounds = bspModel.bounds.translated(origins[model]),
                                           .origin = origins[model]});
    }
    world.m_faceGathered.resize(world.m_faces.size());
    world.m_bucketIndices.resize(bucketFrames.size());

    // The pipelines and their bind groups: per frame, the camera, the lightmaps, the light table and
    // the sky; per batch, its texture.
    auto shader = device.createShader({.label = "world", .code = desc.shaderCode});
    if (!shader) {
        return gpuFailure(std::move(shader.error()));
    }
    world.m_shader = *shader;
    const std::array frameBindings{
        rhi::BindingLayout{
            .binding = 0,
            .type = rhi::BindingType::UniformBuffer,
            .stages = {rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment}}, // the matrix; the lighting
        rhi::BindingLayout{
            .binding = 1, .type = rhi::BindingType::SampledTexture, .stages = {rhi::ShaderStage::Fragment}},
        rhi::BindingLayout{
            .binding = 2, .type = rhi::BindingType::SampledTexture, .stages = {rhi::ShaderStage::Fragment}},
        rhi::BindingLayout{
            .binding = 3, .type = rhi::BindingType::SampledTexture, .stages = {rhi::ShaderStage::Fragment}},
        rhi::BindingLayout{
            .binding = 4, .type = rhi::BindingType::Sampler, .stages = {rhi::ShaderStage::Fragment}}};
    const std::array textureBindings{
        rhi::BindingLayout{
            .binding = 0, .type = rhi::BindingType::SampledTexture, .stages = {rhi::ShaderStage::Fragment}},
        rhi::BindingLayout{
            .binding = 1, .type = rhi::BindingType::Sampler, .stages = {rhi::ShaderStage::Fragment}}};
    auto frameLayout = device.createBindGroupLayout({.label = "world.frame", .bindings = frameBindings});
    auto textureLayout =
        device.createBindGroupLayout({.label = "world.texture", .bindings = textureBindings});
    auto textureSampler = device.createSampler({.label = "world.texture"});
    // Each side clamped at its edges, as the original draws them.
    auto skySampler = device.createSampler(
        {.label = "world.sky", .mipmap = rhi::Filter::Nearest, .address = rhi::AddressMode::ClampToEdge});
    if (!frameLayout || !textureLayout || !textureSampler || !skySampler) {
        return gpuFailure(!frameLayout      ? std::move(frameLayout.error())
                          : !textureLayout  ? std::move(textureLayout.error())
                          : !textureSampler ? std::move(textureSampler.error())
                                            : std::move(skySampler.error()));
    }
    world.m_frameLayout = *frameLayout;
    world.m_textureLayout = *textureLayout;
    world.m_textureSampler = *textureSampler;
    world.m_skySampler = *skySampler;

    const std::array attributes{
        rhi::VertexAttribute{.location = 0, .format = rhi::VertexFormat::Float3, .offset = 0},
        rhi::VertexAttribute{
            .location = 1, .format = rhi::VertexFormat::Float2, .offset = offsetof(WorldVertex, u)}, // NOLINT
        rhi::VertexAttribute{.location = 2,
                             .format = rhi::VertexFormat::Float2,
                             .offset = offsetof(WorldVertex, lightmapX)}, // NOLINT
        rhi::VertexAttribute{.location = 3,
                             .format = rhi::VertexFormat::Uint8x4,
                             .offset = offsetof(WorldVertex, styles)}}; // NOLINT
    const std::array vertexBuffers{
        rhi::VertexBufferLayout{.stride = sizeof(WorldVertex), .attributes = attributes}};
    const std::array targets{rhi::ColorTarget{.format = desc.colorFormat}};
    const rhi::DepthState depth{.isWriteEnabled = true, .compare = rhi::CompareOp::Greater};
    const std::array layouts{world.m_frameLayout, world.m_textureLayout};
    auto pipeline =
        device.createRenderPipeline({.label = "world",
                                     .vertex = {.shader = world.m_shader, .entryPoint = "vertexMain"},
                                     .fragment = {.shader = world.m_shader, .entryPoint = "fragmentMain"},
                                     .vertexBuffers = vertexBuffers,
                                     .colorTargets = targets,
                                     .depth = depth,
                                     .bindGroupLayouts = layouts});
    const std::array skyLayouts{world.m_frameLayout};
    auto skyPipeline =
        device.createRenderPipeline({.label = "world.sky",
                                     .vertex = {.shader = world.m_shader, .entryPoint = "skyVertexMain"},
                                     .fragment = {.shader = world.m_shader, .entryPoint = "skyFragmentMain"},
                                     .vertexBuffers = vertexBuffers,
                                     .colorTargets = targets,
                                     .depth = depth,
                                     .bindGroupLayouts = skyLayouts});
    if (!pipeline || !skyPipeline) {
        return gpuFailure(!pipeline ? std::move(pipeline.error()) : std::move(skyPipeline.error()));
    }
    world.m_pipeline = *pipeline;
    world.m_skyPipeline = *skyPipeline;

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
            rhi::Binding{.binding = 2, .resource = *lightTableTexture},
            rhi::Binding{.binding = 3, .resource = *skyTexture},
            rhi::Binding{.binding = 4, .resource = world.m_skySampler}};
        auto group = device.createBindGroup(
            {.label = "world.frame", .layout = world.m_frameLayout, .bindings = bindings});
        if (!group) {
            device.destroy(*buffer);
            return gpuFailure(std::move(group.error()));
        }
        // As many indices as every face has: what a frame gathers is never more.
        auto indices =
            device.createBuffer({.label = "world.indices",
                                 .size = std::max<std::uint64_t>(mostIndices, 1) * sizeof(std::uint32_t),
                                 .usage = {rhi::BufferUsage::Index},
                                 .location = rhi::MemoryLocation::Upload});
        if (!indices) {
            device.destroy(*group);
            device.destroy(*buffer);
            return gpuFailure(std::move(indices.error()));
        }
        world.m_frameSlots.push_back(FrameSlot{.buffer = *buffer, .group = *group, .indices = *indices});
    }

    // A bind group for each texture each bucket shows.
    for (const std::vector<rhi::TextureHandle>& frames : bucketFrames) {
        std::vector<rhi::BindGroupHandle>& groups = world.m_bucketFrames.emplace_back();
        for (const rhi::TextureHandle texture : frames) {
            const std::array bindings{rhi::Binding{.binding = 0, .resource = texture},
                                      rhi::Binding{.binding = 1, .resource = world.m_textureSampler}};
            auto group = device.createBindGroup(
                {.label = "world.texture", .layout = world.m_textureLayout, .bindings = bindings});
            if (!group) {
                return gpuFailure(std::move(group.error()));
            }
            groups.push_back(*group);
        }
    }
    world.m_stats.triangles = mostIndices / 3;
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

    for (const rhi::BufferHandle buffer : staging) {
        device.destroy(buffer); // kept by the device until this frame's copies are done
    }
    log::info(kLog, "world: {} faces, {} triangles, {} textures, lightmaps {}x{}, {} missing",
              world.m_stats.faces, world.m_stats.triangles, world.m_stats.textures, atlasExtent.width,
              atlasExtent.height, world.m_stats.missingTextures.size());
    return world;
}

BspWorld::BspWorld(BspWorld&& other) noexcept
    : m_device{other.m_device}, m_map{other.m_map}, m_vertices{std::exchange(other.m_vertices, {})},
      m_textures{std::move(other.m_textures)}, m_textureSampler{std::exchange(other.m_textureSampler, {})},
      m_skySampler{std::exchange(other.m_skySampler, {})}, m_shader{std::exchange(other.m_shader, {})},
      m_frameLayout{std::exchange(other.m_frameLayout, {})},
      m_textureLayout{std::exchange(other.m_textureLayout, {})},
      m_pipeline{std::exchange(other.m_pipeline, {})}, m_skyPipeline{std::exchange(other.m_skyPipeline, {})},
      m_bucketFrames{std::move(other.m_bucketFrames)}, m_frameSlots{std::move(other.m_frameSlots)},
      m_faces{std::move(other.m_faces)}, m_models{std::move(other.m_models)}, m_lighting{other.m_lighting},
      m_stats{std::move(other.m_stats)}, m_drawCount{other.m_drawCount},
      m_faceGathered{std::move(other.m_faceGathered)}, m_visibleFrom{other.m_visibleFrom},
      m_visibleLeaves{std::move(other.m_visibleLeaves)}, m_bucketIndices{std::move(other.m_bucketIndices)},
      m_skyIndices{std::move(other.m_skyIndices)}, m_batches{std::move(other.m_batches)},
      m_lastFrame{other.m_lastFrame} {
    other.m_textures.clear();
    other.m_bucketFrames.clear();
    other.m_frameSlots.clear();
}

BspWorld::~BspWorld() {
    release();
}

void BspWorld::release() noexcept {
    for (const FrameSlot& slot : m_frameSlots) {
        m_device->destroy(slot.group);
        m_device->destroy(slot.buffer);
        m_device->destroy(slot.indices);
    }
    for (const rhi::BindGroupHandle group : m_bucketFrames | std::views::join) {
        m_device->destroy(group);
    }
    for (const rhi::TextureHandle texture : m_textures) {
        m_device->destroy(texture);
    }
    m_device->destroy(m_pipeline);
    m_device->destroy(m_skyPipeline);
    m_device->destroy(m_frameLayout);
    m_device->destroy(m_textureLayout);
    m_device->destroy(m_shader);
    m_device->destroy(m_textureSampler);
    m_device->destroy(m_skySampler);
    m_device->destroy(m_vertices);
    m_frameSlots.clear();
    m_bucketFrames.clear();
    m_textures.clear();
}

void BspWorld::addFace(std::uint32_t face, Vec3 eye, bool isOfAnEntity) {
    if (m_faceGathered[face] == m_drawCount) {
        return; // in another leaf already
    }
    m_faceGathered[face] = m_drawCount;
    const FaceDraw& draw = m_faces[face];
    const float distance = draw.plane.signedDistance(eye);
    const bool isFacing = isOfAnEntity
                              ? (draw.isBackSide ? distance < -kBackfaceEpsilon : distance > kBackfaceEpsilon)
                              : (distance < 0.0F) == draw.isBackSide;
    if (draw.vertexCount == 0 || !isFacing) {
        return;
    }
    std::vector<std::uint32_t>& indices =
        draw.bucket == kSkyBucket ? m_skyIndices : m_bucketIndices[draw.bucket];
    for (std::uint32_t corner = 2; corner < draw.vertexCount; ++corner) {
        const std::uint32_t first = draw.firstVertex;
        indices.insert(indices.end(), {first, first + corner - 1, first + corner});
    }
    ++m_lastFrame.faces;
}

void BspWorld::gather(const Camera& camera, float aspect) {
    m_lastFrame = {};
    for (std::vector<std::uint32_t>& indices : m_bucketIndices) {
        indices.clear();
    }
    m_skyIndices.clear();

    // The leaves the camera's leaf may see, found again when it moves to another leaf.
    const assets::LeafIndex leaf = m_map->leafAt(camera.position);
    if (leaf != m_visibleFrom) {
        const assets::LeafSet visible = m_map->potentiallyVisible(leaf);
        m_visibleLeaves.clear();
        for (std::uint32_t index = 1; index < m_map->leaves().size(); ++index) {
            if (visible.contains(assets::LeafIndex{index})) {
                m_visibleLeaves.push_back(assets::LeafIndex{index});
            }
        }
        m_visibleFrom = leaf;
    }

    const std::array<Plane, 4> frustum = frustumPlanes(camera, aspect);
    const auto isInView = [&frustum](const Aabb& box) {
        return std::ranges::none_of(
            frustum, [&box](const Plane& plane) { return plane.sideOf(box) == PlaneSide::Back; });
    };
    for (const assets::LeafIndex visible : m_visibleLeaves) {
        const assets::BspLeaf& record = m_map->leaves()[visible.value];
        if (!isInView(record.bounds)) {
            continue;
        }
        ++m_lastFrame.leaves;
        for (std::uint32_t mark = 0; mark < record.markSurfaceCount; ++mark) {
            addFace(m_map->markSurfaces()[record.firstMarkSurface.value + mark].value, camera.position,
                    false);
        }
    }
    for (const ModelDraw& model : m_models) {
        if (!isInView(model.bounds)) {
            continue;
        }
        for (std::uint32_t face = model.firstFace; face < model.firstFace + model.faceCount; ++face) {
            addFace(face, camera.position - model.origin, true);
        }
    }
}

void BspWorld::draw(rhi::CommandList& commands, const WorldFrame& view) {
    // A ring of kMaxFramesInFlight: the slot written now was last read frames in flight ago.
    const FrameSlot& slot = m_frameSlots[m_drawCount++ % m_frameSlots.size()];
    FrameUniform frame{.viewProjection = viewProjectionMatrix(view.camera, view.aspect),
                       .lighting = m_lighting,
                       .eye = extend(view.camera.position, 1.0F)};
    std::ranges::transform(view.lightStyles, frame.styles.begin(),
                           [](std::uint16_t value) { return static_cast<float>(value); });
    std::ranges::copy(std::as_bytes(std::span{&frame, 1}), m_device->mappedBytes(slot.buffer).begin());

    // The indices of what the camera sees, a batch for each texture that has some, then the sky's.
    // An animated texture shows its frames ten a second, as the original's R_TextureAnimation.
    gather(view.camera, view.aspect);
    const auto step = static_cast<std::size_t>(std::max(view.time.count(), 0.0F) * kAnimationFramesPerSecond);
    const std::span<std::byte> indexBytes = m_device->mappedBytes(slot.indices);
    std::uint32_t written = 0;
    const auto write = [&indexBytes, &written](std::span<const std::uint32_t> indices) {
        std::ranges::copy(std::as_bytes(indices),
                          indexBytes.subspan(std::size_t{written} * sizeof(std::uint32_t)).begin());
        const IndexRange range{.firstIndex = written, .indexCount = narrow<std::uint32_t>(indices.size())};
        written += range.indexCount;
        return range;
    };
    m_batches.clear();
    for (std::size_t bucket = 0; bucket < m_bucketIndices.size(); ++bucket) {
        if (!m_bucketIndices[bucket].empty()) {
            const std::vector<rhi::BindGroupHandle>& frames = m_bucketFrames[bucket];
            m_batches.push_back(
                Batch{.indices = write(m_bucketIndices[bucket]), .textures = frames[step % frames.size()]});
        }
    }
    const IndexRange sky = write(m_skyIndices);
    m_lastFrame.triangles = written / 3;

    commands.setPipeline(m_pipeline);
    commands.setVertexBuffer(0, m_vertices, 0);
    commands.setIndexBuffer(slot.indices, 0, rhi::IndexFormat::Uint32);
    commands.setBindGroup(0, slot.group);
    for (const Batch& batch : m_batches) {
        commands.setBindGroup(1, batch.textures);
        commands.drawIndexed(batch.indices.indexCount, 1, batch.indices.firstIndex, 0, 0);
    }
    if (sky.indexCount != 0) {
        // Last, so that what the world hides of them fails the depth test before it is shaded.
        commands.setPipeline(m_skyPipeline);
        commands.setBindGroup(0, slot.group);
        commands.drawIndexed(sky.indexCount, 1, sky.firstIndex, 0, 0);
    }
}

} // namespace nx::render
