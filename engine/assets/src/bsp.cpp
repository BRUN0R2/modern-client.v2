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

// A node: plane (32 bits), two children (16 bits each: a later node, or ~leaf), bounds (six 16-bit
// integers), first face and face count (16 bits each).
constexpr std::size_t kNodeBytes = 24;
constexpr std::size_t kNodeChildrenOffset = 4;
constexpr std::size_t kNodeBoundsOffset = 8;
constexpr std::size_t kNodeFirstFaceOffset = 20;
constexpr std::size_t kNodeFaceCountOffset = 22;

// A leaf: contents and visibility offset (32 bits each, -1 for none), bounds (six 16-bit integers),
// first mark surface and count (16 bits each), and the ambient sound levels.
constexpr std::size_t kLeafBytes = 28;
constexpr std::size_t kLeafVisibilityOffset = 4;
constexpr std::size_t kLeafBoundsOffset = 8;
constexpr std::size_t kLeafFirstMarkSurfaceOffset = 20;
constexpr std::size_t kLeafMarkSurfaceCountOffset = 22;
constexpr std::size_t kLeafAmbientOffset = 24;
constexpr std::int32_t kNoVisibility = -1;

// A clip node: plane (32 bits) and two children (16 bits each: a later clip node, or contents).
constexpr std::size_t kClipNodeBytes = 8;
constexpr std::size_t kClipNodeChildrenOffset = 4;

constexpr std::size_t kMarkSurfaceBytes = 2;

// A model: bounds and origin (nine floats), the roots of its node tree and of its three hulls, the
// leaves that have visibility, its first face and face count (32 bits each).
constexpr std::size_t kModelBytes = 64;
constexpr std::size_t kModelMaxOffset = 12;
constexpr std::size_t kModelOriginOffset = 24;
constexpr std::size_t kModelHeadsOffset = 36;
constexpr std::size_t kModelVisibleLeavesOffset = 52;
constexpr std::size_t kModelFirstFaceOffset = 56;
constexpr std::size_t kModelFaceCountOffset = 60;

/// The bytes of a child, and of a 16-bit bound.
constexpr std::size_t kChildBytes = sizeof(std::int16_t);
constexpr std::size_t kAxes = 3;

/// How far from 1 the length of a stored normal may be: compilers write unit normals in floats.
constexpr float kUnitLengthTolerance = 1.0e-3F;

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

/// The box of six 16-bit integers: the minimum, then the maximum.
[[nodiscard]] auto readShortBounds(std::span<const std::byte> bytes) noexcept -> Aabb {
    const auto axis = [bytes](std::size_t index) {
        return static_cast<float>(
            loadLittleEndian<std::int16_t>(bytes.subspan(index * sizeof(std::int16_t))));
    };
    return Aabb{.min = {.x = axis(0), .y = axis(1), .z = axis(2)},
                .max = {.x = axis(kAxes), .y = axis(kAxes + 1), .z = axis(kAxes + 2)}};
}

/// `value` as the contents of a leaf or of a side of a clip node. Errors: BadValue.
[[nodiscard]] auto contentsOf(std::int32_t value, Lump lump, std::size_t record)
    -> Result<Contents, BspError> {
    const std::optional<Contents> contents = contentsNumbered(value);
    if (!contents || *contents == Contents::Ladder) {
        return fail(BspError::BadValue,
                    std::format("{}: contents {}, not a leaf's", where(lump, record), value));
    }
    return *contents;
}

/// `count` records from `first` lie within `available`. Errors: BadIndex.
[[nodiscard]] auto checkRange(std::uint64_t first, std::uint64_t count, std::size_t available, Lump lump,
                              std::size_t record, std::string_view what) -> Result<void, BspError> {
    if (first + count > available) {
        return fail(BspError::BadIndex, std::format("{}: {} {} to {} are not below {}", where(lump, record),
                                                    what, first, first + count, available));
    }
    return {};
}

