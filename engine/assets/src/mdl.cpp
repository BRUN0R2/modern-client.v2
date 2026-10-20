// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;
import nx.math;

namespace nx::assets {

namespace {

// The header: "IDST", the version, the name, the file length, the eye position, two boxes (the
// model's, then the clipping one), the flags, then a count and an offset for every table.
constexpr std::array kStudioMagic{std::byte{'I'}, std::byte{'D'}, std::byte{'S'}, std::byte{'T'}};
constexpr std::size_t kStudioVersionOffset = 4;
constexpr std::size_t kStudioNameOffset = 8;
constexpr std::size_t kStudioNameBytes = 64;
constexpr std::size_t kEyePositionOffset = 76;
constexpr std::size_t kBoundsOffset = 88;
constexpr std::size_t kStudioHeaderBytes = 244;

/// A table of the header: where its count and its offset are, and the size of its records.
struct TableField {
    std::size_t countOffset = 0;
    std::size_t recordBytes = 0;
    std::string_view name;
};

constexpr TableField kBones{.countOffset = 140, .recordBytes = 112, .name = "Bones"};
constexpr TableField kBoneControllers{.countOffset = 148, .recordBytes = 24, .name = "BoneControllers"};
constexpr TableField kHitboxes{.countOffset = 156, .recordBytes = 32, .name = "Hitboxes"};
constexpr TableField kTextures{.countOffset = 180, .recordBytes = 80, .name = "Textures"};
constexpr TableField kBodyParts{.countOffset = 204, .recordBytes = 76, .name = "BodyParts"};
constexpr TableField kAttachments{.countOffset = 212, .recordBytes = 88, .name = "Attachments"};

// The skins: the reference count, the family count, then the offset of the 16-bit texture indices.
constexpr std::size_t kSkinReferencesOffset = 192;
constexpr std::size_t kSkinFamiliesOffset = 196;
constexpr std::size_t kSkinsOffset = 200;

// A bone: name, parent (-1 for a root), flags, the controller of each axis (-1 for none), the rest
// values and the scales of each axis.
constexpr std::size_t kBoneNameBytes = 32;
constexpr std::size_t kBoneParentOffset = 32;
constexpr std::size_t kBoneControllersOffset = 40;
constexpr std::size_t kBoneValuesOffset = 64;
constexpr std::size_t kBoneScalesOffset = 88;
constexpr std::int32_t kNone = -1;

// A bone controller: bone, type (one axis bit, and the loop bit), start, end, rest and channel.
constexpr std::size_t kControllerTypeOffset = 4;
constexpr std::size_t kControllerStartOffset = 8;
constexpr std::size_t kControllerEndOffset = 12;
constexpr std::size_t kControllerRestOffset = 16;
constexpr std::size_t kControllerChannelOffset = 20;
constexpr std::uint32_t kLoopingController = 0x8000;

// A hitbox: bone, group, box. An attachment: name, type, bone, origin, and three unused vectors.
constexpr std::size_t kHitboxGroupOffset = 4;
constexpr std::size_t kHitboxBoundsOffset = 8;
constexpr std::size_t kAttachmentNameBytes = 32;
constexpr std::size_t kAttachmentBoneOffset = 36;
constexpr std::size_t kAttachmentOriginOffset = 40;

// A texture: name, flags, width, height and the offset of its pixels, which the palette follows.
constexpr std::size_t kStudioTextureNameBytes = 64;
constexpr std::size_t kStudioTextureFlagsOffset = 64;
constexpr std::size_t kStudioTextureWidthOffset = 68;
constexpr std::size_t kStudioTextureHeightOffset = 72;
constexpr std::size_t kStudioTexturePixelsOffset = 76;

// A body part: name, model count, base, the offset of its models. A model: name, type, bounding
// radius, then counts and offsets of meshes, vertices (with their bones), normals (with theirs).
constexpr std::size_t kBodyPartNameBytes = 64;
constexpr std::size_t kBodyPartModelCountOffset = 64;
constexpr std::size_t kBodyPartBaseOffset = 68;
constexpr std::size_t kBodyPartModelsOffset = 72;
constexpr std::size_t kModelBytes = 112;
constexpr std::size_t kModelNameBytes = 64;
constexpr std::size_t kModelRadiusOffset = 68;
constexpr std::size_t kModelMeshCountOffset = 72;
constexpr std::size_t kModelVertexCountOffset = 80;
constexpr std::size_t kModelNormalCountOffset = 92;

// A mesh: triangle count, the offset of its triangle commands, skin reference, normal count and
// offset. A command: a 16-bit corner count (positive for a strip, negative for a fan, 0 to end),
// then each corner's vertex, normal, s and t (16 bits each).
constexpr std::size_t kMeshBytes = 20;
constexpr std::size_t kMeshCommandsOffset = 4;
constexpr std::size_t kMeshSkinReferenceOffset = 8;
constexpr std::size_t kCornerBytes = 8;
constexpr std::size_t kMinimumCorners = 3;

constexpr std::size_t kVectorBytes = 12;

/// "<table> <record>": where a failure is.
[[nodiscard]] auto where(std::string_view table, std::size_t record) -> std::string {
    return std::format("{} {}", table, record);
}

/// `count` records of `recordBytes` at `offset` (32-bit fields of the file) within `bytes`.
/// Errors: OutOfBounds.
[[nodiscard]] auto region(std::span<const std::byte> bytes, std::uint64_t offset, std::uint64_t count,
                          std::size_t recordBytes, std::string_view what)
    -> Result<std::span<const std::byte>, MdlError> {
    const std::uint64_t size = count * recordBytes;
    if (offset > bytes.size() || size > bytes.size() - offset) {
        return fail(MdlError::OutOfBounds, std::format("{}: {} bytes at {} are outside the file's {}", what,
                                                       size, offset, bytes.size()));
    }
    return bytes.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
}

/// The records of a header table: its count and offset from the header.
[[nodiscard]] auto tableOf(std::span<const std::byte> bytes, const TableField& table)
    -> Result<std::span<const std::byte>, MdlError> {
    const auto count = loadLittleEndian<std::uint32_t>(bytes.subspan(table.countOffset));
    const auto offset =
        loadLittleEndian<std::uint32_t>(bytes.subspan(table.countOffset + sizeof(std::uint32_t)));
    return region(bytes, offset, count, table.recordBytes, table.name);
}

/// `value` as an index into a list of `count`. Errors: BadIndex.
template <class Tag>
[[nodiscard]] auto indexInto(std::int64_t value, std::size_t count, std::string_view table,
                             std::size_t record, std::string_view field)
    -> Result<StudioIndex<Tag>, MdlError> {
    if (value < 0 || std::cmp_greater_equal(value, count)) {
        return fail(MdlError::BadIndex,
                    std::format("{}: {} {} is not in [0, {})", where(table, record), field, value, count));
    }
    return StudioIndex<Tag>{.value = static_cast<std::uint32_t>(value)};
}

/// Decodes every record of `table` with `decode(record, index)`. Errors: those of `decode`.
template <class Decode>
[[nodiscard]] auto
decodeAll(std::span<const std::byte> table, std::size_t recordBytes, Decode decode) -> Result<
    std::vector<typename std::invoke_result_t<Decode, std::span<const std::byte>, std::size_t>::value_type>,
    MdlError> {
    using Record = typename std::invoke_result_t<Decode, std::span<const std::byte>, std::size_t>::value_type;
    std::vector<Record> records;
    records.reserve(table.size() / recordBytes);
    for (std::size_t index = 0; index < table.size() / recordBytes; ++index) {
        auto record = decode(table.subspan(index * recordBytes, recordBytes), index);
        if (!record) {
            return std::unexpected(std::move(record.error()));
        }
        records.push_back(std::move(*record));
    }
    return records;
}

[[nodiscard]] auto decodeBone(std::span<const std::byte> record, std::size_t index, std::size_t controllers)
    -> Result<StudioBone, MdlError> {
    StudioBone bone{.name = nameOf(record.first(kBoneNameBytes)),
                    .parent = {},
                    .controllers = {},
                    .values = {},
                    .scales = {}};
    if (const auto parent = loadLittleEndian<std::int32_t>(record.subspan(kBoneParentOffset));
        parent != kNone) {
        // A parent comes first, so the skeleton is computed in one pass and cannot cycle.
        auto parentIndex = indexInto<BoneTag>(parent, index, "Bones", index, "parent");
        if (!parentIndex) {
            return std::unexpected(std::move(parentIndex.error()));
        }
        bone.parent = *parentIndex;
    }
    for (std::size_t axis = 0; axis < kBoneAxes; ++axis) {
        const auto controller = loadLittleEndian<std::int32_t>(
            record.subspan(kBoneControllersOffset + (axis * sizeof(std::int32_t))));
        if (controller != kNone) {
            auto controllerIndex =
                indexInto<BoneControllerTag>(controller, controllers, "Bones", index, "controller");
            if (!controllerIndex) {
                return std::unexpected(std::move(controllerIndex.error()));
            }
            bone.controllers.at(axis) = *controllerIndex;
        }
        bone.values.at(axis) =
            loadLittleEndian<float>(record.subspan(kBoneValuesOffset + (axis * sizeof(float))));
        bone.scales.at(axis) =
            loadLittleEndian<float>(record.subspan(kBoneScalesOffset + (axis * sizeof(float))));
    }
    if (!std::ranges::all_of(bone.values, [](float value) { return std::isfinite(value); }) ||
        !std::ranges::all_of(bone.scales, [](float value) { return std::isfinite(value); })) {
        return fail(MdlError::BadValue,
                    std::format("{}: the values and scales must be finite", where("Bones", index)));
    }
    return bone;
}

[[nodiscard]] auto decodeController(std::span<const std::byte> record, std::size_t index, std::size_t bones)
    -> Result<BoneController, MdlError> {
    const auto bone =
        indexInto<BoneTag>(loadLittleEndian<std::int32_t>(record), bones, "BoneControllers", index, "bone");
    if (!bone) {
        return std::unexpected(bone.error());
    }
    const auto type = loadLittleEndian<std::uint32_t>(record.subspan(kControllerTypeOffset));
    const std::uint32_t axisBits = type & ~kLoopingController;
    // One axis bit, X (1) to RotationZ (32).
    if (!std::has_single_bit(axisBits) || std::cmp_greater(std::bit_width(axisBits), kBoneAxes)) {
        return fail(MdlError::BadValue, std::format("{}: type {:#x} drives no single axis",
                                                    where("BoneControllers", index), type));
    }
    const auto start = loadLittleEndian<float>(record.subspan(kControllerStartOffset));
    const auto end = loadLittleEndian<float>(record.subspan(kControllerEndOffset));
    const auto channel = loadLittleEndian<std::int32_t>(record.subspan(kControllerChannelOffset));
    if (!std::isfinite(start) || !std::isfinite(end) || channel < 0) {
        return fail(MdlError::BadValue, std::format("{}: range {} to {}, channel {}",
                                                    where("BoneControllers", index), start, end, channel));
    }
    return BoneController{.bone = *bone,
                          .axis = static_cast<BoneAxis>(std::bit_width(axisBits) - 1),
                          .wraps = (type & kLoopingController) != 0,
                          .start = start,
                          .end = end,
                          .rest = loadLittleEndian<std::int32_t>(record.subspan(kControllerRestOffset)),
                          .channel = static_cast<std::uint32_t>(channel)};
}

[[nodiscard]] auto decodeHitbox(std::span<const std::byte> record, std::size_t index, std::size_t bones)
    -> Result<Hitbox, MdlError> {
    const auto bone =
        indexInto<BoneTag>(loadLittleEndian<std::int32_t>(record), bones, "Hitboxes", index, "bone");
    if (!bone) {
        return std::unexpected(bone.error());
    }
    const Aabb bounds{.min = readVec3(record.subspan(kHitboxBoundsOffset)),
                      .max = readVec3(record.subspan(kHitboxBoundsOffset + kVectorBytes))};
    if (!isFinite(bounds.min) || !isFinite(bounds.max)) {
        return fail(MdlError::BadValue, std::format("{}: the box must be finite", where("Hitboxes", index)));
    }
    return Hitbox{.bone = *bone,
                  .group = loadLittleEndian<std::uint32_t>(record.subspan(kHitboxGroupOffset)),
                  .bounds = bounds};
}

[[nodiscard]] auto decodeAttachment(std::span<const std::byte> record, std::size_t index, std::size_t bones)
    -> Result<Attachment, MdlError> {
    const auto bone =
        indexInto<BoneTag>(loadLittleEndian<std::int32_t>(record.subspan(kAttachmentBoneOffset)), bones,
                           "Attachments", index, "bone");
    if (!bone) {
        return std::unexpected(bone.error());
    }
    const Vec3 origin = readVec3(record.subspan(kAttachmentOriginOffset));
    if (!isFinite(origin)) {
        return fail(MdlError::BadValue,
                    std::format("{}: the origin must be finite", where("Attachments", index)));
    }
    return Attachment{.name = nameOf(record.first(kAttachmentNameBytes)), .bone = *bone, .origin = origin};
}

[[nodiscard]] auto decodeTexture(std::span<const std::byte> record, std::size_t index,
                                 std::span<const std::byte> bytes) -> Result<StudioTexture, MdlError> {
    const auto width = loadLittleEndian<std::int32_t>(record.subspan(kStudioTextureWidthOffset));
    const auto height = loadLittleEndian<std::int32_t>(record.subspan(kStudioTextureHeightOffset));
    if (width < 1 || height < 1) {
        return fail(MdlError::BadValue, std::format("{}: {}x{}", where("Textures", index), width, height));
    }
    const std::uint64_t pixelCount =
        std::uint64_t{static_cast<std::uint32_t>(width)} * static_cast<std::uint32_t>(height);
    const auto data =
        region(bytes, loadLittleEndian<std::uint32_t>(record.subspan(kStudioTexturePixelsOffset)),
               pixelCount + kPaletteBytes, 1, where("Textures", index));
    if (!data) {
        return std::unexpected(data.error());
    }
    StudioTexture texture{.name = nameOf(record.first(kStudioTextureNameBytes)),
                          .flags = {},
                          .width = static_cast<std::uint32_t>(width),
                          .height = static_cast<std::uint32_t>(height),
                          .pixels = data->first(static_cast<std::size_t>(pixelCount)),
                          .palette = data->last(kPaletteBytes)};
    const auto flags = loadLittleEndian<std::uint32_t>(record.subspan(kStudioTextureFlagsOffset));
    for (std::size_t bit = 0; bit < enumCount<StudioTextureFlag>(); ++bit) {
        if ((flags & (1U << bit)) != 0) {
            texture.flags.set(static_cast<StudioTextureFlag>(bit));
        }
    }
    return texture;
}

/// The vectors of a model and the bone of each: `count` vectors at the offset after the count, and
/// their bones at the offset before. Errors: OutOfBounds, BadIndex, BadValue.
[[nodiscard]] auto decodeBoundVectors(std::span<const std::byte> bytes, std::span<const std::byte> model,
                                      std::size_t countOffset, std::size_t bones, std::string_view what)
    -> Result<std::vector<BoundVector>, MdlError> {
    const auto count = loadLittleEndian<std::uint32_t>(model.subspan(countOffset));
    const auto boneTable =
        region(bytes, loadLittleEndian<std::uint32_t>(model.subspan(countOffset + sizeof(std::uint32_t))),
               count, 1, what);
    const auto vectorTable = region(
        bytes, loadLittleEndian<std::uint32_t>(model.subspan(countOffset + (2 * sizeof(std::uint32_t)))),
        count, kVectorBytes, what);
    if (!boneTable || !vectorTable) {
        return std::unexpected(!boneTable ? boneTable.error() : vectorTable.error());
    }
    std::vector<BoundVector> vectors;
    vectors.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto bone = indexInto<BoneTag>(std::to_integer<std::uint8_t>((*boneTable)[index]), bones, what,
                                             index, "bone");
        const Vec3 vector = readVec3(vectorTable->subspan(index * kVectorBytes));
        if (!bone) {
            return std::unexpected(bone.error());
        }
        if (!isFinite(vector)) {
            return fail(MdlError::BadValue, std::format("{}: {} must be finite", where(what, index), vector));
        }
        vectors.push_back(BoundVector{.vector = vector, .bone = *bone});
    }
    return vectors;
}

/// The triangle commands at `offset`, up to the zero count that ends them. Errors: OutOfBounds,
/// BadIndex, BadValue.
[[nodiscard]] auto decodeRuns(std::span<const std::byte> bytes, std::uint32_t offset,
                              const StudioModel& model, std::string_view what)
    -> Result<std::vector<TriangleRun>, MdlError> {
    std::vector<TriangleRun> runs;
    for (std::uint64_t at = offset;;) {
        const auto header = region(bytes, at, 1, sizeof(std::int16_t), what);
        if (!header) {
            return std::unexpected(header.error());
        }
        const auto count = loadLittleEndian<std::int16_t>(*header);
        if (count == 0) {
            return runs;
        }
        const std::size_t corners = static_cast<std::size_t>(std::abs(std::int32_t{count}));
        if (corners < kMinimumCorners) {
            return fail(MdlError::BadValue, std::format("{}: a run of {} corners", what, corners));
        }
        at += sizeof(std::int16_t);
        const auto data = region(bytes, at, corners, kCornerBytes, what);
        if (!data) {
            return std::unexpected(data.error());
        }
        TriangleRun run{.winding = count > 0 ? Winding::Strip : Winding::Fan, .corners = {}};
        run.corners.reserve(corners);
        for (std::size_t corner = 0; corner < corners; ++corner) {
            const auto field = [&data, corner](std::size_t position) {
                return loadLittleEndian<std::int16_t>(
                    data->subspan((corner * kCornerBytes) + (position * sizeof(std::int16_t))));
            };
            const auto vertex =
                indexInto<StudioVertexTag>(field(0), model.vertices.size(), what, corner, "vertex");
            const auto normal =
                indexInto<StudioNormalTag>(field(1), model.normals.size(), what, corner, "normal");
            if (!vertex || !normal) {
                return std::unexpected(!vertex ? vertex.error() : normal.error());
            }
            run.corners.push_back(
                MeshCorner{.vertex = *vertex, .normal = *normal, .s = field(2), .t = field(3)});
        }
        at += data->size();
        runs.push_back(std::move(run));
    }
}

[[nodiscard]] auto decodeModel(std::span<const std::byte> bytes, std::span<const std::byte> record,
                               std::size_t bones, std::string_view part) -> Result<StudioModel, MdlError> {
    StudioModel model{.name = nameOf(record.first(kModelNameBytes)),
                      .boundingRadius = loadLittleEndian<float>(record.subspan(kModelRadiusOffset)),
                      .vertices = {},
                      .normals = {},
                      .meshes = {}};
    const std::string what = std::format("{} model {:?}", part, model.name);
    if (!std::isfinite(model.boundingRadius)) {
        return fail(MdlError::BadValue, std::format("{}: the bounding radius must be finite", what));
    }
    auto vertices = decodeBoundVectors(bytes, record, kModelVertexCountOffset, bones, what + " vertices");
    auto normals = decodeBoundVectors(bytes, record, kModelNormalCountOffset, bones, what + " normals");
    if (!vertices || !normals) {
        return std::unexpected(std::move(!vertices ? vertices.error() : normals.error()));
    }
    model.vertices = std::move(*vertices);
    model.normals = std::move(*normals);
    const auto meshes = region(
        bytes, loadLittleEndian<std::uint32_t>(record.subspan(kModelMeshCountOffset + sizeof(std::uint32_t))),
        loadLittleEndian<std::uint32_t>(record.subspan(kModelMeshCountOffset)), kMeshBytes, what);
    if (!meshes) {
        return std::unexpected(meshes.error());
    }
    for (std::size_t mesh = 0; mesh < meshes->size() / kMeshBytes; ++mesh) {
        const auto meshRecord = meshes->subspan(mesh * kMeshBytes, kMeshBytes);
        auto runs =
            decodeRuns(bytes, loadLittleEndian<std::uint32_t>(meshRecord.subspan(kMeshCommandsOffset)), model,
                       std::format("{} mesh {}", what, mesh));
        if (!runs) {
            return std::unexpected(std::move(runs.error()));
        }
        model.meshes.push_back(StudioMesh{
            .skinReference = loadLittleEndian<std::uint16_t>(meshRecord.subspan(kMeshSkinReferenceOffset)),
            .runs = std::move(*runs)});
    }
    return model;
}

[[nodiscard]] auto decodeBodyPart(std::span<const std::byte> bytes, std::span<const std::byte> record,
                                  std::size_t index, std::size_t bones) -> Result<BodyPart, MdlError> {
    BodyPart part{.name = nameOf(record.first(kBodyPartNameBytes)),
                  .base = loadLittleEndian<std::uint32_t>(record.subspan(kBodyPartBaseOffset)),
                  .models = {}};
    const std::string what = std::format("{} {:?}", where("BodyParts", index), part.name);
    const auto models =
        region(bytes, loadLittleEndian<std::uint32_t>(record.subspan(kBodyPartModelsOffset)),
               loadLittleEndian<std::uint32_t>(record.subspan(kBodyPartModelCountOffset)), kModelBytes, what);
    if (!models) {
        return std::unexpected(models.error());
    }
    for (std::size_t model = 0; model < models->size() / kModelBytes; ++model) {
        auto decoded = decodeModel(bytes, models->subspan(model * kModelBytes, kModelBytes), bones, what);
        if (!decoded) {
            return std::unexpected(std::move(decoded.error()));
        }
        part.models.push_back(std::move(*decoded));
    }
    return part;
}

/// The skin families: each maps every skin reference to a texture. Errors: OutOfBounds, BadIndex.
[[nodiscard]] auto decodeSkins(std::span<const std::byte> bytes, std::size_t textures)
    -> Result<std::vector<std::vector<StudioTextureIndex>>, MdlError> {
    const auto references = loadLittleEndian<std::uint32_t>(bytes.subspan(kSkinReferencesOffset));
    const auto families = loadLittleEndian<std::uint32_t>(bytes.subspan(kSkinFamiliesOffset));
    // Both at least one, so the table's size, checked below, bounds them both.
    if (references == 0 || families == 0) {
        return fail(MdlError::BadValue,
                    std::format("Skins: {} references in {} families, for a model with textures", references,
                                families));
    }
    const auto table = region(bytes, loadLittleEndian<std::uint32_t>(bytes.subspan(kSkinsOffset)),
                              std::uint64_t{references} * families, sizeof(std::int16_t), "Skins");
    if (!table) {
        return std::unexpected(table.error());
    }
    std::vector<std::vector<StudioTextureIndex>> skins(families);
    for (std::size_t family = 0; family < families; ++family) {
        for (std::size_t reference = 0; reference < references; ++reference) {
            const std::size_t entry = (family * references) + reference;
            const auto texture = indexInto<StudioTextureTag>(
                loadLittleEndian<std::int16_t>(table->subspan(entry * sizeof(std::int16_t))), textures,
                "Skins", family, "texture");
            if (!texture) {
                return std::unexpected(texture.error());
            }
            skins[family].push_back(*texture);
        }
    }
    return skins;
}

/// Moves what `decoded` holds into `destination`; its failure, when it holds one.
template <class Records>
[[nodiscard]] auto store(Result<Records, MdlError>&& decoded, Records& destination)
    -> std::optional<Failure<MdlError>> {
    if (!decoded) {
        return std::move(decoded.error());
    }
    destination = std::move(*decoded);
    return std::nullopt;
}

/// Fails unless every mesh's skin reference is below `references`. Errors: BadIndex.
[[nodiscard]] auto checkSkinReferences(std::span<const BodyPart> bodyParts, std::size_t references)
    -> Result<void, MdlError> {
    for (const BodyPart& part : bodyParts) {
        for (const StudioModel& model : part.models) {
            for (const StudioMesh& mesh : model.meshes) {
                if (mesh.skinReference >= references) {
                    return fail(MdlError::BadIndex,
                                std::format("{:?} model {:?}: skin reference {} is not below {}", part.name,
                                            model.name, mesh.skinReference, references));
                }
            }
        }
    }
    return {};
}

} // namespace

