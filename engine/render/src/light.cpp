// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.render;

import std;
import nx.core;
import nx.math;
import nx.assets;

namespace nx::render {

namespace {

/// The bytes of a lightmap sample: red, green and blue.
constexpr std::size_t kSampleBytes = 3;
/// The bits a summed light drops: a sample times its style's brightness, 264 at the normal style, is
/// near the sample again once shifted.
constexpr std::uint32_t kStyleShift = 8;
constexpr std::uint32_t kBrightestChannel = 255;
/// A luxel's index, from its distance in luxels: the nearest, halves away from the first (Q_rint).
constexpr float kHalfLuxel = 0.5F;

/// Where a model's light is looked for: from this far above its origin, down this far; against the
/// sky's way this far; and at the four corners of a square about its origin, the first this far from
/// it on each axis, each next one a side of the square away.
constexpr float kAboveOrigin = 8.0F;
constexpr float kFloorReach = 2048.0F;
constexpr float kSkyReach = 8192.0F;
constexpr float kGradientStep = 16.0F;
constexpr float kGradientSide = 32.0F;
/// The sum of a light's three channels at their brightest, and three more bits: a gradient point's
/// share of light.
constexpr float kGradientScale = 768.0F;
/// The light a model's ambient and its ambient and shade together hold to.
constexpr float kMostAmbient = 128.0F;
constexpr float kMostLight = 255.0F;

/// The world's root: none in a map without models.
[[nodiscard]] auto worldRoot(const assets::BspView& map) -> std::optional<assets::NodeChild> {
    if (map.models().empty()) {
        return std::nullopt;
    }
    return assets::NodeChild{map.models()[assets::kWorldModel.value].headNode};
}

/// Walks the segment from `start` to `end` down the tree of `map`'s world, the nearer side of each
/// node first, as the original's R_RecursiveLightPoint and SurfaceAtPoint walk it, and gives `meets`
/// each face of every node it crosses, with the point where it crosses the node's plane, until
/// `meets` finds something: what the walk returns. The way ahead is kept on a stack of its own, not in
/// recursion, however deep a file's tree.
template <class Found, class Meets>
[[nodiscard]] auto firstAlong(const assets::BspView& map, Vec3 start, Vec3 end, const Meets& meets)
    -> std::optional<Found> {
    /// A side of a node to walk from `start` to `end`.
    struct Side {
        assets::NodeChild child;
        Vec3 start;
        Vec3 end;
    };
    /// A node's faces, met where the segment crosses its plane.
    struct Crossing {
        assets::NodeIndex node;
        Vec3 point;
    };
    const std::optional<assets::NodeChild> root = worldRoot(map);
    if (!root) {
        return std::nullopt;
    }
    std::vector<std::variant<Side, Crossing>> ahead{Side{.child = *root, .start = start, .end = end}};
    while (!ahead.empty()) {
        const std::variant<Side, Crossing> step = ahead.back();
        ahead.pop_back();
        if (const auto* crossing = std::get_if<Crossing>(&step)) {
            const assets::BspNode& node = map.nodes()[crossing->node.value];
            for (std::uint32_t offset = 0; offset < node.faceCount; ++offset) {
                if (std::optional<Found> found =
                        meets(assets::FaceIndex{node.firstFace.value + offset}, crossing->point)) {
                    return found;
                }
            }
            continue;
        }
        const Side& side = std::get<Side>(step);
        const auto* index = std::get_if<assets::NodeIndex>(&side.child);
        if (index == nullptr) {
            continue; // a leaf: nothing is met in it
        }
        const assets::BspNode& node = map.nodes()[index->value];
        const Plane& plane = map.planes()[node.plane.value].plane;
        const float front = plane.signedDistance(side.start);
        const float back = plane.signedDistance(side.end);
        const std::size_t nearSide = front < 0.0F ? 1 : 0;
        if ((back < 0.0F) == (nearSide == 1)) {
            ahead.push_back(Side{.child = node.children[nearSide], .start = side.start, .end = side.end});
            continue;
        }
        const Vec3 point = lerp(side.start, side.end, front / (front - back));
        // Taken in reverse: the near side, then the node's faces, then the far side.
        ahead.push_back(Side{.child = node.children[nearSide ^ 1], .start = point, .end = side.end});
        ahead.push_back(Crossing{.node = *index, .point = point});
        ahead.push_back(Side{.child = node.children[nearSide], .start = side.start, .end = point});
    }
    return std::nullopt;
}

/// The coordinate of `point` along a texture's axis `axis`, as the original computes it, in single
/// precision.
[[nodiscard]] auto coordinateOf(Vec3 point, Vec4 axis) noexcept -> float {
    return dot(point, xyz(axis)) + axis.w;
}

/// The brightness of `style` in `styles`: the normal one above the styles the game sets.
[[nodiscard]] auto brightnessOf(const LightStyleValues& styles, std::uint8_t style) noexcept
    -> std::uint32_t {
    return style < styles.size() ? styles[style] : kNormalLightStyleValue;
}

/// The sum of `light`'s channels.
[[nodiscard]] auto sumOf(LightColor light) noexcept -> std::uint32_t {
    return std::uint32_t{light.red} + std::uint32_t{light.green} + std::uint32_t{light.blue};
}

} // namespace

WorldLight::WorldLight(const assets::BspView& map) : m_map{&map} {
    m_faces.reserve(map.faces().size());
    for (const assets::BspFace& face : map.faces()) {
        const assets::TextureInfo& info = map.textureInfos()[face.textureInfo.value];
        const std::optional<assets::MipTexture>& texture = map.textures()[info.texture.value];
        FaceLight light{.s = info.s,
                        .t = info.t,
                        .extent = surfaceExtentOf(map, face),
                        .isSpecial = info.isSpecial,
                        .isSky = texture && surfaceKindOf(texture->name) == SurfaceKind::Sky};
        if (light.extent && face.lightmapOffset) {
            const std::uint64_t bytes = std::uint64_t{light.extent->width} * light.extent->height *
                                        kSampleBytes * lightmapCountOf(face);
            light.hasSamples = *face.lightmapOffset + bytes <= map.lighting().size();
        }
        m_faces.push_back(light);
    }
}

auto WorldLight::lightAlong(Vec3 start, Vec3 end, const LightStyleValues& styles) const -> LightColor {
    if (m_map->lighting().empty()) {
        return LightColor{.red = kBrightestChannel, .green = kBrightestChannel, .blue = kBrightestChannel};
    }
    const auto meets = [&](assets::FaceIndex index, Vec3 point) -> std::optional<LightColor> {
        const FaceLight& face = m_faces[index.value];
        if (face.isSpecial || !face.extent) {
            return std::nullopt; // no lightmap: the light passes it
        }
        const SurfaceExtent& extent = *face.extent;
        const float s = coordinateOf(point, face.s);
        const float t = coordinateOf(point, face.t);
        const auto minS = static_cast<float>(extent.minS);
        const auto minT = static_cast<float>(extent.minT);
        if (s < minS || t < minT) {
            return std::nullopt;
        }
        const float ds = s - minS;
        const float dt = t - minT;
        const auto luxelUnits = static_cast<float>(kLuxelUnits);
        if (ds > static_cast<float>(extent.width - 1) * luxelUnits ||
            dt > static_cast<float>(extent.height - 1) * luxelUnits) {
            return std::nullopt;
        }
        if (!face.hasSamples) {
            return LightColor{}; // met, and black
        }
        const auto column = static_cast<std::size_t>((ds / luxelUnits) + kHalfLuxel);
        const auto row = static_cast<std::size_t>((dt / luxelUnits) + kHalfLuxel);
        const assets::BspFace& record = m_map->faces()[index.value];
        const std::size_t mapBytes = std::size_t{extent.width} * extent.height * kSampleBytes;
        const std::uint32_t lightmaps = lightmapCountOf(record);
        std::size_t at = *record.lightmapOffset + (((row * extent.width) + column) * kSampleBytes);
        std::array<std::uint32_t, kSampleBytes> summed{};
        for (std::uint32_t lightmap = 0; lightmap < lightmaps; ++lightmap, at += mapBytes) {
            const std::uint32_t brightness = brightnessOf(styles, record.lightStyles[lightmap]);
            for (std::size_t channel = 0; channel < kSampleBytes; ++channel) {
                summed[channel] +=
                    std::to_integer<std::uint32_t>(m_map->lighting()[at + channel]) * brightness;
            }
        }
        const auto channelOf = [&summed](std::size_t channel) {
            return static_cast<std::uint8_t>(std::min(summed[channel] >> kStyleShift, kBrightestChannel));
        };
        return LightColor{.red = channelOf(0), .green = channelOf(1), .blue = channelOf(2)};
    };
    return firstAlong<LightColor>(*m_map, start, end, meets).value_or(LightColor{});
}

auto WorldLight::meetsSky(Vec3 start, Vec3 end) const -> bool {
    const auto meets = [this](assets::FaceIndex index, Vec3 point) -> std::optional<bool> {
        const FaceLight& face = m_faces[index.value];
        if (!face.extent) {
            return std::nullopt;
        }
        // The original truncates the coordinates to whole texels first.
        const float ds = std::trunc(coordinateOf(point, face.s)) - static_cast<float>(face.extent->minS);
        const float dt = std::trunc(coordinateOf(point, face.t)) - static_cast<float>(face.extent->minT);
        const auto luxelUnits = static_cast<float>(kLuxelUnits);
        if (ds < 0.0F || dt < 0.0F || ds > static_cast<float>(face.extent->width - 1) * luxelUnits ||
            dt > static_cast<float>(face.extent->height - 1) * luxelUnits) {
            return std::nullopt;
        }
        return face.isSky;
    };
    return firstAlong<bool>(*m_map, start, end, meets).value_or(false);
}

auto WorldLight::modelLightAt(const ModelLightQuery& query, const LightStyleValues& styles) const
    -> ModelLight {
    Vec3 direction{.z = -1.0F};
    const Vec3 source = query.origin - (direction * kAboveOrigin);
    Vec3 light{};
    const Vec3& sky = query.sky.color;
    if (sky.x + sky.y + sky.z != 0.0F && meetsSky(source, query.origin - (query.sky.direction * kSkyReach))) {
        direction = query.sky.direction;
        light = Vec3{.x = std::trunc(sky.x), .y = std::trunc(sky.y), .z = std::trunc(sky.z)}; // its colorVec
    }
    if (light.x + light.y + light.z == 0.0F) {
        const Vec3 end = source + (direction * kFloorReach);
        const LightColor floor = lightAlong(source, end, styles);
        light = Vec3{.x = static_cast<float>(floor.red),
                     .y = static_cast<float>(floor.green),
                     .z = static_cast<float>(floor.blue)};
        // Where the light grows dimmer, from four points about the origin, each moved from the last as
        // the original moves it: its gradient.
        Vec3 from = source;
        Vec3 to = end;
        const auto shareAfter = [&](float x, float y) {
            from += Vec3{.x = x, .y = y};
            to += Vec3{.x = x, .y = y};
            return static_cast<float>(sumOf(lightAlong(from, to, styles))) / kGradientScale;
        };
        const float backLeft = shareAfter(-kGradientStep, -kGradientStep);
        const float backRight = shareAfter(kGradientSide, 0.0F);
        const float frontRight = shareAfter(0.0F, kGradientSide);
        const float frontLeft = shareAfter(-kGradientSide, 0.0F);
        direction.x = backLeft - backRight - frontRight + frontLeft;
        direction.y = backRight + backLeft - frontRight - frontLeft;
        direction = normalized(direction);
    }
    const float brightest = std::max({light.x, light.y, light.z});
    const float total = brightest == 0.0F ? 1.0F : brightest;
    direction = direction * total * std::clamp(query.direct, kLeastDirectLight, 1.0F);
    ModelLight result{.shade = length(direction), .direction = normalized(direction)};
    result.ambient = std::min(total - result.shade, kMostAmbient);
    if (result.ambient + result.shade > kMostLight) {
        result.shade = kMostLight - result.ambient;
    }
    if (brightest > 0.0F) {
        result.color = light * (1.0F / brightest);
    }
    return result;
}

} // namespace nx::render