/// What the tree records index: the sizes of the arrays.
struct TreeTargets {
    std::size_t planes = 0;
    std::size_t faces = 0;
    std::size_t nodes = 0;
    std::size_t leaves = 0;
    std::size_t clipNodes = 0;
    std::size_t markSurfaces = 0;
    std::size_t visibilityBytes = 0;
};

/// A child of node `parent`: a node after it, or the leaf ~child. Errors: BadIndex, BadValue.
[[nodiscard]] auto decodeNodeChild(std::int16_t child, std::size_t parent, const TreeTargets& targets)
    -> Result<NodeChild, BspError> {
    if (child < 0) {
        return indexInto<LeafTag>(static_cast<std::uint64_t>(-1 - std::int32_t{child}), targets.leaves,
                                  Lump::Nodes, parent, "leaf")
            .transform([](LeafIndex leaf) { return NodeChild{leaf}; });
    }
    if (std::cmp_less_equal(child, parent)) {
        return fail(BspError::BadValue, std::format("{}: child node {} is not after its parent",
                                                    where(Lump::Nodes, parent), child));
    }
    return indexInto<NodeTag>(static_cast<std::uint64_t>(child), targets.nodes, Lump::Nodes, parent,
                              "child node")
        .transform([](NodeIndex node) { return NodeChild{node}; });
}

[[nodiscard]] auto decodeNode(std::span<const std::byte> record, std::size_t index,
                              const TreeTargets& targets) -> Result<BspNode, BspError> {
    const auto plane = indexInto<PlaneTag>(loadLittleEndian<std::uint32_t>(record), targets.planes,
                                           Lump::Nodes, index, "plane");
    const auto front =
        decodeNodeChild(loadLittleEndian<std::int16_t>(record.subspan(kNodeChildrenOffset)), index, targets);
    const auto back = decodeNodeChild(
        loadLittleEndian<std::int16_t>(record.subspan(kNodeChildrenOffset + kChildBytes)), index, targets);
    if (!plane || !front || !back) {
        return std::unexpected(!plane ? plane.error() : !front ? front.error() : back.error());
    }
    const std::uint32_t firstFace = loadLittleEndian<std::uint16_t>(record.subspan(kNodeFirstFaceOffset));
    const std::uint32_t faceCount = loadLittleEndian<std::uint16_t>(record.subspan(kNodeFaceCountOffset));
    if (auto range = checkRange(firstFace, faceCount, targets.faces, Lump::Nodes, index, "faces"); !range) {
        return std::unexpected(std::move(range.error()));
    }
    return BspNode{.plane = *plane,
                   .children = {*front, *back},
                   .bounds = readShortBounds(record.subspan(kNodeBoundsOffset)),
                   .firstFace = FaceIndex{.value = firstFace},
                   .faceCount = faceCount};
}

[[nodiscard]] auto decodeLeaf(std::span<const std::byte> record, std::size_t index,
                              const TreeTargets& targets) -> Result<BspLeaf, BspError> {
    const auto contents = contentsOf(loadLittleEndian<std::int32_t>(record), Lump::Leaves, index);
    if (!contents) {
        return std::unexpected(contents.error());
    }
    const std::uint32_t firstMarkSurface =
        loadLittleEndian<std::uint16_t>(record.subspan(kLeafFirstMarkSurfaceOffset));
    const std::uint32_t markSurfaceCount =
        loadLittleEndian<std::uint16_t>(record.subspan(kLeafMarkSurfaceCountOffset));
    if (auto range = checkRange(firstMarkSurface, markSurfaceCount, targets.markSurfaces, Lump::Leaves, index,
                                "mark surfaces");
        !range) {
        return std::unexpected(std::move(range.error()));
    }
    BspLeaf leaf{.contents = *contents,
                 .visibilityOffset = std::nullopt,
                 .bounds = readShortBounds(record.subspan(kLeafBoundsOffset)),
                 .firstMarkSurface = MarkSurfaceIndex{.value = firstMarkSurface},
                 .markSurfaceCount = markSurfaceCount,
                 .ambientLevels = {}};
    for (std::size_t sound = 0; sound < kAmbientSounds; ++sound) {
        leaf.ambientLevels.at(sound) = std::to_integer<std::uint8_t>(record[kLeafAmbientOffset + sound]);
    }
    const auto visibility = loadLittleEndian<std::int32_t>(record.subspan(kLeafVisibilityOffset));
    if (visibility != kNoVisibility) {
        if (visibility < 0 || std::cmp_greater_equal(visibility, targets.visibilityBytes)) {
            return fail(BspError::BadIndex,
                        std::format("{}: visibility offset {} is not below {}", where(Lump::Leaves, index),
                                    visibility, targets.visibilityBytes));
        }
        leaf.visibilityOffset = static_cast<std::uint32_t>(visibility);
    }
    return leaf;
}

