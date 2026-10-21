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

/// A luxel's center, from its corner, in luxels.
constexpr double kLuxelCenter = 0.5;
/// The width of the lightmap atlas; its height grows with the map.
constexpr std::uint32_t kAtlasWidth = 1024;
/// The checkerboard of a texture no WAD has: its size and its squares.
constexpr std::uint32_t kCheckerSize = 16;
constexpr std::uint32_t kCheckerSquare = 8;
/// The palette index a masked (`{`) texture draws as transparent.
constexpr std::uint8_t kTransparentIndex = 255;

/// The bytes of a texel the world uploads, red, green, blue and alpha; of a lightmap sample and of a
/// palette color, red, green and blue.
constexpr std::size_t kRgbaBytes = 4;
constexpr std::size_t kRgbBytes = 3;
/// A channel at its brightest, and an opaque alpha.
constexpr std::byte kFull{255};
/// The corners of a triangle.
constexpr std::uint32_t kTriangleCorners = 3;

/// The light styles of a face without a lightmap, and after the last of a face's lightmaps.
constexpr std::uint8_t kNoStyle = assets::kNoLightStyle;

/// A vertex of a turbulent face (water, lasers): its texture coordinates are in texels and warp.
constexpr std::uint32_t kTurbulent = 1;

struct WorldVertex {
    Vec3 position{};
    /// From 0 to 1 across the texture; in texels on a turbulent face, which warps them.
    float u = 0.0F;
    float v = 0.0F;
    /// In luxels of the lightmaps, their centers at halves; negative without lightmaps.
    float lightmapX = -1.0F;
    float lightmapY = -1.0F;
    std::array<std::uint8_t, assets::kFaceLightStyles> styles{kNoStyle, kNoStyle, kNoStyle, kNoStyle};
    std::uint32_t flags = 0; ///< kTurbulent, or 0.
    /// On a turbulent face, the height of the corner the original tells which way its waves go by:
    /// the first of the last piece it cuts the face into, the first of its list of them.
    float waveBase = 0.0F;
};
/// The 4-byte words of a vertex: the position's three, the texture's two, the lightmaps' two, the
/// styles' one, the flags' and the wave's base.
constexpr std::size_t kWorldVertexWords = 10;
static_assert(sizeof(WorldVertex) == kWorldVertexWords * sizeof(float),
              "the vertex layout of the world pipeline");

// The bind groups of the world's pipelines, their bindings and the vertex attributes, as
// shaders/world.slang declares them.
constexpr std::uint32_t kFrameGroup = 0;
constexpr std::uint32_t kTextureGroup = 1;
constexpr std::uint32_t kEntityGroup = 2;
constexpr std::uint32_t kFrameUniformBinding = 0;
constexpr std::uint32_t kLightmapsBinding = 1;
constexpr std::uint32_t kLightTableBinding = 2;
constexpr std::uint32_t kSkySidesBinding = 3;
constexpr std::uint32_t kSkySamplerBinding = 4;
constexpr std::uint32_t kAlbedoBinding = 0;
constexpr std::uint32_t kAlbedoSamplerBinding = 1;
constexpr std::uint32_t kEntityUniformBinding = 0;

constexpr std::array kWorldVertexAttributes{
    rhi::VertexAttribute{.location = 0, .format = rhi::VertexFormat::Float3, .offset = 0},
    rhi::VertexAttribute{
        .location = 1, .format = rhi::VertexFormat::Float2, .offset = offsetof(WorldVertex, u)}, // NOLINT
    rhi::VertexAttribute{.location = 2,
                         .format = rhi::VertexFormat::Float2,
                         .offset = offsetof(WorldVertex, lightmapX)}, // NOLINT
    rhi::VertexAttribute{.location = 3,
                         .format = rhi::VertexFormat::Uint8x4,
                         .offset = offsetof(WorldVertex, styles)}, // NOLINT
    rhi::VertexAttribute{
        .location = 4, .format = rhi::VertexFormat::Uint32, .offset = offsetof(WorldVertex, flags)}, // NOLINT
    rhi::VertexAttribute{.location = 5,
                         .format = rhi::VertexFormat::Float,
                         .offset = offsetof(WorldVertex, waveBase)}}; // NOLINT

/// A brush model's uniform: its rendercolor, and its renderamt in `color.w`, from 0 to 1; where it is
/// drawn this frame, and in `origin.w` the height of its waves. The world's is 0.
struct EntityUniform {
    Vec4 color{};
    Vec4 origin{};
    std::array<Vec4, Mat3::kRowCount> turn{Vec4{.x = 1.0F}, Vec4{.y = 1.0F}, Vec4{.z = 1.0F}}; ///< Its rows.
};

/// Each model's uniform is this far from the last in a frame's buffer of them: the largest offset
/// alignment of uniform buffers a GPU asks for.
constexpr std::uint64_t kEntityUniformStride = 256;
static_assert(sizeof(EntityUniform) <= kEntityUniformStride);

/// The frame's uniform: the rows of the view-projection matrix; in `lighting`, the original's
/// lightscale, the size of the lightmaps and the time; the camera's position; the light styles'
/// brightness.
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

/// A texture of an animation: '+', its frame ('0' to '9' in the main sequence, 'a' to 'j', of any
/// case, in the alternate one), then the name its frames share.
struct AnimationFrame {
    bool isAlternate = false;
    std::uint32_t number = 0;
    std::string_view sequence{};
};

/// Where the name an animation's frames share starts, after the '+' and the frame.
constexpr std::size_t kSequenceStart = 2;