auto MdlView::parse(std::span<const std::byte> bytes) -> Result<MdlView, MdlError> {
    if (bytes.size() < kStudioHeaderBytes) {
        return fail(MdlError::TooSmall, std::format("{} bytes, less than the {} of the header", bytes.size(),
                                                    kStudioHeaderBytes));
    }
    if (!std::ranges::equal(bytes.first(kStudioMagic.size()), kStudioMagic)) {
        return fail(MdlError::NotStudioModel, "the file does not start with \"IDST\"");
    }
    if (const auto version = loadLittleEndian<std::int32_t>(bytes.subspan(kStudioVersionOffset));
        version != kStudioVersion) {
        return fail(MdlError::UnsupportedVersion, std::format("version {}, not {}", version, kStudioVersion));
    }
    MdlView view;
    view.m_name = nameOf(bytes.subspan(kStudioNameOffset, kStudioNameBytes));
    view.m_eyePosition = readVec3(bytes.subspan(kEyePositionOffset));
    view.m_bounds = Aabb{.min = readVec3(bytes.subspan(kBoundsOffset)),
                         .max = readVec3(bytes.subspan(kBoundsOffset + kVectorBytes))};
    if (!isFinite(view.m_eyePosition) || !isFinite(view.m_bounds.min) || !isFinite(view.m_bounds.max)) {
        return fail(MdlError::BadValue, "the eye position and the bounds must be finite");
    }
    const auto bones = tableOf(bytes, kBones);
    const auto controllers = tableOf(bytes, kBoneControllers);
    const auto hitboxes = tableOf(bytes, kHitboxes);
    const auto attachments = tableOf(bytes, kAttachments);
    const auto textures = tableOf(bytes, kTextures);
    const auto bodyParts = tableOf(bytes, kBodyParts);
    for (const auto* table : {&bones, &controllers, &hitboxes, &attachments, &textures, &bodyParts}) {
        if (!*table) {
            return std::unexpected(table->error());
        }
    }
    const std::size_t boneCount = bones->size() / kBones.recordBytes;
    const std::size_t controllerCount = controllers->size() / kBoneControllers.recordBytes;
    const auto withBones = [boneCount](auto decode) {
        return [boneCount, decode](std::span<const std::byte> record, std::size_t index) {
            return decode(record, index, boneCount);
        };
    };
    const auto decodeBoneOf = [controllerCount](std::span<const std::byte> record, std::size_t index) {
        return decodeBone(record, index, controllerCount);
    };
    if (auto failure = store(decodeAll(*bones, kBones.recordBytes, decodeBoneOf), view.m_bones)) {
        return std::unexpected(std::move(*failure));
    }
    if (auto failure =
            store(decodeAll(*controllers, kBoneControllers.recordBytes, withBones(decodeController)),
                  view.m_boneControllers)) {
        return std::unexpected(std::move(*failure));
    }
    if (auto failure =
            store(decodeAll(*hitboxes, kHitboxes.recordBytes, withBones(decodeHitbox)), view.m_hitboxes)) {
        return std::unexpected(std::move(*failure));
    }
    if (auto failure = store(decodeAll(*attachments, kAttachments.recordBytes, withBones(decodeAttachment)),
                             view.m_attachments)) {
        return std::unexpected(std::move(*failure));
    }
    const auto decodeTextureOf = [bytes](std::span<const std::byte> record, std::size_t index) {
        return decodeTexture(record, index, bytes);
    };
    if (auto failure = store(decodeAll(*textures, kTextures.recordBytes, decodeTextureOf), view.m_textures)) {
        return std::unexpected(std::move(*failure));
    }
    const auto decodeBodyPartOf = [bytes, boneCount](std::span<const std::byte> record, std::size_t index) {
        return decodeBodyPart(bytes, record, index, boneCount);
    };
    if (auto failure =
            store(decodeAll(*bodyParts, kBodyParts.recordBytes, decodeBodyPartOf), view.m_bodyParts)) {
        return std::unexpected(std::move(*failure));
    }
    // With its textures, a model's skins map its meshes' references to them; without, its *T.mdl does.
    if (!view.m_textures.empty()) {
        if (auto failure = store(decodeSkins(bytes, view.m_textures.size()), view.m_skins)) {
            return std::unexpected(std::move(*failure));
        }
        if (auto references = checkSkinReferences(view.m_bodyParts, view.m_skins.front().size());
            !references) {
            return std::unexpected(std::move(references.error()));
        }
    }
    return view;
}

} // namespace nx::assets