/// A child of a clip node, or a hull root: a clip node, or contents. Errors: BadIndex, BadValue.
[[nodiscard]] auto decodeClipNodeChild(std::int32_t child, Lump lump, std::size_t record,
                                       std::size_t clipNodes) -> Result<ClipNodeChild, BspError> {
    if (child < 0) {
        return contentsOf(child, lump, record).transform([](Contents contents) {
            return ClipNodeChild{contents};
        });
    }
    return indexInto<ClipNodeTag>(static_cast<std::uint64_t>(child), clipNodes, lump, record, "clip node")
        .transform([](ClipNodeIndex node) { return ClipNodeChild{node}; });
}

[[nodiscard]] auto decodeClipNode(std::span<const std::byte> record, std::size_t index,
                                  const TreeTargets& targets) -> Result<ClipNode, BspError> {
    const auto plane = indexInto<PlaneTag>(loadLittleEndian<std::uint32_t>(record), targets.planes,
                                           Lump::ClipNodes, index, "plane");
    const auto front =
        decodeClipNodeChild(loadLittleEndian<std::int16_t>(record.subspan(kClipNodeChildrenOffset)),
                            Lump::ClipNodes, index, targets.clipNodes);
    const auto back = decodeClipNodeChild(
        loadLittleEndian<std::int16_t>(record.subspan(kClipNodeChildrenOffset + kChildBytes)),
        Lump::ClipNodes, index, targets.clipNodes);
    if (!plane || !front || !back) {
        return std::unexpected(!plane ? plane.error() : !front ? front.error() : back.error());
    }
    return ClipNode{.plane = *plane, .children = {*front, *back}};
}

[[nodiscard]] auto decodeModel(std::span<const std::byte> record, std::size_t index,
                               const TreeTargets& targets) -> Result<BspModel, BspError> {
    const Aabb bounds{.min = readVec3(record), .max = readVec3(record.subspan(kModelMaxOffset))};
    const Vec3 origin = readVec3(record.subspan(kModelOriginOffset));
    if (!isFinite(bounds.min) || !isFinite(bounds.max) || !isFinite(origin)) {
        return fail(BspError::BadValue,
                    std::format("{}: bounds and origin must be finite", where(Lump::Models, index)));
    }
    const auto headNode =
        indexInto<NodeTag>(loadLittleEndian<std::uint32_t>(record.subspan(kModelHeadsOffset)), targets.nodes,
                           Lump::Models, index, "head node");
    if (!headNode) {
        return std::unexpected(headNode.error());
    }
    BspModel model{
        .bounds = bounds,
        .origin = origin,
        .headNode = *headNode,
        .hulls = {},
        .visibleLeafCount = loadLittleEndian<std::uint32_t>(record.subspan(kModelVisibleLeavesOffset)),
        .firstFace =
            FaceIndex{.value = loadLittleEndian<std::uint32_t>(record.subspan(kModelFirstFaceOffset))},
        .faceCount = loadLittleEndian<std::uint32_t>(record.subspan(kModelFaceCountOffset))};
    for (std::size_t hull = 0; hull < kClipHulls; ++hull) {
        const auto head = loadLittleEndian<std::int32_t>(
            record.subspan(kModelHeadsOffset + ((hull + 1) * sizeof(std::int32_t))));
        // For a hull it writes no clip node of, hlbsp writes the count of clip nodes written so far:
        // in the last models, the count of them all. Such a hull holds nothing.
        if (std::cmp_equal(head, targets.clipNodes)) {
            model.hulls.at(hull) = Contents::Empty;
            continue;
        }
        auto root = decodeClipNodeChild(head, Lump::Models, index, targets.clipNodes);
        if (!root) {
            return std::unexpected(std::move(root.error()));
        }
        model.hulls.at(hull) = *root;
    }
    if (std::uint64_t{model.visibleLeafCount} >= std::max<std::uint64_t>(targets.leaves, 1)) {
        return fail(BspError::BadIndex,
                    std::format("{}: {} visible leaves after leaf 0, of {} leaves",
                                where(Lump::Models, index), model.visibleLeafCount, targets.leaves));
    }
    if (auto range =
            checkRange(model.firstFace.value, model.faceCount, targets.faces, Lump::Models, index, "faces");
        !range) {
        return std::unexpected(std::move(range.error()));
    }
    return model;
}