[[nodiscard]] auto animationFrameOf(std::string_view name) noexcept -> std::optional<AnimationFrame> {
    if (name.size() <= kSequenceStart || !name.starts_with('+')) {
        return std::nullopt;
    }
    const auto frame = static_cast<char>(std::tolower(static_cast<unsigned char>(name[1])));
    if (frame >= '0' && frame <= '9') {
        return AnimationFrame{.number = static_cast<std::uint32_t>(frame - '0'),
                              .sequence = name.substr(kSequenceStart)};
    }
    if (frame >= 'a' && frame <= 'j') {
        return AnimationFrame{.isAlternate = true,
                              .number = static_cast<std::uint32_t>(frame - 'a'),
                              .sequence = name.substr(kSequenceStart)};
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

/// The textures the faces of `texture` show instead when their brush entity's frame is not 0: the
/// frames of its sequence's alternate (+a... for +0..., and back), as framesOf gives them; none when it
/// is not an animation or has no alternate (R_TextureAnimation's alternate_anims).
[[nodiscard]] auto alternateFramesOf(std::span<const std::optional<assets::MipTexture>> textures,
                                     std::size_t texture) -> std::vector<std::size_t> {
    const std::optional<AnimationFrame> self =
        textures[texture] ? animationFrameOf(textures[texture]->name) : std::nullopt;
    if (!self) {
        return {};
    }
    for (std::size_t index = 0; index < textures.size(); ++index) {
        const std::optional<AnimationFrame> frame =
            textures[index] ? animationFrameOf(textures[index]->name) : std::nullopt;
        if (frame && frame->isAlternate != self->isAlternate &&
            equalsIgnoringCase(frame->sequence, self->sequence)) {
            return framesOf(textures, index);
        }
    }
    return {};
}

/// The RGBA texels of the mip levels of `texture`, level after level.
struct TexelLevels {
    std::vector<std::byte> bytes;
    std::array<std::size_t, assets::kMipLevels> offsets{};
};

[[nodiscard]] auto texelsOf(const assets::MipTexture& texture, bool isMasked,
                            const ChannelTable& textureGamma) -> TexelLevels {
    TexelLevels result;
    for (std::size_t level = 0; level < assets::kMipLevels; ++level) {
        result.offsets.at(level) = result.bytes.size();
        for (const std::byte index : texture.levels.at(level)) {
            const auto entry = std::to_integer<std::size_t>(index);
            const bool isTransparent = isMasked && entry == kTransparentIndex;
            for (std::size_t channel = 0; channel < kRgbBytes; ++channel) {
                const auto value =
                    std::to_integer<std::size_t>(texture.palette[(entry * kRgbBytes) + channel]);
                result.bytes.push_back(isTransparent ? std::byte{0} : std::byte{textureGamma[value]});
            }
            result.bytes.push_back(isTransparent ? std::byte{0} : kFull);
        }
    }
    return result;
}

/// A magenta and black checkerboard: the texture of a name no WAD has.
[[nodiscard]] auto checkerTexels() -> std::vector<std::byte> {
    std::vector<std::byte> texels;
    for (std::uint32_t y = 0; y < kCheckerSize; ++y) {
        for (std::uint32_t x = 0; x < kCheckerSize; ++x) {
            const bool isMagenta = (((x / kCheckerSquare) + (y / kCheckerSquare)) & 1U) == 0;
            const std::byte level = isMagenta ? kFull : std::byte{0};
            texels.insert(texels.end(), {level, std::byte{0}, level, kFull});
        }
    }
    return texels;
}

/// A texture of the map: how its faces are drawn, its size, and what draws it.
struct TextureUse {
    rhi::TextureHandle texture{};
    SurfaceKind kind = SurfaceKind::Opaque;
    float width = 1.0F;
    float height = 1.0F;
};

/// The map's textures on the GPU, and the use of each of the map's, by its index.
struct MapTextures {
    std::vector<rhi::OwnedTexture> owned; ///< The checkerboard first.
    std::vector<TextureUse> uses;
    std::size_t loaded = 0;           ///< Textures of the map drawn with their own pixels.
    std::vector<std::string> missing; ///< The names no WAD has, drawn as the checkerboard.
};

/// The pixels of `texture`: its own, else those of the first WAD of `wads` that has its name; none
/// when no WAD has it, or the first that has it cannot be read, which is logged.
[[nodiscard]] auto pixelsOf(const assets::MipTexture& texture, std::span<const assets::WadView> wads)
    -> std::optional<assets::MipTexture> {
    if (texture.hasPixels()) {
        return texture;
    }
    for (const assets::WadView& wad : wads) {
        if (const assets::WadEntry* entry = wad.find(texture.name)) {
            auto read = assets::readTexture(*entry);
            if (!read) {
                log::warning(kLog, "the texture {:?} of a WAD cannot be read: {}", texture.name,
                             describe(read.error()));
                return std::nullopt;
            }
            return std::move(*read);
        }
    }
    return std::nullopt;
}

/// The textures of the map, each its own or the first WAD's that has its name, through `texgamma`; a
/// checkerboard for the others. Errors: those of the uploads.
[[nodiscard]] auto loadTextures(Uploader& uploader, const BspWorldDesc& desc)
    -> Result<MapTextures, rhi::RhiError> {
    const ChannelTable textureGamma = textureGammaTable(desc.gamma);
    auto checker = uploader.texture("world.missing", {.width = kCheckerSize, .height = kCheckerSize}, 1, 1,
                                    checkerTexels(), kOneLevel);
    if (!checker) {
        return std::unexpected{std::move(checker.error())};
    }
    MapTextures textures;
    const TextureUse missing{.texture = checker->get(),
                             .width = static_cast<float>(kCheckerSize),
                             .height = static_cast<float>(kCheckerSize)};
    textures.owned.push_back(std::move(*checker));
    textures.uses.assign(desc.map.textures().size(), missing);
    for (std::size_t index = 0; index < desc.map.textures().size(); ++index) {
        const std::optional<assets::MipTexture>& slot = desc.map.textures()[index];
        if (!slot) {
            continue;
        }
        TextureUse& use = textures.uses[index];
        use.kind = surfaceKindOf(slot->name);
        use.width = static_cast<float>(slot->width);
        use.height = static_cast<float>(slot->height);
        if (use.kind == SurfaceKind::Sky || use.kind == SurfaceKind::Invisible) {
            continue;
        }
        const std::optional<assets::MipTexture> pixels = pixelsOf(*slot, desc.wads);
        if (!pixels) {
            textures.missing.push_back(slot->name);
            continue;
        }
        const TexelLevels texels = texelsOf(*pixels, use.kind == SurfaceKind::Masked, textureGamma);
        auto texture = uploader.texture(
            std::format("world.texture.{}", slot->name), {.width = pixels->width, .height = pixels->height},
            narrow<std::uint32_t>(assets::kMipLevels), 1, texels.bytes, texels.offsets);
        if (!texture) {
            return std::unexpected{std::move(texture.error())};
        }
        use.texture = texture->get();
        textures.owned.push_back(std::move(*texture));
        ++textures.loaded;
    }
    return textures;
}

/// Where a face's lightmaps lie on it, and how many it has: one for each of its styles.
struct LightmapExtent {
    SurfaceExtent area{};
    std::uint32_t maps = 0;
};

/// A face the world draws: the world's, or a brush entity's in its model's space.
struct FaceToDraw {
    const assets::BspFace* face = nullptr;
    std::uint32_t index = 0; ///< In map.faces().
    std::size_t texture = 0;
    bool isSky = false;
    bool isTurbulent = false;
    std::optional<LightmapExtent> lightmap{};
    std::array<std::uint32_t, 2> corner{}; ///< Of its lightmaps in the atlas, x then y.
};

/// The faces of every model of `map` but those of invisible textures, by the kind `uses` gives them.
[[nodiscard]] auto facesToDraw(const assets::BspView& map, std::span<const TextureUse> uses)
    -> std::vector<FaceToDraw> {
    std::vector<FaceToDraw> faces;
    for (const assets::BspModel& model : map.models()) {
        for (std::uint32_t offset = 0; offset < model.faceCount; ++offset) {
            const assets::BspFace& face = map.faces()[model.firstFace.value + offset];
            const std::size_t texture = map.textureInfos()[face.textureInfo.value].texture.value;
            const SurfaceKind kind = uses[texture].kind;
            if (kind != SurfaceKind::Invisible) {
                faces.push_back(FaceToDraw{.face = &face,
                                           .index = model.firstFace.value + offset,
                                           .texture = texture,
                                           .isSky = kind == SurfaceKind::Sky,
                                           .isTurbulent = kind == SurfaceKind::Turbulent});
            }
        }
    }
    return faces;
}

/// The lightmaps of `entry`, as the compilers measure them; none for a face drawn unlit: the sky,
/// turbulent and special faces, a face without samples, and one whose extent no compiler writes or
/// whose samples the file does not hold.
[[nodiscard]] auto lightmapExtentOf(const assets::BspView& map, const FaceToDraw& entry)
    -> std::optional<LightmapExtent> {
    const assets::TextureInfo& info = map.textureInfos()[entry.face->textureInfo.value];
    if (entry.isSky || entry.isTurbulent || info.isSpecial || !entry.face->lightmapOffset) {
        return std::nullopt;
    }
    const std::optional<SurfaceExtent> area = surfaceExtentOf(map, *entry.face);
    if (!area || area->width > kAtlasWidth || area->height > kAtlasWidth) {
        return std::nullopt;
    }
    const std::uint32_t maps = lightmapCountOf(*entry.face);
    const std::size_t sampleBytes = std::size_t{area->width} * area->height * kRgbBytes;
    if (*entry.face->lightmapOffset + (sampleBytes * maps) > map.lighting().size()) {
        return std::nullopt;
    }
    return LightmapExtent{.area = *area, .maps = maps};
}

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

/// Every face's lightmaps at one place of a layer each of the atlas.
struct LightmapAtlas {
    rhi::Extent2d extent{};
    std::vector<std::byte> texels{};
    std::array<std::size_t, assets::kFaceLightStyles> layerOffsets{}; ///< Where each layer starts.
};

/// Copies the samples of the lightmaps of `entry`, which follow one another in the file, into
/// `atlas`, a layer each, at its corner.
void copyLightmaps(const assets::BspView& map, const FaceToDraw& entry, LightmapAtlas& atlas) {
    const std::span<const std::byte> samples = map.lighting().subspan(*entry.face->lightmapOffset);
    const SurfaceExtent& area = entry.lightmap->area;
    const std::size_t mapSamples = std::size_t{area.width} * area.height;
    for (std::size_t lightmap = 0; lightmap < entry.lightmap->maps; ++lightmap) {
        for (std::uint32_t y = 0; y < area.height; ++y) {
            for (std::uint32_t x = 0; x < area.width; ++x) {
                const std::size_t from =
                    ((lightmap * mapSamples) + (std::size_t{y} * area.width) + x) * kRgbBytes;
                const std::size_t texel =
                    (std::size_t{entry.corner[1] + y} * atlas.extent.width) + entry.corner[0] + x;
                const std::size_t to = atlas.layerOffsets.at(lightmap) + (texel * kRgbaBytes);
                std::ranges::copy(samples.subspan(from, kRgbBytes),
                                  atlas.texels.begin() + narrow<std::ptrdiff_t>(to));
                atlas.texels[to + kRgbBytes] = kFull;
            }
        }
    }
}

/// The lightmaps of `faces`: each lit face's extent, packed into the atlas, its lightmaps at the same
/// place of each layer; the faces drawn unlit get none.
[[nodiscard]] auto packLightmaps(const assets::BspView& map, std::span<FaceToDraw> faces) -> LightmapAtlas {
    ShelfPacker packer;
    for (FaceToDraw& entry : faces) {
        entry.lightmap = lightmapExtentOf(map, entry);
        if (entry.lightmap) {
            entry.corner = packer.place(entry.lightmap->area.width, entry.lightmap->area.height);
        }
    }
    LightmapAtlas atlas{.extent = {.width = kAtlasWidth, .height = std::max(packer.height(), 1U)}};
    const std::size_t layerBytes = std::size_t{atlas.extent.width} * atlas.extent.height * kRgbaBytes;
    for (std::size_t layer = 0; layer < atlas.layerOffsets.size(); ++layer) {
        atlas.layerOffsets.at(layer) = layer * layerBytes;
    }
    atlas.texels.assign(layerBytes * assets::kFaceLightStyles, std::byte{0});
    for (const FaceToDraw& entry : faces) {
        if (entry.lightmap) {
            copyLightmaps(map, entry, atlas);
        }
    }
    return atlas;
}

/// The light table: what the summed light of a sample is drawn as (nx.render:gamma), gray texels.
[[nodiscard]] auto lightTableTexels(const GammaSettings& settings) -> std::vector<std::byte> {
    std::vector<std::byte> texels;
    texels.reserve(kLightTableSize * kRgbaBytes);
    for (const std::uint8_t level : lightTable(settings)) {
        texels.insert(texels.end(), {std::byte{level}, std::byte{level}, std::byte{level}, kFull});
    }
    return texels;
}

/// The cell of the grid the original cuts turbulent faces along, its warp computed at their corners.
constexpr float kTurbulentCell = 64.0F;
/// What a cut leaves on either side, at least, for the original to make it.
constexpr float kLeastCut = 8.0F;
/// A turbulent face wider than any map the original loads is not cut (it would make millions).
constexpr float kWidestCutFace = 16384.0F;

/// The pieces of `polygon` along the axial planes every 64 units, as the original's
/// SubdividePolygon_r cuts a turbulent face: on each axis, at the plane of the grid nearest the
/// middle, unless that leaves less than 8 units on a side.
void cutAlongTheGrid(const std::vector<Vec3>& polygon, std::vector<std::vector<Vec3>>& pieces) {
    Aabb bounds{.min = polygon.front(), .max = polygon.front()};
    for (const Vec3 point : polygon) {
        bounds = bounds.including(point);
    }
    constexpr float kHalf = 0.5F;
    for (const auto along : {+[](Vec3 point) { return point.x; }, +[](Vec3 point) { return point.y; },
                             +[](Vec3 point) { return point.z; }}) {
        const float middle = (along(bounds.min) + along(bounds.max)) * kHalf;
        const float cut = kTurbulentCell * std::floor((middle / kTurbulentCell) + kHalf);
        if (along(bounds.max) - cut < kLeastCut || cut - along(bounds.min) < kLeastCut) {
            continue;
        }
        std::vector<Vec3> front;
        std::vector<Vec3> back;
        for (std::size_t corner = 0; corner < polygon.size(); ++corner) {
            const Vec3 point = polygon[corner];
            const Vec3 next = polygon[(corner + 1) % polygon.size()];
            const float distance = along(point) - cut;
            const float nextDistance = along(next) - cut;
            if (distance >= 0.0F) {
                front.push_back(point);
            }
            if (distance <= 0.0F) {
                back.push_back(point);
            }
            if (distance != 0.0F && nextDistance != 0.0F && (distance > 0.0F) != (nextDistance > 0.0F)) {
                const Vec3 crossing = point + ((next - point) * (distance / (distance - nextDistance)));
                front.push_back(crossing);
                back.push_back(crossing);
            }
        }
        cutAlongTheGrid(front, pieces);
        cutAlongTheGrid(back, pieces);
        return;
    }
    pieces.push_back(polygon);
}

/// The vertex of `point` of `entry`, its texture `use`: on a turbulent face, its texture coordinates
/// in texels and without the texture's shift (R_TextureCoord), unlit; else, from 0 to 1 across the
/// texture, with its lightmaps' luxel and styles.
[[nodiscard]] auto vertexOf(Vec3 point, const FaceToDraw& entry, const assets::TextureInfo& info,
                            const TextureUse& use) -> WorldVertex {
    WorldVertex vertex{.position = point}; // a brush entity's in its model's space
    if (entry.isTurbulent) {
        vertex.u = dot(point, Vec3{.x = info.s.x, .y = info.s.y, .z = info.s.z});
        vertex.v = dot(point, Vec3{.x = info.t.x, .y = info.t.y, .z = info.t.z});
        vertex.flags = kTurbulent;
        return vertex;
    }
    const double s = textureCoordinateOf(point, info.s);
    const double t = textureCoordinateOf(point, info.t);
    vertex.u = static_cast<float>(s / double{use.width});
    vertex.v = static_cast<float>(t / double{use.height});
    if (entry.lightmap) {
        // Luxel centers: the first luxel's s is minS, 16 texels apart.
        const SurfaceExtent& area = entry.lightmap->area;
        vertex.lightmapX =
            static_cast<float>(((s - area.minS) / kLuxelUnits) + kLuxelCenter + entry.corner[0]);
        vertex.lightmapY =
            static_cast<float>(((t - area.minT) / kLuxelUnits) + kLuxelCenter + entry.corner[1]);
        vertex.styles = entry.face->lightStyles;
    }
    return vertex;
}

/// The triangles of the faces, and how each is drawn.
struct Geometry {
    std::vector<WorldVertex> vertices;
    std::vector<std::uint32_t> faceIndices; ///< Each face's triangles, in vertices.
    std::vector<FaceDraw> faces;            ///< By the index of the face in the map.
    /// The textures each bucket of faces shows.
    std::vector<BucketTextures> bucketTextures;
};

/// The triangles of `faces`, each a fan, a turbulent face cut along the grid first; and the bucket of
/// each, its texture's, or the sky's, whose triangles each frame gathers when the camera sees it.
[[nodiscard]] auto geometryOf(const assets::BspView& map, std::span<const FaceToDraw> faces,
                              std::span<const TextureUse> uses) -> Geometry {
    Geometry geometry;
    geometry.faces.resize(map.faces().size());
    std::vector<std::optional<std::uint32_t>> bucketOfTexture(uses.size());
    std::vector<std::vector<Vec3>> pieces;
    for (const FaceToDraw& entry : faces) {
        const assets::TextureInfo& info = map.textureInfos()[entry.face->textureInfo.value];
        std::vector<Vec3> outline;
        for (std::uint32_t edge = 0; edge < entry.face->edgeCount; ++edge) {
            outline.push_back(cornerOf(map, *entry.face, edge));
        }
        Aabb bounds{.min = outline.front(), .max = outline.front()};
        for (const Vec3 point : outline) {
            bounds = bounds.including(point);
        }
        pieces.clear();
        const Vec3 size = bounds.size();
        if (entry.isTurbulent && std::max({size.x, size.y, size.z}) <= kWidestCutFace) {
            cutAlongTheGrid(outline, pieces);
        } else {
            pieces.push_back(std::move(outline));
        }
        const auto firstIndex = narrow<std::uint32_t>(geometry.faceIndices.size());
        // The original puts each piece first in its list of them: the last one cut leads it.
        const float waveBase = pieces.back().front().z;
        for (const std::vector<Vec3>& piece : pieces) {
            const auto first = narrow<std::uint32_t>(geometry.vertices.size());
            for (const Vec3 point : piece) {
                WorldVertex vertex = vertexOf(point, entry, info, uses[entry.texture]);
                if (entry.isTurbulent) {
                    vertex.waveBase = waveBase;
                }
                geometry.vertices.push_back(vertex);
            }
            for (std::uint32_t corner = kTriangleCorners - 1; corner < piece.size(); ++corner) {
                geometry.faceIndices.insert(geometry.faceIndices.end(),
                                            {first, first + corner - 1, first + corner});
            }
        }
        std::uint32_t bucket = kSkyBucket;
        if (!entry.isSky) {
            std::optional<std::uint32_t>& slot = bucketOfTexture[entry.texture];
            if (!slot) {
                slot = narrow<std::uint32_t>(geometry.bucketTextures.size());
                const auto texturesOf = [uses](const std::vector<std::size_t>& frames) {
                    return frames |
                           std::views::transform([uses](std::size_t frame) { return uses[frame].texture; }) |
                           std::ranges::to<std::vector>();
                };
                geometry.bucketTextures.push_back(BucketTextures{
                    .frames = texturesOf(framesOf(map.textures(), entry.texture)),
                    .alternates = texturesOf(alternateFramesOf(map.textures(), entry.texture))});
            }
            bucket = *slot;
        }
        geometry.faces[entry.index] =
            FaceDraw{.firstIndex = firstIndex,
                     .indexCount = narrow<std::uint32_t>(geometry.faceIndices.size()) - firstIndex,
                     .bucket = bucket,
                     .plane = map.planes()[entry.face->plane.value].plane,
                     .isBackSide = entry.face->isBackSide,
                     .middle = bounds.center()};
    }
    return geometry;
}

/// The value `rendermode`, `renderamt` and `rendercolor` are held to, from 0.
constexpr int kRenderValueTop = 255;

/// The brush entities' models, from model 1, as their entities ("model" "*3") place and draw them
/// (`origin`, `rendermode`, `renderamt`, `rendercolor`, each value held to 0 to 255). A mode the
/// original does not know draws as its default does, as Glow.
[[nodiscard]] auto modelDrawsOf(const assets::BspView& map) -> std::vector<ModelDraw> {
    constexpr auto kTop = static_cast<float>(kRenderValueTop);
    std::vector<ModelDraw> models;
    for (const assets::BspModel& model : map.models() | std::views::drop(1)) {
        models.push_back(ModelDraw{
            .firstFace = model.firstFace.value, .faceCount = model.faceCount, .bounds = model.bounds});
    }
    for (const assets::BspEntity& entity : map.entities()) {
        const std::optional<assets::ModelIndex> index = entity.findBrushModel();
        if (!index || *index == assets::kWorldModel || index->value > models.size()) {
            continue;
        }
        ModelDraw& drawn = models[index->value - 1]; // from model 1
        drawn.origin = entity.findVector("origin").value_or(Vec3{});
        const int mode = entity.findInteger("rendermode").value_or(0);
        drawn.mode = mode >= 0 && mode < std::to_underlying(RenderMode::Count) ? static_cast<RenderMode>(mode)
                                                                               : RenderMode::Glow;
        const auto amount =
            static_cast<float>(std::clamp(entity.findInteger("renderamt").value_or(0), 0, kRenderValueTop)) /
            kTop;
        const Vec3 color = entity.findVector("rendercolor").value_or(Vec3{});
        drawn.color = Vec4{.x = std::clamp(color.x, 0.0F, kTop) / kTop,
                           .y = std::clamp(color.y, 0.0F, kTop) / kTop,
                           .z = std::clamp(color.z, 0.0F, kTop) / kTop,
                           .w = amount};
        // R_AddEntity: drawn other than normally, nothing at a renderamt of 0.
        drawn.isDrawn = drawn.mode == RenderMode::Normal || amount > 0.0F;
    }
    return models;
}

/// The textures every frame binds.
struct FrameTextures {
    rhi::OwnedTexture lightmaps;
    rhi::OwnedTexture lightTable;
    rhi::OwnedTexture sky;
};

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
            layers.texels.insert(layers.texels.end(), {kNoSkyLevel, kNoSkyLevel, kNoSkyLevel, kFull});
        }
        return layers;
    }
    const auto isOfTheFirstsSize = [&sky](const assets::RgbaImage& side) {
        return side.width == sky.front().width && side.height == sky.front().height &&
               side.pixels.size() == std::size_t{side.width} * side.height * kRgbaBytes;
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

/// The textures of `atlas`, of the light table of `settings` and of `sky`, the sides of the sky in
/// the order the shader picks them. Errors: those of the uploads.
[[nodiscard]] auto uploadFrameTextures(Uploader& uploader, const LightmapAtlas& atlas,
                                       const GammaSettings& settings, const SkyLayers& sky)
    -> Result<FrameTextures, rhi::RhiError> {
    auto lightmaps =
        uploader.texture("world.lightmaps", atlas.extent, 1, narrow<std::uint32_t>(assets::kFaceLightStyles),
                         atlas.texels, atlas.layerOffsets);
    if (!lightmaps) {
        return std::unexpected{std::move(lightmaps.error())};
    }
    auto lightTable =
        uploader.texture("world.lightTable", {.width = narrow<std::uint32_t>(kLightTableSize), .height = 1},
                         1, 1, lightTableTexels(settings), kOneLevel);
    if (!lightTable) {
        return std::unexpected{std::move(lightTable.error())};
    }
    auto skySides = uploader.texture("world.sky", sky.extent, 1, narrow<std::uint32_t>(kSkySuffixes.size()),
                                     sky.texels, sky.offsets);
    if (!skySides) {
        return std::unexpected{std::move(skySides.error())};
    }
    return FrameTextures{.lightmaps = std::move(*lightmaps),
                         .lightTable = std::move(*lightTable),
                         .sky = std::move(*skySides)};
}

} // namespace

