// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.render;

import std;
import nx.core;
import nx.math;
import nx.assets;

namespace nx::render {

namespace {

/// A decal's scale, held to these (MIN_DECAL_SCALE, MAX_DECAL_SCALE).
constexpr float kLeastScale = 0.01F;
constexpr float kMostScale = 16.0F;
/// A node's faces are tried when the decal's center is closer than this to its plane
/// (DECAL_DISTANCE).
constexpr float kNodeReach = 4.0F;
/// A decal over this many others on a face, each covered more than kCoveredShare, replaces the one it
/// covers least (MAX_OVERLAP_DECALS).
constexpr std::size_t kMostCovered = 6;
constexpr double kCoveredShare = 0.6;
/// The most corners a decal on a face has once cut (MAX_DECALCLIPVERT).
constexpr std::size_t kMostCorners = 32;
/// The middle of a decal's image, from 0 to 1 across it.
constexpr float kMiddle = 0.5F;
/// The palette index a decal draws clear, and the color whose last place in a palette makes a decal
/// masked rather than tinted: pure blue.
constexpr std::size_t kClearIndex = 255;
constexpr std::array<std::uint8_t, 3> kMaskedColor{0, 0, 255};
constexpr std::size_t kRgbBytes = 3;
constexpr std::size_t kRgbaBytes = 4;
constexpr std::byte kOpaque{255};
/// A luxel's center, from its corner, in luxels.
constexpr double kLuxelCenter = 0.5;
/// The prefix of a conveyor's texture: its faces take no decals (SURF_CONVEYOR).
constexpr std::string_view kConveyorPrefix = "scroll";
/// The corners of a triangle.
constexpr std::size_t kTriangleCorners = 3;
/// A level of a decal's image averages each 2 x 2 texels of the level before, rounding to the nearest.
constexpr std::uint32_t kAveragedTexels = 4;
constexpr std::uint32_t kHalfTexels = kAveragedTexels / 2;

/// A corner of a face being cut to a decal: where it is, and where it falls on the decal.
struct CutCorner {
    Vec3 position{};
    float u = 0.0F;
    float v = 0.0F;
};

/// A side of the decal's square a face is cut along, in the order the original cuts it.
enum class Side : std::uint8_t { Left, Right, Top, Bottom };

[[nodiscard]] auto isInside(const CutCorner& corner, Side side) noexcept -> bool {
    switch (side) {
    case Side::Left:
        return corner.u > 0.0F;
    case Side::Right:
        return corner.u < 1.0F;
    case Side::Top:
        return corner.v > 0.0F;
    case Side::Bottom:
        return corner.v < 1.0F;
    }
    return false;
}

/// Where the edge from `from` to `to` crosses `side`: the coordinate cut exactly on the side, the
/// rest in proportion.
[[nodiscard]] auto crossing(const CutCorner& from, const CutCorner& to, Side side) noexcept -> CutCorner {
    const bool isAcross = side == Side::Left || side == Side::Right;
    const float bound = side == Side::Left || side == Side::Top ? 0.0F : 1.0F;
    const float share = isAcross ? (from.u - bound) / (from.u - to.u) : (from.v - bound) / (from.v - to.v);
    CutCorner cut{.position = from.position + ((to.position - from.position) * share),
                  .u = from.u + ((to.u - from.u) * share),
                  .v = from.v + ((to.v - from.v) * share)};
    (isAcross ? cut.u : cut.v) = bound;
    return cut;
}

/// `polygon` cut to the decal's square, a side after another (Sutherland and Hodgman's), as the
/// original cuts it; at most kMostCorners corners.
[[nodiscard]] auto cutToTheDecal(std::vector<CutCorner> polygon) -> std::vector<CutCorner> {
    std::vector<CutCorner> cut;
    for (const Side side : {Side::Left, Side::Right, Side::Top, Side::Bottom}) {
        cut.clear();
        for (std::size_t index = 0; index < polygon.size() && !polygon.empty(); ++index) {
            const CutCorner& before = polygon[(index + polygon.size() - 1) % polygon.size()];
            const CutCorner& corner = polygon[index];
            const bool isCornerInside = isInside(corner, side);
            const bool isBeforeInside = isInside(before, side);
            if (isCornerInside && !isBeforeInside) {
                cut.push_back(crossing(before, corner, side));
            } else if (!isCornerInside && isBeforeInside) {
                cut.push_back(crossing(corner, before, side));
            }
            if (isCornerInside) {
                cut.push_back(corner);
            }
            if (cut.size() > kMostCorners) {
                cut.resize(kMostCorners);
            }
        }
        std::swap(polygon, cut);
    }
    return polygon;
}

[[nodiscard]] auto withoutOffset(Vec4 axis) noexcept -> Vec3 {
    return Vec3{.x = axis.x, .y = axis.y, .z = axis.z};
}

/// The share of `range` within 0 to 1.
[[nodiscard]] auto shareWithin(double from, double to) noexcept -> double {
    return std::min(to, 1.0) - std::max(from, 0.0);
}

} // namespace

auto decalImageOf(const assets::MipTexture& texture, const ChannelTable& textureGamma) -> assets::RgbaImage {
    const auto paletteByte = [&texture](std::size_t entry, std::size_t channel) {
        return std::to_integer<std::uint8_t>(texture.palette[(entry * kRgbBytes) + channel]);
    };
    const bool isMasked =
        std::ranges::all_of(std::views::iota(std::size_t{0}, kRgbBytes), [&](std::size_t channel) {
            return paletteByte(kClearIndex, channel) == kMaskedColor.at(channel);
        });
    assets::RgbaImage image{.width = texture.width, .height = texture.height};
    image.pixels.reserve(texture.levels.front().size() * kRgbaBytes);
    for (const std::byte index : texture.levels.front()) {
        const auto entry = std::to_integer<std::size_t>(index);
        // A tinted decal is its last color throughout, as opaque as each index; a masked one each
        // index's color; index 255 is clear in both.
        const std::size_t color = isMasked ? entry : kClearIndex;
        for (std::size_t channel = 0; channel < kRgbBytes; ++channel) {
            image.pixels.push_back(entry == kClearIndex && isMasked
                                       ? std::byte{0}
                                       : std::byte{textureGamma[paletteByte(color, channel)]});
        }
        image.pixels.push_back(entry == kClearIndex ? std::byte{0} : isMasked ? kOpaque : index);
    }
    return image;
}

auto decalLevelsOf(const assets::RgbaImage& image) -> DecalLevels {
    DecalLevels levels;
    levels.offsets.push_back(0);
    levels.bytes = image.pixels;
    std::uint32_t width = image.width;
    std::uint32_t height = image.height;
    while (width > 1 || height > 1) {
        const std::uint32_t nextWidth = std::max(width / 2, 1U);
        const std::uint32_t nextHeight = std::max(height / 2, 1U);
        const std::size_t from = levels.offsets.back();
        levels.offsets.push_back(levels.bytes.size());
        for (std::uint32_t y = 0; y < nextHeight; ++y) {
            for (std::uint32_t x = 0; x < nextWidth; ++x) {
                for (std::size_t channel = 0; channel < kRgbaBytes; ++channel) {
                    std::uint32_t sum = 0;
                    for (const std::uint32_t row :
                         {std::min(y * 2, height - 1), std::min((y * 2) + 1, height - 1)}) {
                        for (const std::uint32_t column :
                             {std::min(x * 2, width - 1), std::min((x * 2) + 1, width - 1)}) {
                            sum += std::to_integer<std::uint32_t>(
                                levels.bytes[from + ((((std::size_t{row} * width) + column) * kRgbaBytes) +
                                                     channel)]);
                        }
                    }
                    levels.bytes.push_back(static_cast<std::byte>((sum + kHalfTexels) / kAveragedTexels));
                }
            }
        }
        width = nextWidth;
        height = nextHeight;
    }
    return levels;
}

auto takesDecals(std::string_view name) noexcept -> bool {
    const SurfaceKind kind = surfaceKindOf(name);
    const std::string_view unmasked = name.starts_with('{') ? name.substr(1) : name;
    const bool isConveyor = unmasked.size() >= kConveyorPrefix.size() &&
                            equalsIgnoringCase(unmasked.substr(0, kConveyorPrefix.size()), kConveyorPrefix);
    return kind == SurfaceKind::Opaque && !isConveyor;
}

DecalPool::DecalPool(const assets::BspView& map, std::vector<DecalFace> faces, std::size_t limit)
    : m_map{&map}, m_faces{std::move(faces)}, m_slots(std::min(limit, kMostDecals)),
      m_decalsOfFace(m_faces.size()) {
    expects(m_faces.size() == map.faces().size(), "DecalPool: a DecalFace for each face of the map");
}

auto DecalPool::shoot(const DecalShot& shot) -> std::uint32_t {
    const std::span<const assets::BspModel> models = m_map->models();
    if (shot.model.value >= models.size()) {
        expects(false, "DecalPool::shoot: a model of the map");
        return 0;
    }
    if (m_slots.empty() || shot.size.width == 0 || shot.size.height == 0) {
        return 0;
    }
    Walk walked{.shot = shot, .reach = static_cast<float>(std::max(shot.size.width, shot.size.height) / 2)};
    walked.shot.scale = std::clamp(shot.scale, kLeastScale, kMostScale);
    std::uint32_t stamped = 0;
    walk(models[shot.model.value].headNode, walked, stamped);
    return stamped;
}

void DecalPool::walk(assets::NodeIndex index, const Walk& shot, std::uint32_t& stamped) {
    const assets::BspNode& node = m_map->nodes()[index.value];
    const Plane& plane = m_map->planes()[node.plane.value].plane;
    const float distance = dot(shot.shot.center, plane.normal) - plane.distance;
    const auto into = [&](std::size_t side) {
        if (const auto* const child = std::get_if<assets::NodeIndex>(&node.children.at(side))) {
            walk(*child, shot, stamped);
        }
    };
    if (distance > shot.reach) {
        into(0);
        return;
    }
    if (distance < -shot.reach) {
        into(1);
        return;
    }
    if (std::abs(distance) < kNodeReach) {
        for (std::uint32_t offset = 0; offset < node.faceCount; ++offset) {
            if (stamp(node.firstFace.value + offset, shot)) {
                ++stamped;
            }
        }
    }
    into(0);
    into(1);
}

auto DecalPool::stamp(std::uint32_t face, const Walk& walked) -> bool {
    const DecalShot& shot = walked.shot;
    const DecalFace& about = m_faces[face];
    if (!about.takesDecals) {
        return false;
    }
    const assets::BspFace& bspFace = m_map->faces()[face];
    const assets::TextureInfo& info = m_map->textureInfos()[bspFace.textureInfo.value];
    const Vec3 sAxis = normalized(withoutOffset(info.s));
    const Vec3 tAxis = normalized(withoutOffset(info.t));
    const auto width = static_cast<float>(shot.size.width);
    const auto height = static_cast<float>(shot.size.height);
    if (about.extent) {
        // The original's quick test, in the face's texels: whether the decal's box can reach the
        // face's luxels; its sizes in units cut to whole units.
        const auto unitsWide = static_cast<float>(static_cast<int>(width / shot.scale));
        const auto unitsHigh = static_cast<float>(static_cast<int>(height / shot.scale));
        const Vec3 s = withoutOffset(info.s);
        const Vec3 t = withoutOffset(info.t);
        const float boxWidth = std::abs(unitsWide * dot(s, sAxis)) + std::abs(unitsHigh * dot(s, tAxis));
        const float boxHeight = std::abs(unitsWide * dot(t, sAxis)) + std::abs(unitsHigh * dot(t, tAxis));
        const auto left = static_cast<float>(textureCoordinateOf(shot.center, info.s) - about.extent->minS) -
                          (boxWidth / 2.0F);
        const auto top = static_cast<float>(textureCoordinateOf(shot.center, info.t) - about.extent->minT) -
                         (boxHeight / 2.0F);
        if (left <= -boxWidth || top <= -boxHeight ||
            left > static_cast<float>(about.extent->spanS) + boxWidth ||
            top > static_cast<float>(about.extent->spanT) + boxHeight) {
            return false;
        }
    }
    const std::size_t slot = slotFor(face, shot, sAxis, tAxis);
    empty(slot);
    // The face's corners on the decal: a texel a unit at scale 1, its middle at the decal's center.
    const float acrossScale = shot.scale / width;
    const float downScale = shot.scale / height;
    std::vector<CutCorner> outline;
    for (std::uint32_t edge = 0; edge < bspFace.edgeCount; ++edge) {
        const Vec3 corner = cornerOf(*m_map, bspFace, edge);
        outline.push_back(CutCorner{.position = corner,
                                    .u = (acrossScale * dot(corner - shot.center, sAxis)) + kMiddle,
                                    .v = (downScale * dot(corner - shot.center, tAxis)) + kMiddle});
    }
    const std::vector<CutCorner> cut = cutToTheDecal(std::move(outline));
    if (cut.size() < kTriangleCorners) {
        return false; // its slot stays empty: its decal went all the same
    }
    PlacedDecal placed{.texture = shot.texture, .face = face, .model = shot.model};
    for (const CutCorner& corner : cut) {
        DecalVertex vertex{.position = corner.position, .u = corner.u, .v = corner.v};
        if (about.extent && about.lightmapCorner) {
            // As the face's own vertices are lit: luxel centers 16 texels apart from texturemins.
            vertex.lightmapX = static_cast<float>(
                ((textureCoordinateOf(corner.position, info.s) - about.extent->minS) / kLuxelUnits) +
                kLuxelCenter + (*about.lightmapCorner)[0]);
            vertex.lightmapY = static_cast<float>(
                ((textureCoordinateOf(corner.position, info.t) - about.extent->minT) / kLuxelUnits) +
                kLuxelCenter + (*about.lightmapCorner)[1]);
            vertex.styles = about.styles;
        }
        placed.polygon.push_back(vertex);
    }
    m_slots[slot] = Slot{.decal = std::move(placed),
                         .center = shot.center,
                         .scale = shot.scale,
                         .size = shot.size,
                         .sequence = m_sequence++};
    m_decalsOfFace[face].push_back(slot);
    ++m_revision;
    return true;
}

auto DecalPool::slotFor(std::uint32_t face, const DecalShot& shot, Vec3 sAxis, Vec3 tAxis) -> std::size_t {
    // How much of each decal of the face the new one covers, in that decal's own square.
    const double halfWide = static_cast<double>(shot.size.width) / double{shot.scale} / 2.0;
    const double halfHigh = static_cast<double>(shot.size.height) / double{shot.scale} / 2.0;
    std::size_t covered = 0;
    std::optional<std::size_t> leastCovered;
    double least = std::numeric_limits<double>::max();
    for (const std::size_t slot : m_decalsOfFace[face]) {
        const Slot& under = m_slots[slot];
        const double across = double{under.scale} / static_cast<double>(under.size.width);
        const double down = double{under.scale} / static_cast<double>(under.size.height);
        const double alongS = double{dot(shot.center - under.center, sAxis)};
        const double alongT = double{dot(shot.center - under.center, tAxis)};
        const double wide = shareWithin((across * (alongS - halfWide)) + double{kMiddle},
                                        (across * (alongS + halfWide)) + double{kMiddle});
        const double high = shareWithin((down * (alongT - halfHigh)) + double{kMiddle},
                                        (down * (alongT + halfHigh)) + double{kMiddle});
        if (wide <= 0.0 || high <= 0.0) {
            continue;
        }
        const double area = wide * high;
        if (area > kCoveredShare) {
            ++covered;
            if (area <= least) {
                least = area;
                leastCovered = slot;
            }
        }
    }
    if (covered >= kMostCovered && leastCovered) {
        return *leastCovered;
    }
    const std::size_t next = m_next;
    m_next = (m_next + 1) % m_slots.size();
    return next;
}

void DecalPool::empty(std::size_t slot) {
    std::optional<PlacedDecal>& decal = m_slots[slot].decal;
    if (!decal) {
        return;
    }
    std::erase(m_decalsOfFace[decal->face], slot);
    decal.reset();
    ++m_revision;
}

auto DecalPool::inOrder() const -> std::vector<const PlacedDecal*> {
    std::vector<const Slot*> used;
    for (const Slot& slot : m_slots) {
        if (slot.decal) {
            used.push_back(&slot);
        }
    }
    std::ranges::sort(used, {}, &Slot::sequence);
    return used | std::views::transform([](const Slot* slot) { return &*slot->decal; }) |
           std::ranges::to<std::vector>();
}

auto DecalPool::decalsOn(std::uint32_t face) const noexcept -> std::size_t {
    return face < m_decalsOfFace.size() ? m_decalsOfFace[face].size() : 0;
}

} // namespace nx::render