/// The entity text: `{ "key" "value" ... }` blocks. A quoted word is taken as it is, without escapes:
/// the wad key holds Windows paths. The text ends at the end of the lump or at its first zero byte.
/// Errors: BadEntities.
[[nodiscard]] auto parseEntities(std::span<const std::byte> lump)
    -> Result<std::vector<BspEntity>, BspError> {
    const std::string text = std::ranges::subrange(lump.begin(), std::ranges::find(lump, std::byte{0})) |
                             std::views::transform([](std::byte byte) { return static_cast<char>(byte); }) |
                             std::ranges::to<std::string>();
    std::size_t offset = 0;
    std::uint32_t line = 1;
    const auto failure = [&line](std::string_view problem) {
        return fail(BspError::BadEntities, std::format("Entities: line {}: {}", line, problem));
    };
    const auto skipSpace = [&] {
        while (offset < text.size() && (text[offset] == ' ' || text[offset] == '\t' || text[offset] == '\r' ||
                                        text[offset] == '\n')) {
            line += text[offset] == '\n' ? 1U : 0U;
            ++offset;
        }
    };
    const auto readQuoted = [&]() -> std::optional<std::string> {
        if (offset == text.size() || text[offset] != '"') {
            return std::nullopt;
        }
        const std::size_t end = text.find('"', offset + 1);
        if (end == std::string::npos) {
            return std::nullopt;
        }
        std::string word = text.substr(offset + 1, end - offset - 1);
        line += static_cast<std::uint32_t>(std::ranges::count(word, '\n'));
        offset = end + 1;
        return word;
    };
    std::vector<BspEntity> entities;
    for (skipSpace(); offset < text.size(); skipSpace()) {
        if (text[offset] != '{') {
            return failure("an entity does not start with '{'");
        }
        ++offset;
        BspEntity entity;
        for (skipSpace(); offset < text.size() && text[offset] != '}'; skipSpace()) {
            auto key = readQuoted();
            skipSpace();
            auto value = readQuoted();
            if (!key || !value) {
                return failure("a key and its value must be two quoted words");
            }
            entity.keyValues.emplace_back(std::move(*key), std::move(*value));
        }
        if (offset == text.size()) {
            return failure("an entity is not closed with '}'");
        }
        ++offset;
        entities.push_back(std::move(entity));
    }
    return entities;
}

/// Moves what `decoded` holds into `destination`; its failure, when it holds one.
template <class Records>
[[nodiscard]] auto store(Result<Records, BspError>&& decoded, Records& destination)
    -> Result<void, BspError> {
    if (!decoded) {
        return std::unexpected{std::move(decoded.error())};
    }
    destination = std::move(*decoded);
    return {};
}