auto BspWorld::create(rhi::Device& device, rhi::CommandList& commands, const BspWorldDesc& desc)
    -> Result<BspWorld, WorldError> {
    const assets::BspView& map = desc.map;
    auto skyLayers = skyLayersOf(desc.sky);
    if (!skyLayers) {
        return std::unexpected{std::move(skyLayers.error())};
    }
    const auto gpuFailure = [](Failure<rhi::RhiError> failure) {
        return fail(WorldError::GpuFailed, std::move(failure));
    };
    BspWorld world{device, map};
    Uploader uploader{device, commands};
    auto textures = loadTextures(uploader, desc);
    if (!textures) {
        return gpuFailure(std::move(textures.error()));
    }
    std::vector<FaceToDraw> faces = facesToDraw(map, textures->uses);
    const LightmapAtlas atlas = packLightmaps(map, faces);
    auto frameTextures = uploadFrameTextures(uploader, atlas, desc.gamma, *skyLayers);
    if (!frameTextures) {
        return gpuFailure(std::move(frameTextures.error()));
    }
    Geometry geometry = geometryOf(map, faces, textures->uses);
    world.m_lighting = {.x = static_cast<float>(lightScale(desc.gamma)),
                        .y = static_cast<float>(atlas.extent.width),
                        .z = static_cast<float>(atlas.extent.height)};
    world.m_faces = std::move(geometry.faces);
    world.m_faceIndices = std::move(geometry.faceIndices);
    world.m_models = modelDrawsOf(map);
    world.m_faceGathered.resize(world.m_faces.size());
    world.m_bucketIndices.resize(geometry.bucketTextures.size());
    world.m_stats = WorldStats{.faces = faces.size(),
                               .triangles = world.m_faceIndices.size() / kTriangleCorners,
                               .textures = textures->loaded,
                               .missingTextures = std::move(textures->missing),
                               .lightmapAtlas = atlas.extent};
    if (auto made = world.createLayouts(desc.shaderCode)
                        .and_then([&] { return world.createPipelines(desc.colorFormat); })
                        .and_then([&] {
                            return world.createFrameSlots(frameTextures->lightmaps.get(),
                                                          frameTextures->lightTable.get(),
                                                          frameTextures->sky.get());
                        })
                        .and_then([&] { return world.createBucketGroups(geometry.bucketTextures); });
        !made) {
        return gpuFailure(std::move(made.error()));
    }
    auto vertices = uploader.buffer("world.vertices", rhi::BufferUsage::Vertex, rhi::BufferState::Vertex,
                                    std::as_bytes(std::span{geometry.vertices}));
    if (!vertices) {
        return gpuFailure(std::move(vertices.error()));
    }
    world.m_vertices = std::move(*vertices);
    world.m_textures = std::move(textures->owned);
    for (rhi::OwnedTexture* texture :
         {&frameTextures->lightmaps, &frameTextures->lightTable, &frameTextures->sky}) {
        world.m_textures.push_back(std::move(*texture));
    }
    log::info(kLog, "world: {} faces, {} triangles, {} textures, lightmaps {}x{}, {} missing",
              world.m_stats.faces, world.m_stats.triangles, world.m_stats.textures, atlas.extent.width,
              atlas.extent.height, world.m_stats.missingTextures.size());
    return world;
}

