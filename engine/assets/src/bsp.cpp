// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;
import nx.math;

namespace nx::assets {

namespace {

/// The lumps of a BSP v30 file, in the order of its header.
enum class Lump : std::uint8_t {
    Entities,
    Planes,
    Textures,
    Vertices,
    Visibility,
    Nodes,
    TextureInfos,
    Faces,
    Lighting,
    ClipNodes,
    Leaves,
    MarkSurfaces,
    Edges,
    SurfaceEdges,
    Models,
    Count,
};

using Lumps = EnumArray<Lump, std::span<const std::byte>>;

// The header: the version, then an offset and a length for every lump.
constexpr std::size_t kLumpTableOffset = 4;
constexpr std::size_t kLumpEntryBytes = 8;
constexpr std::size_t kHeaderBytes = kLumpTableOffset + (enumCount<Lump>() * kLumpEntryBytes);

// The records: a plane is its normal, its distance and its type; a vertex three floats; an edge two
// 16-bit vertex indices; a surface edge a signed 32-bit edge index, negative when reversed; a
// texture info two vectors of four floats, a texture index and flags.
constexpr std::size_t kPlaneBytes = 20;
constexpr std::size_t kPlaneDistanceOffset = 12;
constexpr std::size_t kPlaneTypeOffset = 16;
constexpr std::size_t kVertexBytes = 12;
constexpr std::size_t kEdgeBytes = 4;
constexpr std::size_t kSurfaceEdgeBytes = 4;
constexpr std::size_t kTextureInfoBytes = 40;
constexpr std::size_t kTextureInfoTOffset = 16;
constexpr std::size_t kTextureInfoTextureOffset = 32;
constexpr std::size_t kTextureInfoFlagsOffset = 36;
constexpr std::uint32_t kSpecialTextureFlag = 1;

// A face: plane (16 bits), side (16 bits), first surface edge (32), edge count (16), texture info
// (16), four light styles, and the lightmap offset (32, -1 when unlit).
constexpr std::size_t kFaceBytes = 20;
constexpr std::size_t kFaceSideOffset = 2;
constexpr std::size_t kFaceFirstEdgeOffset = 4;
constexpr std::size_t kFaceEdgeCountOffset = 8;
constexpr std::size_t kFaceTextureInfoOffset = 10;
constexpr std::size_t kFaceLightStylesOffset = 12;
constexpr std::size_t kFaceLightmapOffset = 16;
constexpr std::uint32_t kMinimumFaceEdges = 3;
constexpr std::int32_t kNoLightmap = -1;

// The textures lump: a count, an offset per texture from the lump's start (-1 for an empty slot),
// then the mip textures.
constexpr std::int32_t kEmptyTextureSlot = -1;

/// How far from 1 the length of a stored normal may be: compilers write unit normals in floats.
constexpr float kUnitLengthTolerance = 1.0e-3F;

constexpr std::size_t kRgbBytes = 3;

[[nodiscard]] auto readVec3(std::span<const std::byte> bytes) noexcept -> Vec3 {
    return Vec3{.x = loadLittleEndian<float>(bytes),
                .y = loadLittleEndian<float>(bytes.subspan(sizeof(float))),
                .z = loadLittleEndian<float>(bytes.subspan(2 * sizeof(float)))};
}

[[nodiscard]] auto readVec4(std::span<const std::byte> bytes) noexcept -> Vec4 {
    const Vec3 xyz = readVec3(bytes);
    return Vec4{
        .x = xyz.x, .y = xyz.y, .z = xyz.z, .w = loadLittleEndian<float>(bytes.subspan(3 * sizeof(float)))};
}

[[nodiscard]] auto isFinite(Vec3 vector) noexcept -> bool {
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

[[nodiscard]] auto isFinite(Vec4 vector) noexcept -> bool {
    return isFinite(Vec3{.x = vector.x, .y = vector.y, .z = vector.z}) && std::isfinite(vector.w);
}

/// "<lump> <record>": where a failure is.
[[nodiscard]] auto where(Lump lump, std::size_t record) -> std::string {
    return std::format("{} {}", enumName(lump), record);
}

/// `value` as an index into an array of `count`. Errors: BadIndex.
template <class Tag>
[[nodiscard]] auto indexInto(std::uint64_t value, std::size_t count, Lump lump, std::size_t record,
                             std::string_view field) -> Result<BspIndex<Tag>, BspError> {
    if (value >= count) {
        return fail(BspError::BadIndex,
                    std::format("{}: {} {} is not below {}", where(lump, record), field, value, count));
    }
    return BspIndex<Tag>{.value = static_cast<std::uint32_t>(value)};
}

/// Every lump of `bytes`. Errors: TooSmall, UnsupportedVersion, LumpOutOfBounds.
[[nodiscard]] auto lumpsOf(std::span<const std::byte> bytes) -> Result<Lumps, BspError> {
    if (bytes.size() < kHeaderBytes) {
        return fail(BspError::TooSmall,
                    std::format("{} bytes, less than the {} of the header", bytes.size(), kHeaderBytes));
    }
    if (const auto version = loadLittleEndian<std::int32_t>(bytes); version != kBspVersion) {
        return fail(BspError::UnsupportedVersion, std::format("version {}, not {}", version, kBspVersion));
    }
    Lumps lumps;
    for (const Lump lump : Lumps::keys()) {
        const auto entry = bytes.subspan(kLumpTableOffset + (std::to_underlying(lump) * kLumpEntryBytes));
        const std::uint64_t offset = loadLittleEndian<std::uint32_t>(entry);
        const std::uint64_t length = loadLittleEndian<std::uint32_t>(entry.subspan(sizeof(std::uint32_t)));
        if (offset > bytes.size() || length > bytes.size() - offset) {
            return fail(BspError::LumpOutOfBounds, std::format("{}: {} bytes at {} are outside the file's {}",
                                                               enumName(lump), length, offset, bytes.size()));
        }
        lumps[lump] = bytes.subspan(narrow<std::size_t>(offset), narrow<std::size_t>(length));
    }
    return lumps;
}

/// Decodes every record of `lump`, `recordBytes` each, with `decode(record, index)`. Errors:
/// MisalignedLump, and those of `decode`.
template <class Decode>
[[nodiscard]] auto
decodeRecords(const Lumps& lumps, Lump lump, std::size_t recordBytes, Decode decode) -> Result<
    std::vector<typename std::invoke_result_t<Decode, std::span<const std::byte>, std::size_t>::value_type>,
    BspError> {
    using Record = typename std::invoke_result_t<Decode, std::span<const std::byte>, std::size_t>::value_type;
    const std::span<const std::byte> data = lumps[lump];
    if (data.size() % recordBytes != 0) {
        return fail(BspError::MisalignedLump, std::format("{}: {} bytes are not whole records of {}",
                                                          enumName(lump), data.size(), recordBytes));
    }
    std::vector<Record> records;
    records.reserve(data.size() / recordBytes);
    for (std::size_t index = 0; index < data.size() / recordBytes; ++index) {
        auto record = decode(data.subspan(index * recordBytes, recordBytes), index);
        if (!record) {
            return std::unexpected(std::move(record.error()));
        }
        records.push_back(std::move(*record));
    }
    return records;
}

[[nodiscard]] auto decodePlane(std::span<const std::byte> record, std::size_t index)
    -> Result<BspPlane, BspError> {
    const Vec3 normal = readVec3(record);
    const auto distance = loadLittleEndian<float>(record.subspan(kPlaneDistanceOffset));
    const auto axis = loadLittleEndian<std::uint32_t>(record.subspan(kPlaneTypeOffset));
    if (!isFinite(normal) || !std::isfinite(distance)) {
        return fail(BspError::BadValue, std::format("{}: normal {} and distance {} must be finite",
                                                    where(Lump::Planes, index), normal, distance));
    }
    if (std::abs(length(normal) - 1.0F) > kUnitLengthTolerance) {
        return fail(BspError::BadValue,
                    std::format("{}: the normal {} is not unit length", where(Lump::Planes, index), normal));
    }
    if (axis >= enumCount<PlaneAxis>()) {
        return fail(BspError::BadValue, std::format("{}: unknown type {}", where(Lump::Planes, index), axis));
    }
    return BspPlane{.plane = Plane{.normal = normal, .distance = distance},
                    .axis = static_cast<PlaneAxis>(axis)};
}

[[nodiscard]] auto decodeVertex(std::span<const std::byte> record, std::size_t index)
    -> Result<Vec3, BspError> {
    const Vec3 vertex = readVec3(record);
    if (!isFinite(vertex)) {
        return fail(BspError::BadValue,
                    std::format("{}: {} must be finite", where(Lump::Vertices, index), vertex));
    }
    return vertex;
}

[[nodiscard]] auto decodeEdge(std::span<const std::byte> record, std::size_t index, std::size_t vertices)
    -> Result<BspEdge, BspError> {
    const auto first = indexInto<VertexTag>(loadLittleEndian<std::uint16_t>(record), vertices, Lump::Edges,
                                            index, "first vertex");
    const auto second =
        indexInto<VertexTag>(loadLittleEndian<std::uint16_t>(record.subspan(sizeof(std::uint16_t))), vertices,
                             Lump::Edges, index, "second vertex");
    if (!first || !second) {
        return std::unexpected(!first ? first.error() : second.error());
    }
    return BspEdge{.vertices = {*first, *second}};
}

[[nodiscard]] auto decodeSurfaceEdge(std::span<const std::byte> record, std::size_t index, std::size_t edges)
    -> Result<SurfaceEdge, BspError> {
    const auto value = loadLittleEndian<std::int32_t>(record);
    // The magnitude is the edge; the most negative value has none.
    const std::uint64_t magnitude =
        value == std::numeric_limits<std::int32_t>::min()
            ? std::numeric_limits<std::uint64_t>::max()
            : static_cast<std::uint64_t>(value < 0 ? -std::int64_t{value} : value);
    return indexInto<EdgeTag>(magnitude, edges, Lump::SurfaceEdges, index, "edge")
        .transform([value](EdgeIndex edge) { return SurfaceEdge{.edge = edge, .isReversed = value < 0}; });
}

/// The textures lump: nullopt for an empty slot. Errors: LumpOutOfBounds, BadTexture.
[[nodiscard]] auto decodeTextures(std::span<const std::byte> data)
    -> Result<std::vector<std::optional<MipTexture>>, BspError> {
    if (data.empty()) {
        return std::vector<std::optional<MipTexture>>{};
    }
    if (data.size() < sizeof(std::uint32_t)) {
        return fail(BspError::LumpOutOfBounds,
                    std::format("Textures: {} bytes cannot hold the count", data.size()));
    }
    const std::uint64_t count = loadLittleEndian<std::uint32_t>(data);
    if ((count + 1) * sizeof(std::int32_t) > data.size()) {
        return fail(BspError::LumpOutOfBounds,
                    std::format("Textures: {} offsets do not fit in {} bytes", count, data.size()));
    }
    std::vector<std::optional<MipTexture>> textures;
    textures.reserve(narrow<std::size_t>(count));
    for (std::size_t index = 0; index < count; ++index) {
        const auto offset = loadLittleEndian<std::int32_t>(data.subspan((index + 1) * sizeof(std::int32_t)));
        if (offset == kEmptyTextureSlot) {
            textures.emplace_back(std::nullopt);
            continue;
        }
        if (offset < 0 || std::cmp_greater_equal(offset, data.size())) {
            return fail(BspError::LumpOutOfBounds,
                        std::format("{}: offset {} is outside the lump's {} bytes",
                                    where(Lump::Textures, index), offset, data.size()));
        }
        auto texture =
            parseMipTexture(data.subspan(static_cast<std::size_t>(offset)), TexturePixels::MayBeInWad);
        if (!texture) {
            return fail(BspError::BadTexture, where(Lump::Textures, index), std::move(texture.error()));
        }
        textures.emplace_back(std::move(*texture));
    }
    return textures;
}

[[nodiscard]] auto decodeTextureInfo(std::span<const std::byte> record, std::size_t index,
                                     std::size_t textures) -> Result<TextureInfo, BspError> {
    const Vec4 s = readVec4(record);
    const Vec4 t = readVec4(record.subspan(kTextureInfoTOffset));
    if (!isFinite(s) || !isFinite(t)) {
        return fail(BspError::BadValue,
                    std::format("{}: the texture vectors must be finite", where(Lump::TextureInfos, index)));
    }
    const auto texture =
        indexInto<TextureTag>(loadLittleEndian<std::uint32_t>(record.subspan(kTextureInfoTextureOffset)),
                              textures, Lump::TextureInfos, index, "texture");
    if (!texture) {
        return std::unexpected(texture.error());
    }
    const auto flags = loadLittleEndian<std::uint32_t>(record.subspan(kTextureInfoFlagsOffset));
    return TextureInfo{.s = s, .t = t, .texture = *texture, .isSpecial = (flags & kSpecialTextureFlag) != 0};
}

/// What the faces index: the sizes of the other arrays.
struct FaceTargets {
    std::size_t planes = 0;
    std::size_t surfaceEdges = 0;
    std::size_t textureInfos = 0;
    std::size_t lightingBytes = 0;
};

[[nodiscard]] auto decodeFace(std::span<const std::byte> record, std::size_t index,
                              const FaceTargets& targets) -> Result<BspFace, BspError> {
    const auto plane = indexInto<PlaneTag>(loadLittleEndian<std::uint16_t>(record), targets.planes,
                                           Lump::Faces, index, "plane");
    const auto textureInfo =
        indexInto<TextureInfoTag>(loadLittleEndian<std::uint16_t>(record.subspan(kFaceTextureInfoOffset)),
                                  targets.textureInfos, Lump::Faces, index, "texture info");
    if (!plane || !textureInfo) {
        return std::unexpected(!plane ? plane.error() : textureInfo.error());
    }
    const std::uint64_t firstEdge = loadLittleEndian<std::uint32_t>(record.subspan(kFaceFirstEdgeOffset));
    const std::uint32_t edgeCount = loadLittleEndian<std::uint16_t>(record.subspan(kFaceEdgeCountOffset));
    if (edgeCount < kMinimumFaceEdges) {
        return fail(BspError::BadValue, std::format("{}: {} edges, fewer than {}", where(Lump::Faces, index),
                                                    edgeCount, kMinimumFaceEdges));
    }
    if (firstEdge + edgeCount > targets.surfaceEdges) {
        return fail(BspError::BadIndex,
                    std::format("{}: edges {} to {} are not below {}", where(Lump::Faces, index), firstEdge,
                                firstEdge + edgeCount, targets.surfaceEdges));
    }
    BspFace face{
        .plane = *plane,
        .isBackSide = loadLittleEndian<std::int16_t>(record.subspan(kFaceSideOffset)) != 0,
        .firstEdge = SurfaceEdgeIndex{.value = static_cast<std::uint32_t>(firstEdge)},
        .edgeCount = edgeCount,
        .textureInfo = *textureInfo,
        .lightStyles = {},
        .lightmapOffset = std::nullopt,
    };
    for (std::size_t style = 0; style < kFaceLightStyles; ++style) {
        face.lightStyles.at(style) = std::to_integer<std::uint8_t>(record[kFaceLightStylesOffset + style]);
    }
    const auto lightmap = loadLittleEndian<std::int32_t>(record.subspan(kFaceLightmapOffset));
    if (lightmap != kNoLightmap) {
        if (lightmap < 0 || std::cmp_greater_equal(lightmap, targets.lightingBytes)) {
            return fail(BspError::BadIndex,
                        std::format("{}: lightmap offset {} is not below {}", where(Lump::Faces, index),
                                    lightmap, targets.lightingBytes));
        }
        face.lightmapOffset = static_cast<std::uint32_t>(lightmap);
    }
    return face;
}

/// Moves what `decoded` holds into `destination`; its failure, when it holds one.
template <class Records>
[[nodiscard]] auto store(Result<Records, BspError>&& decoded, Records& destination)
    -> std::optional<Failure<BspError>> {
    if (!decoded) {
        return std::move(decoded.error());
    }
    destination = std::move(*decoded);
    return std::nullopt;
}

} // namespace

auto BspView::parse(std::span<const std::byte> bytes) -> Result<BspView, BspError> {
    const auto lumps = lumpsOf(bytes);
    if (!lumps) {
        return std::unexpected(lumps.error());
    }
    BspView view;
    if (auto failure = store(decodeRecords(*lumps, Lump::Planes, kPlaneBytes, decodePlane), view.m_planes)) {
        return std::unexpected(std::move(*failure));
    }
    if (auto failure =
            store(decodeRecords(*lumps, Lump::Vertices, kVertexBytes, decodeVertex), view.m_vertices)) {
        return std::unexpected(std::move(*failure));
    }
    const auto decodeEdgeOf = [vertices = view.m_vertices.size()](std::span<const std::byte> record,
                                                                  std::size_t index) {
        return decodeEdge(record, index, vertices);
    };
    if (auto failure = store(decodeRecords(*lumps, Lump::Edges, kEdgeBytes, decodeEdgeOf), view.m_edges)) {
        return std::unexpected(std::move(*failure));
    }
    const auto decodeSurfaceEdgeOf = [edges = view.m_edges.size()](std::span<const std::byte> record,
                                                                   std::size_t index) {
        return decodeSurfaceEdge(record, index, edges);
    };
    if (auto failure =
            store(decodeRecords(*lumps, Lump::SurfaceEdges, kSurfaceEdgeBytes, decodeSurfaceEdgeOf),
                  view.m_surfaceEdges)) {
        return std::unexpected(std::move(*failure));
    }
    if (auto failure = store(decodeTextures((*lumps)[Lump::Textures]), view.m_textures)) {
        return std::unexpected(std::move(*failure));
    }
    const auto decodeTextureInfoOf = [textures = view.m_textures.size()](std::span<const std::byte> record,
                                                                         std::size_t index) {
        return decodeTextureInfo(record, index, textures);
    };
    if (auto failure =
            store(decodeRecords(*lumps, Lump::TextureInfos, kTextureInfoBytes, decodeTextureInfoOf),
                  view.m_textureInfos)) {
        return std::unexpected(std::move(*failure));
    }
    view.m_lighting = (*lumps)[Lump::Lighting];
    if (view.m_lighting.size() % kRgbBytes != 0) {
        return fail(BspError::MisalignedLump,
                    std::format("Lighting: {} bytes are not whole RGB samples", view.m_lighting.size()));
    }
    const auto decodeFaceOf = [targets = FaceTargets{.planes = view.m_planes.size(),
                                                     .surfaceEdges = view.m_surfaceEdges.size(),
                                                     .textureInfos = view.m_textureInfos.size(),
                                                     .lightingBytes = view.m_lighting.size()}](
                                  std::span<const std::byte> record, std::size_t index) {
        return decodeFace(record, index, targets);
    };
    if (auto failure = store(decodeRecords(*lumps, Lump::Faces, kFaceBytes, decodeFaceOf), view.m_faces)) {
        return std::unexpected(std::move(*failure));
    }
    return view;
}

} // namespace nx::assets