/// Fails unless every node is the child of at most one other: with children after parents, that
/// makes a tree, whose every walk is linear. Two parents would make a graph whose full walk, the
/// renderer's, doubles with every level. Errors: BadValue.
[[nodiscard]] auto checkSingleParents(const std::vector<BspNode>& nodes) -> Result<void, BspError> {
    std::vector<bool> hasParent(nodes.size());
    for (std::size_t parent = 0; parent < nodes.size(); ++parent) {
        for (const NodeChild& child : nodes[parent].children) {
            const auto* node = std::get_if<NodeIndex>(&child);
            if (node == nullptr) {
                continue;
            }
            if (hasParent[node->value]) {
                return fail(BspError::BadValue,
                            std::format("{}: {} has a second parent", where(Lump::Nodes, parent),
                                        where(Lump::Nodes, node->value)));
            }
            hasParent[node->value] = true;
        }
    }
    return {};
}

/// Fails when clip nodes form a cycle, which would make a trace through them endless. Shared
/// subtrees are fine: a trace splits its segment at each plane, so the pieces that reach a node by
/// different paths are disjoint, and it visits a node at most once per crossing. Errors: BadValue.
[[nodiscard]] auto checkAcyclic(const std::vector<ClipNode>& clipNodes) -> Result<void, BspError> {
    // Kahn's algorithm: taking away, again and again, the nodes that no node left points to takes
    // away every node, unless some are in a cycle or under one.
    std::vector<std::size_t> parents(clipNodes.size());
    for (const ClipNode& node : clipNodes) {
        for (const ClipNodeChild& child : node.children) {
            if (const auto* index = std::get_if<ClipNodeIndex>(&child)) {
                ++parents[index->value];
            }
        }
    }
    std::vector<std::size_t> unparented =
        std::views::iota(std::size_t{0}, clipNodes.size()) |
        std::views::filter([&parents](std::size_t node) { return parents[node] == 0; }) |
        std::ranges::to<std::vector>();
    std::size_t takenAway = 0;
    while (!unparented.empty()) {
        const std::size_t node = unparented.back();
        unparented.pop_back();
        ++takenAway;
        for (const ClipNodeChild& child : clipNodes[node].children) {
            const auto* index = std::get_if<ClipNodeIndex>(&child);
            if (index != nullptr && --parents[index->value] == 0) {
                unparented.push_back(index->value);
            }
        }
    }
    if (takenAway != clipNodes.size()) {
        const auto left = std::ranges::find_if(parents, [](std::size_t count) { return count != 0; });
        return fail(BspError::BadValue,
                    std::format("{} is in a cycle of clip nodes, or under one",
                                where(Lump::ClipNodes, narrow<std::size_t>(left - parents.begin()))));
    }
    return {};
}

/// The bytes of a potentially visible set of `leafCount` leaves: a bit for each.
[[nodiscard]] auto visibilityRowBytes(std::uint32_t leafCount) noexcept -> std::size_t {
    return bytesForBits(leafCount);
}

/// Decompresses the potentially visible set of `leaf`, at `offset` of `visibility`, into `row`: the
/// compiler writes each run of zero bytes as a zero and the run's length. Errors: BadVisibility (past
/// the lump).
[[nodiscard]] auto decompressVisibility(std::span<const std::byte> visibility, std::size_t offset,
                                        std::span<std::uint8_t> row, std::uint32_t leaf)
    -> Result<void, BspError> {
    std::size_t in = offset;
    std::size_t out = 0;
    const auto pastTheLump = [&] {
        return fail(BspError::BadVisibility,
                    std::format("leaf {}: its set goes past the visibility lump at byte {} of {}", leaf, out,
                                row.size()));
    };
    while (out < row.size()) {
        if (in == visibility.size()) {
            return pastTheLump();
        }
        const auto value = std::to_integer<std::uint8_t>(visibility[in++]);
        if (value != 0) {
            row[out++] = value;
            continue;
        }
        if (in == visibility.size()) {
            return pastTheLump();
        }
        // The vis tools pad each set to whole 64-leaf words, and some count the leaves of the brush
        // entities' models too (de_dust2: 152 bytes for 1201 leaves; de_prodigy: 320 for 2022): the
        // last run of zeros may go past the world's leaves. The set ends with them, as the original's.
        const std::size_t zeros = std::min(std::to_integer<std::size_t>(visibility[in++]), row.size() - out);
        std::ranges::fill(row.subspan(out, zeros), std::uint8_t{0});
        out += zeros;
    }
    return {};
}