namespace {

/// What a brush model of bounds `bounds` covers, placed by `placement`: its bounds there; turned, a
/// cube as wide as its farthest corner, as the original culls a turned brush entity.
[[nodiscard]] auto boundsOf(const Aabb& bounds, const ModelPlacement& placement) -> Aabb {
    if (placement.angles == EulerAngles{}) {
        return bounds.translated(placement.origin);
    }
    const Vec3 corner =
        componentMax(Vec3{std::abs(bounds.min.x), std::abs(bounds.min.y), std::abs(bounds.min.z)},
                     Vec3{std::abs(bounds.max.x), std::abs(bounds.max.y), std::abs(bounds.max.z)});
    const float radius = length(corner);
    const Vec3 reach{radius, radius, radius};
    return Aabb{.min = placement.origin - reach, .max = placement.origin + reach};
}

} // namespace

void BspWorld::place(std::span<const ModelPlacement> placements) {
    m_placements.resize(m_models.size());
    for (std::size_t index = 0; index < m_models.size(); ++index) {
        m_placements[index] = ModelPlacement{.model = assets::ModelIndex{narrow<std::uint32_t>(index + 1)},
                                             .origin = m_models[index].origin};
    }
    for (const ModelPlacement& placement : placements) {
        const std::uint32_t model = placement.model.value;
        expects(model >= 1 && model <= m_models.size(),
                "BspWorld::prepare: a placement of a brush model of the map");
        if (model >= 1 && model <= m_models.size()) {
            m_placements[model - 1] = placement; // m_models starts at model 1
        }
    }
}

auto BspWorld::isSeen(std::uint32_t face, Vec3 eye, bool isOfAnEntity) const noexcept -> bool {
    const FaceDraw& draw = m_faces[face];
    const float distance = draw.plane.signedDistance(eye);
    const bool isFacing = isOfAnEntity
                              ? (draw.isBackSide ? distance < -kBackfaceEpsilon : distance > kBackfaceEpsilon)
                              : (distance < 0.0F) == draw.isBackSide;
    return draw.indexCount != 0 && isFacing;
}

void BspWorld::addFace(std::uint32_t face, Vec3 eye) {
    if (m_faceGathered[face] == m_drawCount) {
        return; // in another leaf already
    }
    m_faceGathered[face] = m_drawCount;
    if (!isSeen(face, eye, false)) {
        return;
    }
    const FaceDraw& draw = m_faces[face];
    std::vector<std::uint32_t>& indices =
        draw.bucket == kSkyBucket ? m_skyIndices : m_bucketIndices[draw.bucket];
    const auto triangles = std::span{m_faceIndices}.subspan(draw.firstIndex, draw.indexCount);
    indices.insert(indices.end(), triangles.begin(), triangles.end());
    ++m_lastFrame.faces;
}

void BspWorld::addEntity(std::uint32_t model, const Camera& camera, const ModelPlacement& placement) {
    const ModelDraw& drawn = m_models[model];
    const bool isTurned = placement.angles != EulerAngles{};
    const AngleVectors axes = angleVectors(placement.angles);
    // The eye in the model's space, as the original's R_DrawBrushModel turns it.
    const Vec3 eye =
        isTurned ? intoAxes(axes, camera.position - placement.origin) : camera.position - placement.origin;
    const auto placed = [&](Vec3 point) {
        return placement.origin + (isTurned ? outOfAxes(axes, point) : point);
    };
    const auto first = narrow<std::uint32_t>(m_entityFaces.size());
    for (std::uint32_t face = drawn.firstFace; face < drawn.firstFace + drawn.faceCount; ++face) {
        if (m_faces[face].bucket != kSkyBucket && isSeen(face, eye, true)) {
            m_entityFaces.push_back(face);
        }
    }
    const auto count = narrow<std::uint32_t>(m_entityFaces.size()) - first;
    if (count == 0) {
        return;
    }
    if (drawn.mode == RenderMode::TransTexture) {
        // R_SurfaceCompare: the farthest along the view first.
        const Vec3 forward = angleVectors(camera.angles).forward;
        std::ranges::stable_sort(std::span{m_entityFaces}.subspan(first, count), std::ranges::greater{},
                                 [this, forward, &placed](std::uint32_t face) {
                                     return dot(placed(m_faces[face].middle), forward);
                                 });
    }
    for (const std::uint32_t face : std::span{m_entityFaces}.subspan(first, count)) {
        const FaceDraw& draw = m_faces[face];
        const auto firstIndex = narrow<std::uint32_t>(m_entityIndices.size());
        const auto triangles = std::span{m_faceIndices}.subspan(draw.firstIndex, draw.indexCount);
        m_entityIndices.insert(m_entityIndices.end(), triangles.begin(), triangles.end());
        m_entityRanges.push_back(
            IndexRange{.firstIndex = firstIndex,
                       .indexCount = narrow<std::uint32_t>(m_entityIndices.size()) - firstIndex});
    }
    m_entityDraws.push_back(
        EntityDraw{.model = model,
                   .distance = lengthSquared(drawn.bounds.center() + placement.origin - camera.position),
                   .firstFace = first,
                   .faceCount = count});
    m_lastFrame.faces += count;
}