/// The tree, the models and the entities.
struct Tree {
    std::vector<FaceIndex> markSurfaces;
    std::span<const std::byte> visibility;
    std::vector<BspNode> nodes;
    std::vector<BspLeaf> leaves;
    std::vector<ClipNode> clipNodes;
    std::vector<BspModel> models;
    std::vector<BspEntity> entities;
};

/// Errors: MisalignedLump, BadIndex, BadValue, BadEntities, BadVisibility.
[[nodiscard]] auto decodeTree(const Lumps& lumps, std::size_t planes, std::size_t faces)
    -> Result<Tree, BspError> {
    Tree tree;
    const auto decodeMarkSurfaceOf = [faces](std::span<const std::byte> record, std::size_t index) {
        return indexInto<FaceTag>(loadLittleEndian<std::uint16_t>(record), faces, Lump::MarkSurfaces, index,
                                  "face");
    };
    if (auto stored = store(decodeRecords(lumps, Lump::MarkSurfaces, kMarkSurfaceBytes, decodeMarkSurfaceOf),
                            tree.markSurfaces);
        !stored) {
        return std::unexpected(std::move(stored.error()));
    }
    tree.visibility = lumps[Lump::Visibility];
    // Every array's size first: a node indexes the nodes after it, and the leaves.
    const TreeTargets targets{.planes = planes,
                              .faces = faces,
                              .nodes = lumps[Lump::Nodes].size() / kNodeBytes,
                              .leaves = lumps[Lump::Leaves].size() / kLeafBytes,
                              .clipNodes = lumps[Lump::ClipNodes].size() / kClipNodeBytes,
                              .markSurfaces = tree.markSurfaces.size(),
                              .visibilityBytes = tree.visibility.size()};
    const auto with = [&targets](auto decode) {
        return [&targets, decode](std::span<const std::byte> record, std::size_t index) {
            return decode(record, index, targets);
        };
    };
    if (auto stored = store(decodeRecords(lumps, Lump::Leaves, kLeafBytes, with(decodeLeaf)), tree.leaves);
        !stored) {
        return std::unexpected(std::move(stored.error()));
    }
    if (auto stored = store(decodeRecords(lumps, Lump::Nodes, kNodeBytes, with(decodeNode)), tree.nodes);
        !stored) {
        return std::unexpected(std::move(stored.error()));
    }
    if (auto stored = store(decodeRecords(lumps, Lump::ClipNodes, kClipNodeBytes, with(decodeClipNode)),
                            tree.clipNodes);
        !stored) {
        return std::unexpected(std::move(stored.error()));
    }
    if (auto trees =
            checkSingleParents(tree.nodes).and_then([&tree] { return checkAcyclic(tree.clipNodes); });
        !trees) {
        return std::unexpected(std::move(trees.error()));
    }
    if (auto stored = store(decodeRecords(lumps, Lump::Models, kModelBytes, with(decodeModel)), tree.models);
        !stored) {
        return std::unexpected(std::move(stored.error()));
    }
    if (auto stored = store(parseEntities(lumps[Lump::Entities]), tree.entities); !stored) {
        return std::unexpected(std::move(stored.error()));
    }
    // The set of every leaf of the world, so that potentiallyVisible() decompresses without checks.
    if (!tree.models.empty()) {
        const std::uint32_t leafCount = tree.models.front().visibleLeafCount;
        std::vector<std::uint8_t> row(visibilityRowBytes(leafCount));
        for (std::uint32_t leaf = 1; leaf <= leafCount; ++leaf) {
            const std::optional<std::uint32_t> offset = tree.leaves[leaf].visibilityOffset;
            if (auto decoded = offset ? decompressVisibility(tree.visibility, *offset, row, leaf)
                                      : Result<void, BspError>{};
                !decoded) {
                return std::unexpected(std::move(decoded.error()));
            }
        }
    }
    return tree;
}

} // namespace