void BspWorld::gather(const Camera& camera, float aspect) {
    m_lastFrame = {};
    for (std::vector<std::uint32_t>& indices : m_bucketIndices) {
        indices.clear();
    }
    m_skyIndices.clear();
    m_entityDraws.clear();
    m_blendedDraws.clear();
    m_entityFaces.clear();
    m_entityIndices.clear();
    m_entityRanges.clear();

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

    const auto frustum = frustumPlanes(camera, aspect);
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
            addFace(m_map->markSurfaces()[record.firstMarkSurface.value + mark].value, camera.position);
        }
    }
    for (std::uint32_t index = 0; index < m_models.size(); ++index) {
        const ModelDraw& model = m_models[index];
        const ModelPlacement& placement = m_placements[index];
        if (model.isDrawn && isInView(boundsOf(model.bounds, placement))) {
            addEntity(index, camera, placement);
        }
    }

    // The solid entities are drawn with the world; the others among everything drawn blended, a
    // TransAlpha one before all of them (R_TransEntityCompare).
    for (std::uint32_t index = 0; index < m_entityDraws.size(); ++index) {
        const RenderMode mode = m_models[m_entityDraws[index].model].mode;
        if (mode != RenderMode::Normal) {
            m_blendedDraws.push_back(BlendedDraw{
                .distance = mode == RenderMode::TransAlpha ? kFarthest : m_entityDraws[index].distance,
                .mode = mode,
                .index = index});
        }
    }
}

void BspWorld::prepare(const WorldFrame& view) {
    // A ring of kMaxFramesInFlight: the slot written now was last read frames in flight ago.
    const FrameSlot& slot = m_frameSlots[m_drawCount++ % m_frameSlots.size()];
    FrameUniform frame{.viewProjection = viewProjectionMatrix(view.camera, view.aspect),
                       .lighting = m_lighting,
                       .eye = extend(view.camera.position, 1.0F)};
    std::ranges::transform(view.lightStyles, frame.styles.begin(),
                           [](std::uint16_t value) { return static_cast<float>(value); });
    frame.lighting.w = view.time.count(); // what turbulent faces warp with
    std::ranges::copy(std::as_bytes(std::span{&frame, 1}), m_device->mappedBytes(slot.buffer.get()).begin());

    // Where each brush model is drawn, and how it is blended.
    place(view.placements);
    const std::span<std::byte> entityBytes = m_device->mappedBytes(slot.entities.get());
    const auto writeEntity = [&entityBytes](std::size_t model, const EntityUniform& uniform) {
        std::ranges::copy(std::as_bytes(std::span{&uniform, 1}),
                          entityBytes.subspan(kEntityUniformStride * model).begin());
    };
    writeEntity(0, EntityUniform{});
    for (std::size_t index = 0; index < m_models.size(); ++index) {
        const ModelPlacement& placement = m_placements[index];
        EntityUniform uniform{.color = m_models[index].color,
                              .origin = extend(placement.origin, placement.waveHeight)};
        if (placement.angles != EulerAngles{}) {
            const Mat3 turn = rotationMatrix(placement.angles);
            for (std::size_t row = 0; row < Mat3::kRowCount; ++row) {
                uniform.turn.at(row) = extend(turn.rows.at(row), 0.0F);
            }
        }
        writeEntity(index + 1, uniform);
    }

    // The indices of what the camera sees, a batch for each texture that has some, then the sky's.
    // An animated texture shows its frames ten a second, as the original's R_TextureAnimation.
    gather(view.camera, view.aspect);
    m_animationStep = static_cast<std::size_t>(std::max(view.time.count(), 0.0F) * kAnimationFramesPerSecond);
    const std::span<std::byte> indexBytes = m_device->mappedBytes(slot.indices.get());
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
            const std::vector<rhi::OwnedBindGroup>& frames = m_bucketFrames[bucket].frames;
            m_batches.push_back(Batch{.indices = write(m_bucketIndices[bucket]),
                                      .textures = frames[m_animationStep % frames.size()].get()});
        }
    }
    m_sky = write(m_skyIndices);
    m_firstEntityIndex = write(m_entityIndices).firstIndex;
    m_lastFrame.triangles = written / kTriangleCorners;
}

void BspWorld::draw(rhi::CommandList& commands, const WorldFrame& view) {
    prepare(view);
    drawInOrder(commands, *this);
}

void BspWorld::drawSolid(rhi::CommandList& commands) const {
    const FrameSlot& slot = preparedSlot();
    commands.setPipeline(m_pipeline.get());
    commands.setVertexBuffer(0, m_vertices.get(), 0);
    commands.setIndexBuffer(slot.indices.get(), 0, rhi::IndexFormat::Uint32);
    commands.setBindGroup(kFrameGroup, slot.group.get());
    commands.setBindGroup(kEntityGroup, slot.entityGroups[0].get());
    for (const Batch& batch : m_batches) {
        commands.setBindGroup(kTextureGroup, batch.textures);
        commands.drawIndexed(batch.indices.indexCount, 1, batch.indices.firstIndex, 0, 0);
    }
    if (m_sky.indexCount != 0) {
        // Last, so that what the world hides of them fails the depth test before it is shaded.
        commands.setPipeline(m_skyPipeline.get());
        commands.setBindGroup(kFrameGroup, slot.group.get());
        commands.drawIndexed(m_sky.indexCount, 1, m_sky.firstIndex, 0, 0);
    }
    // Then the solid brush entities, in their order.
    for (const EntityDraw& entity : m_entityDraws) {
        if (m_models[entity.model].mode == RenderMode::Normal) {
            drawEntity(commands, entity);
        }
    }
}

void BspWorld::drawBlended(rhi::CommandList& commands, std::uint32_t index) const {
    if (index >= m_entityDraws.size()) {
        expects(false, "BspWorld::drawBlended: one of blendedDraws()'s");
        return;
    }
    // Another renderer may have drawn since the world's buffers were bound.
    commands.setVertexBuffer(0, m_vertices.get(), 0);
    commands.setIndexBuffer(preparedSlot().indices.get(), 0, rhi::IndexFormat::Uint32);
    drawEntity(commands, m_entityDraws[index]);
}

void BspWorld::drawEntity(rhi::CommandList& commands, const EntityDraw& entity) const {
    const FrameSlot& slot = preparedSlot();
    const RenderMode mode = m_models[entity.model].mode;
    commands.setPipeline(pipelineOf(mode));
    commands.setBindGroup(kFrameGroup, slot.group.get());
    commands.setBindGroup(kEntityGroup, slot.entityGroups[std::size_t{entity.model} + 1].get());
    const bool showsAlternates = m_placements[entity.model].showsAlternateTextures;
    for (std::uint32_t face = entity.firstFace; face < entity.firstFace + entity.faceCount; ++face) {
        if (mode != RenderMode::TransColor) {
            const BucketFrames& bucket = m_bucketFrames[m_faces[m_entityFaces[face]].bucket];
            const std::vector<rhi::OwnedBindGroup>& frames =
                showsAlternates && !bucket.alternates.empty() ? bucket.alternates : bucket.frames;
            commands.setBindGroup(kTextureGroup, frames[m_animationStep % frames.size()].get());
        }
        const IndexRange& range = m_entityRanges[face];
        commands.drawIndexed(range.indexCount, 1, m_firstEntityIndex + range.firstIndex, 0, 0);
    }
}

auto BspWorld::preparedSlot() const noexcept -> const FrameSlot& {
    expects(m_drawCount > 0, "BspWorld: prepared before it is drawn");
    return m_frameSlots[(m_drawCount - 1) % m_frameSlots.size()];
}

auto BspWorld::pipelineOf(RenderMode mode) const noexcept -> rhi::RenderPipelineHandle {
    switch (mode) {
    case RenderMode::Normal:
        return m_pipeline.get();
    case RenderMode::TransAlpha:
        return m_solidPipeline.get();
    case RenderMode::TransColor:
        return m_colorPipeline.get();
    case RenderMode::TransAdd:
        return m_additivePipeline.get();
    case RenderMode::TransTexture:
    case RenderMode::Glow:
    case RenderMode::Count:
        break;
    }
    return m_texturePipeline.get();
}

auto BspWorld::createLayouts(std::span<const std::byte> shaderCode) -> Result<void, rhi::RhiError> {
    rhi::Device& device = *m_device;
    constexpr Flags<rhi::ShaderStage> kFragment{rhi::ShaderStage::Fragment};
    constexpr Flags<rhi::ShaderStage> kBothStages{rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment};
    // The frame: the matrix and the lighting; the lightmaps, the light table, the sky and its sampler.
    const std::array frameBindings{
        rhi::BindingLayout{
            .binding = kFrameUniformBinding, .type = rhi::BindingType::UniformBuffer, .stages = kBothStages},
        rhi::BindingLayout{
            .binding = kLightmapsBinding, .type = rhi::BindingType::SampledTexture, .stages = kFragment},
        rhi::BindingLayout{
            .binding = kLightTableBinding, .type = rhi::BindingType::SampledTexture, .stages = kFragment},
        rhi::BindingLayout{
            .binding = kSkySidesBinding, .type = rhi::BindingType::SampledTexture, .stages = kFragment},
        rhi::BindingLayout{
            .binding = kSkySamplerBinding, .type = rhi::BindingType::Sampler, .stages = kFragment}};
    const std::array textureBindings{
        rhi::BindingLayout{
            .binding = kAlbedoBinding, .type = rhi::BindingType::SampledTexture, .stages = kFragment},
        rhi::BindingLayout{
            .binding = kAlbedoSamplerBinding, .type = rhi::BindingType::Sampler, .stages = kFragment}};
    // Where the brush model drawn is, and how a brush entity drawn blended is blended.
    const std::array entityBindings{rhi::BindingLayout{
        .binding = kEntityUniformBinding, .type = rhi::BindingType::UniformBuffer, .stages = kBothStages}};
    auto shader = device.createShader({.label = "world", .code = shaderCode});
    auto frameLayout = device.createBindGroupLayout({.label = "world.frame", .bindings = frameBindings});
    auto textureLayout =
        device.createBindGroupLayout({.label = "world.texture", .bindings = textureBindings});
    auto entityLayout = device.createBindGroupLayout({.label = "world.entity", .bindings = entityBindings});
    auto textureSampler = device.createSampler({.label = "world.texture"});
    // Each side clamped at its edges, as the original draws them.
    auto skySampler = device.createSampler(
        {.label = "world.sky", .mipmap = rhi::Filter::Nearest, .address = rhi::AddressMode::ClampToEdge});
    if (!shader || !frameLayout || !textureLayout || !entityLayout || !textureSampler || !skySampler) {
        return std::unexpected(!shader           ? std::move(shader.error())
                               : !frameLayout    ? std::move(frameLayout.error())
                               : !textureLayout  ? std::move(textureLayout.error())
                               : !entityLayout   ? std::move(entityLayout.error())
                               : !textureSampler ? std::move(textureSampler.error())
                                                 : std::move(skySampler.error()));
    }
    m_shader = std::move(*shader);
    m_frameLayout = std::move(*frameLayout);
    m_textureLayout = std::move(*textureLayout);
    m_entityLayout = std::move(*entityLayout);
    m_textureSampler = std::move(*textureSampler);
    m_skySampler = std::move(*skySampler);
    return {};
}