auto BspView::parse(std::span<const std::byte> bytes) -> Result<BspView, BspError> {
    const auto lumps = lumpsOf(bytes);
    if (!lumps) {
        return std::unexpected(lumps.error());
    }
    BspView view;
    if (auto stored = store(decodeRecords(*lumps, Lump::Planes, kPlaneBytes, decodePlane), view.m_planes);
        !stored) {
        return std::unexpected(std::move(stored.error()));
    }
    if (auto stored =
            store(decodeRecords(*lumps, Lump::Vertices, kVertexBytes, decodeVertex), view.m_vertices);
        !stored) {
        return std::unexpected(std::move(stored.error()));
    }
    const auto decodeEdgeOf = [vertices = view.m_vertices.size()](std::span<const std::byte> record,
                                                                  std::size_t index) {
        return decodeEdge(record, index, vertices);
    };
    if (auto stored = store(decodeRecords(*lumps, Lump::Edges, kEdgeBytes, decodeEdgeOf), view.m_edges);
        !stored) {
        return std::unexpected(std::move(stored.error()));
    }
    const auto decodeSurfaceEdgeOf = [edges = view.m_edges.size()](std::span<const std::byte> record,
                                                                   std::size_t index) {
        return decodeSurfaceEdge(record, index, edges);
    };
    if (auto stored = store(decodeRecords(*lumps, Lump::SurfaceEdges, kSurfaceEdgeBytes, decodeSurfaceEdgeOf),
                            view.m_surfaceEdges);
        !stored) {
        return std::unexpected(std::move(stored.error()));
    }
    if (auto stored = store(decodeTextures((*lumps)[Lump::Textures]), view.m_textures); !stored) {
        return std::unexpected(std::move(stored.error()));
    }
    const auto decodeTextureInfoOf = [textures = view.m_textures.size()](std::span<const std::byte> record,
                                                                         std::size_t index) {
        return decodeTextureInfo(record, index, textures);
    };
    if (auto stored = store(decodeRecords(*lumps, Lump::TextureInfos, kTextureInfoBytes, decodeTextureInfoOf),
                            view.m_textureInfos);
        !stored) {
        return std::unexpected(std::move(stored.error()));
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
    if (auto stored = store(decodeRecords(*lumps, Lump::Faces, kFaceBytes, decodeFaceOf), view.m_faces);
        !stored) {
        return std::unexpected(std::move(stored.error()));
    }
    auto tree = decodeTree(*lumps, view.m_planes.size(), view.m_faces.size());
    if (!tree) {
        return std::unexpected(std::move(tree.error()));
    }
    view.m_markSurfaces = std::move(tree->markSurfaces);
    view.m_visibility = tree->visibility;
    view.m_nodes = std::move(tree->nodes);
    view.m_leaves = std::move(tree->leaves);
    view.m_clipNodes = std::move(tree->clipNodes);
    view.m_models = std::move(tree->models);
    view.m_entities = std::move(tree->entities);
    return view;
}

auto BspView::leafAt(Vec3 point) const noexcept -> LeafIndex {
    if (m_models.empty()) {
        return LeafIndex{0};
    }
    NodeChild child = m_models.front().headNode;
    while (const NodeIndex* node = std::get_if<NodeIndex>(&child)) {
        const BspNode& record = m_nodes[node->value];
        const bool isInFront = m_planes[record.plane.value].plane.signedDistance(point) > 0.0F;
        child = record.children[isInFront ? 0 : 1];
    }
    return std::get<LeafIndex>(child);
}

auto BspView::potentiallyVisible(LeafIndex leaf) const -> LeafSet {
    const std::uint32_t leafCount = m_models.empty() ? 0 : m_models.front().visibleLeafCount;
    std::vector<std::uint8_t> bits(visibilityRowBytes(leafCount));
    const std::optional<std::uint32_t> offset =
        leaf.value == 0 || leaf.value > leafCount ? std::nullopt : m_leaves[leaf.value].visibilityOffset;
    if (!offset) {
        std::ranges::fill(bits, std::uint8_t{0xFF});
        return LeafSet{std::move(bits), leafCount};
    }
    const auto decoded = decompressVisibility(m_visibility, *offset, bits, leaf.value);
    expects(decoded.has_value(), "BspView::potentiallyVisible: parse() checked the set of every leaf");
    return LeafSet{std::move(bits), leafCount};
}

auto LeafSet::contains(LeafIndex leaf) const noexcept -> bool {
    if (leaf.value == 0 || leaf.value > m_leafCount) {
        return false;
    }
    const std::uint32_t bit = leaf.value - 1;
    return ((static_cast<std::uint32_t>(m_bits[bit / kBitsPerByte]) >> (bit % kBitsPerByte)) & 1U) != 0;
}

auto integerOfValue(std::string_view value) noexcept -> std::optional<int> {
    const std::size_t first = std::min(value.find_first_not_of(' '), value.size());
    int number = 0;
    const auto [stop, error] = std::from_chars(value.data() + first, value.data() + value.size(), number);
    return error == std::errc{} ? std::optional{number} : std::nullopt;
}

auto numberOfValue(std::string_view value) noexcept -> std::optional<float> {
    const std::size_t first = std::min(value.find_first_not_of(' '), value.size());
    float number = 0.0F;
    const auto [stop, error] = std::from_chars(value.data() + first, value.data() + value.size(), number);
    return error == std::errc{} ? std::optional{number} : std::nullopt;
}

auto vectorOfValue(std::string_view value) noexcept -> std::optional<Vec3> {
    std::array<float, 3> numbers{};
    const char* next = value.data();
    const char* const end = value.data() + value.size();
    for (float& number : numbers) {
        while (next != end && *next == ' ') {
            ++next;
        }
        const auto [stop, error] = std::from_chars(next, end, number);
        if (error != std::errc{}) {
            return std::nullopt;
        }
        next = stop;
    }
    return Vec3{.x = numbers[0], .y = numbers[1], .z = numbers[2]};
}

auto BspEntity::find(std::string_view key) const noexcept -> std::optional<std::string_view> {
    const auto reversed = keyValues | std::views::reverse;
    const auto found =
        std::ranges::find_if(reversed, [key](const auto& keyValue) { return keyValue.first == key; });
    if (found == reversed.end()) {
        return std::nullopt;
    }
    return found->second;
}

auto BspEntity::findInteger(std::string_view key) const noexcept -> std::optional<int> {
    return find(key).and_then(integerOfValue);
}

auto BspEntity::findNumber(std::string_view key) const noexcept -> std::optional<float> {
    return find(key).and_then(numberOfValue);
}

auto BspEntity::findVector(std::string_view key) const noexcept -> std::optional<Vec3> {
    return find(key).and_then(vectorOfValue);
}

auto contentsNumbered(std::int32_t value) noexcept -> std::optional<Contents> {
    if (value > std::to_underlying(Contents::Empty) || value < std::to_underlying(Contents::Ladder)) {
        return std::nullopt;
    }
    return static_cast<Contents>(value);
}

auto BspEntity::findBrushModel() const noexcept -> std::optional<ModelIndex> {
    const std::optional<std::string_view> model = find("model");
    ModelIndex index;
    if (!model || !model->starts_with('*') ||
        std::from_chars(model->data() + 1, model->data() + model->size(), index.value).ec != std::errc{}) {
        return std::nullopt;
    }
    return index;
}

} // namespace nx::assets