auto BspWorld::createPipelines(rhi::Format colorFormat) -> Result<void, rhi::RhiError> {
    rhi::Device& device = *m_device;
    const std::array vertexBuffers{
        rhi::VertexBufferLayout{.stride = sizeof(WorldVertex), .attributes = kWorldVertexAttributes}};
    const std::array layouts{m_frameLayout.get(), m_textureLayout.get(), m_entityLayout.get()};
    const std::array skyLayouts{m_frameLayout.get()};
    // The world and the sky, opaque; a brush entity drawn blended: its textures (but TransColor)
    // blended over what is behind, as R_SetRenderMode blends them; TransColor and TransAlpha write
    // their depth, the other blended ones do not.
    const rhi::BlendState alphaBlend{.sourceColor = rhi::BlendFactor::SourceAlpha,
                                     .destinationColor = rhi::BlendFactor::OneMinusSourceAlpha,
                                     .sourceAlpha = rhi::BlendFactor::SourceAlpha,
                                     .destinationAlpha = rhi::BlendFactor::OneMinusSourceAlpha};
    const rhi::BlendState additiveBlend{.sourceColor = rhi::BlendFactor::One,
                                        .destinationColor = rhi::BlendFactor::One,
                                        .sourceAlpha = rhi::BlendFactor::One,
                                        .destinationAlpha = rhi::BlendFactor::One};
    enum class Depth : std::uint8_t { Tested, TestedAndWritten };
    const auto pipeline = [&](std::string_view label, std::string_view vertex, std::string_view fragment,
                              std::optional<rhi::BlendState> blend, Depth depth,
                              std::span<const rhi::BindGroupLayoutHandle> groupLayouts) {
        const std::array targets{rhi::ColorTarget{.format = colorFormat, .blend = blend}};
        return device.createRenderPipeline(
            {.label = label,
             .vertex = {.shader = m_shader.get(), .entryPoint = vertex},
             .fragment = {.shader = m_shader.get(), .entryPoint = fragment},
             .vertexBuffers = vertexBuffers,
             .colorTargets = targets,
             .depth = rhi::DepthState{.isWriteEnabled = depth == Depth::TestedAndWritten,
                                      .compare = rhi::CompareOp::Greater},
             .bindGroupLayouts = groupLayouts});
    };
    auto opaque =
        pipeline("world", "vertexMain", "fragmentMain", std::nullopt, Depth::TestedAndWritten, layouts);
    auto sky = pipeline("world.sky", "skyVertexMain", "skyFragmentMain", std::nullopt,
                        Depth::TestedAndWritten, skyLayouts);
    auto solid = pipeline("world.solid", "vertexMain", "solidFragmentMain", std::nullopt,
                          Depth::TestedAndWritten, layouts);
    auto texture =
        pipeline("world.texture", "vertexMain", "textureFragmentMain", alphaBlend, Depth::Tested, layouts);
    auto color = pipeline("world.color", "vertexMain", "colorFragmentMain", alphaBlend,
                          Depth::TestedAndWritten, layouts);
    auto additive = pipeline("world.additive", "vertexMain", "additiveFragmentMain", additiveBlend,
                             Depth::Tested, layouts);
    for (auto* const created : {&opaque, &sky, &solid, &texture, &color, &additive}) {
        if (!*created) {
            return std::unexpected{std::move(created->error())};
        }
    }
    m_pipeline = std::move(*opaque);
    m_skyPipeline = std::move(*sky);
    m_solidPipeline = std::move(*solid);
    m_texturePipeline = std::move(*texture);
    m_colorPipeline = std::move(*color);
    m_additivePipeline = std::move(*additive);
    return {};
}

auto BspWorld::createFrameSlots(rhi::TextureHandle lightmaps, rhi::TextureHandle lightTable,
                                rhi::TextureHandle sky) -> Result<void, rhi::RhiError> {
    rhi::Device& device = *m_device;
    const std::size_t models = m_models.size() + 1; // and the world, model 0
    for (std::uint32_t index = 0; index < rhi::kMaxFramesInFlight; ++index) {
        FrameSlot& slot = m_frameSlots.emplace_back();
        auto buffer = device.createBuffer({.label = "world.frame",
                                           .size = sizeof(FrameUniform),
                                           .usage = {rhi::BufferUsage::Uniform},
                                           .location = rhi::MemoryLocation::Upload});
        // As many indices as every face has: what a frame gathers is never more.
        auto indices = device.createBuffer(
            {.label = "world.indices",
             .size = std::max<std::uint64_t>(m_faceIndices.size(), 1) * sizeof(std::uint32_t),
             .usage = {rhi::BufferUsage::Index},
             .location = rhi::MemoryLocation::Upload});
        auto entities = device.createBuffer({.label = "world.entities",
                                             .size = kEntityUniformStride * models,
                                             .usage = {rhi::BufferUsage::Uniform},
                                             .location = rhi::MemoryLocation::Upload});
        if (!buffer || !indices || !entities) {
            return std::unexpected{!buffer    ? std::move(buffer.error())
                                   : !indices ? std::move(indices.error())
                                              : std::move(entities.error())};
        }
        const std::array bindings{
            rhi::Binding{.binding = kFrameUniformBinding,
                         .resource = rhi::BufferBinding{.buffer = buffer->get()}},
            rhi::Binding{.binding = kLightmapsBinding, .resource = lightmaps},
            rhi::Binding{.binding = kLightTableBinding, .resource = lightTable},
            rhi::Binding{.binding = kSkySidesBinding, .resource = sky},
            rhi::Binding{.binding = kSkySamplerBinding, .resource = m_skySampler.get()}};
        auto group = device.createBindGroup(
            {.label = "world.frame", .layout = m_frameLayout.get(), .bindings = bindings});
        if (!group) {
            return std::unexpected{std::move(group.error())};
        }
        for (std::size_t model = 0; model < models; ++model) {
            const std::array entityBindings{
                rhi::Binding{.binding = kEntityUniformBinding,
                             .resource = rhi::BufferBinding{.buffer = entities->get(),
                                                            .offset = kEntityUniformStride * model,
                                                            .size = sizeof(EntityUniform)}}};
            auto entityGroup = device.createBindGroup(
                {.label = "world.entity", .layout = m_entityLayout.get(), .bindings = entityBindings});
            if (!entityGroup) {
                return std::unexpected{std::move(entityGroup.error())};
            }
            slot.entityGroups.push_back(std::move(*entityGroup));
        }
        slot.buffer = std::move(*buffer);
        slot.indices = std::move(*indices);
        slot.entities = std::move(*entities);
        slot.group = std::move(*group);
    }
    return {};
}

auto BspWorld::createBucketGroups(std::span<const BucketTextures> bucketTextures)
    -> Result<void, rhi::RhiError> {
    const auto groupsOf = [this](std::span<const rhi::TextureHandle> textures,
                                 std::vector<rhi::OwnedBindGroup>& groups) -> Result<void, rhi::RhiError> {
        for (const rhi::TextureHandle texture : textures) {
            const std::array bindings{
                rhi::Binding{.binding = kAlbedoBinding, .resource = texture},
                rhi::Binding{.binding = kAlbedoSamplerBinding, .resource = m_textureSampler.get()}};
            auto group = m_device->createBindGroup(
                {.label = "world.texture", .layout = m_textureLayout.get(), .bindings = bindings});
            if (!group) {
                return std::unexpected{std::move(group.error())};
            }
            groups.push_back(std::move(*group));
        }
        return {};
    };
    for (const BucketTextures& textures : bucketTextures) {
        BucketFrames& groups = m_bucketFrames.emplace_back();
        if (auto made = groupsOf(textures.frames, groups.frames).and_then([&] {
                return groupsOf(textures.alternates, groups.alternates);
            });
            !made) {
            return made;
        }
    }
    return {};
}

} // namespace nx::render
