// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.assets;

namespace {

using nx::assets::BoneAxis;
using nx::assets::BoneIndex;
using nx::assets::MdlError;
using nx::assets::MdlView;
using nx::assets::SequenceIndex;
using nx::assets::StudioTextureFlag;
using nx::assets::Winding;

using Bytes = std::vector<std::byte>;

template <class Value>
void append(Bytes& bytes, Value value) {
    const auto raw =
        std::bit_cast<std::array<std::byte, sizeof(Value)>>(value); // the tests run on little-endian hosts
    bytes.insert(bytes.end(), raw.begin(), raw.end());
}

void appendName(Bytes& bytes, std::string_view name, std::size_t size) {
    for (std::size_t index = 0; index < size; ++index) {
        bytes.push_back(index < name.size() ? static_cast<std::byte>(name[index]) : std::byte{0});
    }
}

void appendVec3(Bytes& bytes, nx::Vec3 vector) {
    append(bytes, vector.x);
    append(bytes, vector.y);
    append(bytes, vector.z);
}

/// A model of two bones (a root and its child) with a controller, a hitbox and an attachment on the
/// child, a 16x16 texture in two skin families, and one body part whose model is one triangle. It
/// records where each record is, for the tests to break.
struct TriangleModel {
    Bytes bytes = Bytes(244);
    std::size_t bones = 0;
    std::size_t controller = 0;
    std::size_t hitbox = 0;
    std::size_t texture = 0;
    std::size_t skins = 0;
    std::size_t mesh = 0;
    std::size_t commands = 0;
    std::size_t vertexBones = 0;
    std::size_t sequences = 0;
    std::size_t animation = 0;
    Bytes groupFile; ///< models/triangle01.mdl, the animation of sequence 1

    TriangleModel() {
        const std::array kMagic{std::byte{'I'}, std::byte{'D'}, std::byte{'S'}, std::byte{'T'}};
        std::ranges::copy(kMagic, bytes.begin());
        put(4, std::int32_t{10});
        Bytes name;
        appendName(name, "models/triangle.mdl", 64);
        std::ranges::copy(name, bytes.begin() + 8);
        put(76, nx::Vec3{.x = 0.0F, .y = 0.0F, .z = 28.0F}); // eyes
        put(88, nx::Vec3{.x = -16.0F, .y = -16.0F, .z = 0.0F});
        put(100, nx::Vec3{.x = 16.0F, .y = 16.0F, .z = 72.0F});

        Bytes boneRecords;
        for (const auto& [boneName, parent] : {std::pair{"root", -1}, std::pair{"head", 0}}) {
            appendName(boneRecords, boneName, 32);
            append(boneRecords, std::int32_t{parent});
            append(boneRecords, std::int32_t{0});
            for (std::size_t axis = 0; axis < 6; ++axis) { // the head turns about Y with controller 0
                append(boneRecords, std::int32_t{parent == 0 && axis == 4 ? 0 : -1});
            }
            for (std::size_t axis = 0; axis < 6; ++axis) {
                append(boneRecords, axis == 2 ? 10.0F : 0.0F);
            }
            for (std::size_t axis = 0; axis < 6; ++axis) {
                append(boneRecords, 0.01F);
            }
        }
        bones = table(140, 2, boneRecords);

        Bytes controllerRecord;
        append(controllerRecord, std::int32_t{1});       // bone
        append(controllerRecord, std::uint32_t{0x8010}); // rotation about Y, looping
        append(controllerRecord, -30.0F);
        append(controllerRecord, 30.0F);
        append(controllerRecord, std::int32_t{0});
        append(controllerRecord, std::int32_t{0});
        controller = table(148, 1, controllerRecord);

        Bytes hitboxRecord;
        append(hitboxRecord, std::int32_t{1});
        append(hitboxRecord, std::int32_t{1}); // the head
        appendVec3(hitboxRecord, {.x = -4.0F, .y = -4.0F, .z = 0.0F});
        appendVec3(hitboxRecord, {.x = 4.0F, .y = 4.0F, .z = 8.0F});
        hitbox = table(156, 1, hitboxRecord);

        Bytes attachmentRecord;
        appendName(attachmentRecord, "eyes", 32);
        append(attachmentRecord, std::int32_t{0});
        append(attachmentRecord, std::int32_t{1});
        appendVec3(attachmentRecord, {.x = 2.0F, .y = 0.0F, .z = 4.0F});
        attachmentRecord.resize(attachmentRecord.size() + 36);
        table(212, 1, attachmentRecord);

        Bytes pixels(16 * 16, std::byte{3});
        pixels.resize(pixels.size() + 768, std::byte{9}); // the palette
        const std::size_t pixelsAt = add(pixels);
        Bytes textureRecord;
        appendName(textureRecord, "skin.bmp", 64);
        append(textureRecord, std::uint32_t{0x42}); // chrome and masked
        append(textureRecord, std::int32_t{16});
        append(textureRecord, std::int32_t{16});
        append(textureRecord, static_cast<std::int32_t>(pixelsAt));
        texture = table(180, 1, textureRecord);

        Bytes skinTable;
        append(skinTable, std::int16_t{0});
        append(skinTable, std::int16_t{0});
        skins = add(skinTable);
        put(192, std::int32_t{1}); // references
        put(196, std::int32_t{2}); // families
        put(200, static_cast<std::int32_t>(skins));

        addBodyPart();
        addSequences();
    }

    template <class Value>
    void put(std::size_t offset, Value value) {
        Bytes encoded;
        if constexpr (std::same_as<Value, nx::Vec3>) {
            appendVec3(encoded, value);
        } else {
            append(encoded, value);
        }
        std::ranges::copy(encoded, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    }

    /// Appends `data`; returns where it starts.
    auto add(const Bytes& data) -> std::size_t {
        const std::size_t offset = bytes.size();
        bytes.insert(bytes.end(), data.begin(), data.end());
        return offset;
    }

    /// Appends the records of a header table and points the header at them.
    auto table(std::size_t countOffset, std::int32_t count, const Bytes& records) -> std::size_t {
        const std::size_t offset = add(records);
        put(countOffset, count);
        put(countOffset + 4, static_cast<std::int32_t>(offset));
        return offset;
    }

private:
    /// "idle" (group 0, here): two frames, an event; bone 0 rises along Z from 5 to 7 and bone 1
    /// holds 100 about Y. "wave" (group 1, in groupFile): one frame, bone 1 at -3 about Y.
    void addSequences() {
        Bytes groups;
        appendName(groups, "default", 32);
        appendName(groups, "", 64);
        append(groups, std::int32_t{0});
        append(groups, std::int32_t{0});
        appendName(groups, "wave", 32);
        appendName(groups, "models/triangle01.mdl", 64);
        append(groups, std::int32_t{0});
        append(groups, std::int32_t{0});
        table(172, 2, groups);

        // Two bone records of six offsets from their own start, then the streams.
        Bytes idle;
        for (const std::uint16_t offset :
             std::to_array<std::uint16_t>({0, 0, 24, 0, 0, 0, 0, 0, 0, 0, 18, 0})) {
            append(idle, offset);
        }
        idle.push_back(std::byte{2}); // two values
        idle.push_back(std::byte{2}); // over two frames
        append(idle, std::int16_t{5});
        append(idle, std::int16_t{7});
        idle.push_back(std::byte{1}); // one value
        idle.push_back(std::byte{2}); // over two frames
        append(idle, std::int16_t{100});
        animation = add(idle);
        const std::size_t event = add(eventRecord());

        groupFile = Bytes{std::byte{'I'}, std::byte{'D'}, std::byte{'S'}, std::byte{'Q'}};
        append(groupFile, std::int32_t{10});
        appendName(groupFile, "triangle01", 64);
        append(groupFile, std::int32_t{0});
        const auto waveAnimation = static_cast<std::int32_t>(groupFile.size());
        for (const std::uint16_t offset :
             std::to_array<std::uint16_t>({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 0})) {
            append(groupFile, offset);
        }
        groupFile.push_back(std::byte{1});
        groupFile.push_back(std::byte{1});
        append(groupFile, std::int16_t{-3});

        Bytes records = sequenceRecord("idle", 2, 1, static_cast<std::int32_t>(event), 0,
                                       static_cast<std::int32_t>(animation));
        const Bytes wave = sequenceRecord("wave", 1, 0, 0, 1, waveAnimation);
        records.insert(records.end(), wave.begin(), wave.end());
        sequences = table(164, 2, records);
    }

    [[nodiscard]] static auto eventRecord() -> Bytes {
        Bytes record;
        append(record, std::int32_t{1});    // frame
        append(record, std::int32_t{5004}); // a sound
        append(record, std::int32_t{0});
        appendName(record, "player/pl_step1.wav", 64);
        return record;
    }

    [[nodiscard]] static auto sequenceRecord(std::string_view label, std::int32_t frames, std::int32_t events,
                                             std::int32_t eventOffset, std::int32_t group,
                                             std::int32_t animationOffset) -> Bytes {
        Bytes record;
        appendName(record, label, 32);
        append(record, 30.0F);           // fps
        append(record, std::int32_t{1}); // looping
        append(record, std::int32_t{1}); // activity
        append(record, std::int32_t{1}); // its weight
        append(record, events);
        append(record, eventOffset);
        append(record, frames);
        append(record, std::int32_t{0}); // pivots
        append(record, std::int32_t{0});
        append(record, std::int32_t{0}); // motion type
        append(record, std::int32_t{0}); // motion bone
        appendVec3(record, {});          // linear movement
        append(record, std::int32_t{0});
        append(record, std::int32_t{0});
        appendVec3(record, {.x = -16.0F, .y = -16.0F, .z = 0.0F});
        appendVec3(record, {.x = 16.0F, .y = 16.0F, .z = 72.0F});
        append(record, std::int32_t{1}); // blends
        append(record, animationOffset);
        for (std::size_t field = 0; field < 6; ++field) { // blend types, starts and ends
            append(record, std::int32_t{0});
        }
        append(record, std::int32_t{0}); // blend parent
        append(record, group);
        for (std::size_t field = 0; field < 4; ++field) { // entry and exit nodes, node flags, next
            append(record, std::int32_t{0});
        }
        return record;
    }

    void addBodyPart() {
        Bytes vertexBoneTable{std::byte{0}, std::byte{1}, std::byte{1}};
        vertexBones = add(vertexBoneTable);
        Bytes vertices;
        appendVec3(vertices, {.x = 0.0F, .y = 0.0F, .z = 0.0F});
        appendVec3(vertices, {.x = 8.0F, .y = 0.0F, .z = 8.0F});
        appendVec3(vertices, {.x = 0.0F, .y = 8.0F, .z = 8.0F});
        const std::size_t vertexTable = add(vertices);
        const std::size_t normalBones = add(Bytes{std::byte{1}});
        Bytes normals;
        appendVec3(normals, {.x = 0.0F, .y = 0.0F, .z = 1.0F});
        const std::size_t normalTable = add(normals);

        Bytes commandBytes;
        append(commandBytes, std::int16_t{3}); // a strip of three corners
        for (const std::int16_t vertex : std::to_array<std::int16_t>({0, 1, 2})) {
            append(commandBytes, vertex);
            append(commandBytes, std::int16_t{0}); // normal
            append(commandBytes, static_cast<std::int16_t>(vertex * 4));
            append(commandBytes, std::int16_t{8});
        }
        append(commandBytes, std::int16_t{0}); // the end
        commands = add(commandBytes);

        Bytes meshRecord;
        append(meshRecord, std::int32_t{1});
        append(meshRecord, static_cast<std::int32_t>(commands));
        append(meshRecord, std::int32_t{0}); // skin reference
        append(meshRecord, std::int32_t{1});
        append(meshRecord, std::int32_t{0});
        mesh = add(meshRecord);

        Bytes modelRecord;
        appendName(modelRecord, "triangle", 64);
        append(modelRecord, std::int32_t{0});
        append(modelRecord, 12.0F);
        for (const auto value :
             {1, static_cast<int>(mesh), 3, static_cast<int>(vertexBones), static_cast<int>(vertexTable), 1,
              static_cast<int>(normalBones), static_cast<int>(normalTable), 0, 0}) {
            append(modelRecord, static_cast<std::int32_t>(value));
        }
        const std::size_t model = add(modelRecord);

        Bytes partRecord;
        appendName(partRecord, "body", 64);
        append(partRecord, std::int32_t{1});
        append(partRecord, std::int32_t{1});
        append(partRecord, static_cast<std::int32_t>(model));
        table(204, 1, partRecord);
    }
};

[[nodiscard]] auto parseError(const TriangleModel& model) -> MdlError {
    const auto view = MdlView::parse(model.bytes);
    EXPECT_FALSE(view.has_value());
    return view ? MdlError{} : view.error().code;
}

TEST(MdlTest, ReadsTheSkeletonAndWhatHangsOnIt) {
    const TriangleModel model;
    const auto view = MdlView::parse(model.bytes);
    ASSERT_TRUE(view.has_value()) << nx::describe(view.error());
    EXPECT_EQ(view->name(), "models/triangle.mdl");
    EXPECT_EQ(view->eyePosition(), (nx::Vec3{.x = 0.0F, .y = 0.0F, .z = 28.0F}));
    EXPECT_EQ(view->bounds().max.z, 72.0F);

    ASSERT_EQ(view->bones().size(), 2U);
    EXPECT_FALSE(view->bones()[0].parent.has_value());
    EXPECT_EQ(view->bones()[1].parent, std::optional{BoneIndex{.value = 0}});
    EXPECT_EQ(view->bones()[1].name, "head");
    EXPECT_EQ(view->bones()[1].values[2], 10.0F);
    EXPECT_TRUE(view->bones()[1].controllers[4].has_value());
    EXPECT_FALSE(view->bones()[1].controllers[3].has_value());

    ASSERT_EQ(view->boneControllers().size(), 1U);
    const auto& controller = view->boneControllers()[0];
    EXPECT_EQ(controller.bone.value, 1U);
    EXPECT_EQ(controller.axis, BoneAxis::RotationY);
    EXPECT_TRUE(controller.wraps);
    EXPECT_EQ(controller.end, 30.0F);

    ASSERT_EQ(view->hitboxes().size(), 1U);
    EXPECT_EQ(view->hitboxes()[0].group, 1U);
    EXPECT_EQ(view->hitboxes()[0].bounds.max, (nx::Vec3{.x = 4.0F, .y = 4.0F, .z = 8.0F}));
    ASSERT_EQ(view->attachments().size(), 1U);
    EXPECT_EQ(view->attachments()[0].name, "eyes");
    EXPECT_EQ(view->attachments()[0].origin.z, 4.0F);
}

TEST(MdlTest, ReadsTexturesSkinsAndGeometry) {
    const TriangleModel model;
    const auto view = MdlView::parse(model.bytes);
    ASSERT_TRUE(view.has_value()) << nx::describe(view.error());

    ASSERT_EQ(view->textures().size(), 1U);
    const auto& texture = view->textures()[0];
    EXPECT_EQ(texture.name, "skin.bmp");
    EXPECT_TRUE(texture.flags.test(StudioTextureFlag::Chrome));
    EXPECT_TRUE(texture.flags.test(StudioTextureFlag::Masked));
    EXPECT_FALSE(texture.flags.test(StudioTextureFlag::Additive));
    EXPECT_EQ(texture.pixels.size(), 256U);
    EXPECT_EQ(texture.palette.size(), nx::assets::kPaletteBytes);
    EXPECT_EQ(texture.palette[0], std::byte{9});
    ASSERT_EQ(view->skins().size(), 2U);
    EXPECT_EQ(view->skins()[1].size(), 1U);

    ASSERT_EQ(view->bodyParts().size(), 1U);
    const auto& part = view->bodyParts()[0];
    EXPECT_EQ(part.name, "body");
    ASSERT_EQ(part.models.size(), 1U);
    const auto& triangle = part.models[0];
    EXPECT_EQ(triangle.vertices.size(), 3U);
    EXPECT_EQ(triangle.vertices[1].bone.value, 1U);
    EXPECT_EQ(triangle.normals.size(), 1U);
    ASSERT_EQ(triangle.meshes.size(), 1U);
    ASSERT_EQ(triangle.meshes[0].runs.size(), 1U);
    const auto& run = triangle.meshes[0].runs[0];
    EXPECT_EQ(run.winding, Winding::Strip);
    ASSERT_EQ(run.corners.size(), 3U);
    EXPECT_EQ(run.corners[2].vertex.value, 2U);
    EXPECT_EQ(run.corners[2].s, 8);
}

TEST(MdlTest, AModelWhoseTexturesAreInItsTFileHasNone) {
    TriangleModel model;
    model.put(180, std::int32_t{0}); // no textures
    const auto view = MdlView::parse(model.bytes);
    ASSERT_TRUE(view.has_value()) << nx::describe(view.error());
    EXPECT_TRUE(view->textures().empty());
    EXPECT_TRUE(view->skins().empty());
}

TEST(MdlTest, ReadsTheSequences) {
    const TriangleModel model;
    const auto view = MdlView::parse(model.bytes);
    ASSERT_TRUE(view.has_value()) << nx::describe(view.error());
    ASSERT_EQ(view->sequenceGroups().size(), 2U);
    EXPECT_EQ(view->sequenceGroups()[1].fileName, "models/triangle01.mdl");
    ASSERT_EQ(view->sequences().size(), 2U);
    const auto& idle = view->sequences()[0];
    EXPECT_EQ(idle.label, "idle");
    EXPECT_EQ(idle.fps, 30.0F);
    EXPECT_TRUE(idle.loops);
    EXPECT_EQ(idle.frameCount, 2U);
    EXPECT_EQ(idle.blendCount, 1U);
    EXPECT_EQ(idle.group.value, 0U);
    ASSERT_EQ(idle.events.size(), 1U);
    EXPECT_EQ(idle.events[0].frame, 1);
    EXPECT_EQ(idle.events[0].event, 5004);
    EXPECT_EQ(idle.events[0].options, "player/pl_step1.wav");
    EXPECT_EQ(view->sequences()[1].group.value, 1U);
}

TEST(MdlTest, DecodesAnimationsHereAndInSequenceGroups) {
    const TriangleModel model;
    const auto view = MdlView::parse(model.bytes);
    ASSERT_TRUE(view.has_value()) << nx::describe(view.error());

    const auto idle = view->decodeAnimation(SequenceIndex{.value = 0}, model.bytes);
    ASSERT_TRUE(idle.has_value()) << nx::describe(idle.error());
    EXPECT_EQ(idle->values.size(), 2U * 2U * 6U);
    const BoneIndex root{.value = 0};
    const BoneIndex head{.value = 1};
    EXPECT_EQ(idle->value(0, 0, root, BoneAxis::Z), 5);
    EXPECT_EQ(idle->value(0, 1, root, BoneAxis::Z), 7);
    EXPECT_EQ(idle->value(0, 0, head, BoneAxis::RotationY), 100);
    EXPECT_EQ(idle->value(0, 1, head, BoneAxis::RotationY), 100) << "past its values, a run repeats the last";
    EXPECT_EQ(idle->value(0, 1, head, BoneAxis::X), 0) << "an axis without a stream stays at 0";

    const auto wave = view->decodeAnimation(SequenceIndex{.value = 1}, model.groupFile);
    ASSERT_TRUE(wave.has_value()) << nx::describe(wave.error());
    EXPECT_EQ(wave->value(0, 0, head, BoneAxis::RotationY), -3);
}

TEST(MdlTest, RejectsABrokenAnimation) {
    const auto animationError = [](auto change, std::uint32_t sequence = 0) {
        TriangleModel model;
        change(model);
        const auto view = MdlView::parse(model.bytes);
        EXPECT_TRUE(view.has_value()) << nx::describe(view.error());
        if (!view) {
            return MdlError{};
        }
        const auto frames = view->decodeAnimation(SequenceIndex{.value = sequence},
                                                  sequence == 0 ? model.bytes : model.groupFile);
        EXPECT_FALSE(frames.has_value());
        return frames ? MdlError{} : frames.error().code;
    };
    EXPECT_EQ(animationError([](TriangleModel&) {}, 2), MdlError::BadIndex) << "sequence 2 of 2";
    EXPECT_EQ(animationError([](TriangleModel& model) { model.bytes[model.animation + 24] = std::byte{0}; }),
              MdlError::BadValue)
        << "a run of no value";
    EXPECT_EQ(
        animationError([](TriangleModel& model) { model.put(model.animation + 4, std::uint16_t{60000}); }),
        MdlError::OutOfBounds);
    EXPECT_EQ(animationError([](TriangleModel& model) { model.groupFile[3] = std::byte{'T'}; }, 1),
              MdlError::NotSequenceGroup);
    EXPECT_EQ(
        animationError([](TriangleModel& model) { model.put(model.sequences + 56, std::int32_t{1 << 30}); }),
        MdlError::BadValue)
        << "more than kMaxAnimationValues";
}

TEST(MdlTest, RejectsABrokenSequence) {
    const auto withChange = [](auto change) {
        TriangleModel model;
        change(model);
        return parseError(model);
    };
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.sequences + 56, std::int32_t{0}); }),
              MdlError::BadValue)
        << "no frame";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.sequences + 120, std::int32_t{0}); }),
              MdlError::BadValue)
        << "no blend";
    EXPECT_EQ(withChange([](TriangleModel& model) {
                  model.put(model.sequences + 32, std::numeric_limits<float>::quiet_NaN());
              }),
              MdlError::BadValue);
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.sequences + 156, std::int32_t{2}); }),
              MdlError::BadIndex)
        << "group 2 of 2";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.sequences + 72, std::int32_t{2}); }),
              MdlError::BadIndex)
        << "motion bone 2 of 2";
    EXPECT_EQ(
        withChange([](TriangleModel& model) { model.put(model.sequences + 52, std::int32_t{1 << 24}); }),
        MdlError::OutOfBounds)
        << "events past the file";
}

TEST(MdlTest, RejectsABrokenHeader) {
    EXPECT_EQ(MdlView::parse(Bytes(243)).error().code, MdlError::TooSmall);
    TriangleModel idsq;
    idsq.bytes[3] = std::byte{'Q'};
    EXPECT_EQ(parseError(idsq), MdlError::NotStudioModel);
    TriangleModel version;
    version.put(4, std::int32_t{6});
    EXPECT_EQ(parseError(version), MdlError::UnsupportedVersion);
    TriangleModel tooManyBones;
    tooManyBones.put(140, std::int32_t{1000});
    EXPECT_EQ(parseError(tooManyBones), MdlError::OutOfBounds);
    TriangleModel eyes;
    eyes.put(76, std::numeric_limits<float>::infinity());
    EXPECT_EQ(parseError(eyes), MdlError::BadValue);
}

TEST(MdlTest, RejectsABrokenRecord) {
    const auto withChange = [](auto change) {
        TriangleModel model;
        change(model);
        return parseError(model);
    };
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.bones + 112 + 32, std::int32_t{1}); }),
              MdlError::BadIndex)
        << "a bone that is its own parent";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.bones + 112 + 40, std::int32_t{1}); }),
              MdlError::BadIndex)
        << "controller 1 of 1";
    EXPECT_EQ(withChange([](TriangleModel& model) {
                  model.put(model.bones + 64, std::numeric_limits<float>::quiet_NaN());
              }),
              MdlError::BadValue);
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.controller + 4, std::uint32_t{0x3}); }),
              MdlError::BadValue)
        << "two axes";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.controller + 4, std::uint32_t{0x40}); }),
              MdlError::BadValue)
        << "no axis of the six";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.controller, std::int32_t{2}); }),
              MdlError::BadIndex)
        << "bone 2 of 2";
    EXPECT_EQ(withChange([](TriangleModel& model) {
                  model.put(model.hitbox + 8, std::numeric_limits<float>::infinity());
              }),
              MdlError::BadValue);
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.texture + 68, std::int32_t{0}); }),
              MdlError::BadValue)
        << "a texture of no width";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.texture + 76, std::int32_t{1 << 24}); }),
              MdlError::OutOfBounds);
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.skins + 2, std::int16_t{1}); }),
              MdlError::BadIndex)
        << "texture 1 of 1";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(192, std::int32_t{0}); }), MdlError::BadValue)
        << "no skin reference";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.mesh + 8, std::int32_t{1}); }),
              MdlError::BadIndex)
        << "skin reference 1 of 1";
}

TEST(MdlTest, RejectsBrokenGeometry) {
    const auto withChange = [](auto change) {
        TriangleModel model;
        change(model);
        return parseError(model);
    };
    EXPECT_EQ(withChange([](TriangleModel& model) { model.bytes[model.vertexBones + 2] = std::byte{2}; }),
              MdlError::BadIndex)
        << "bone 2 of 2";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.commands, std::int16_t{2}); }),
              MdlError::BadValue)
        << "a run of two corners";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.commands + 2 + 8, std::int16_t{3}); }),
              MdlError::BadIndex)
        << "vertex 3 of 3";
    EXPECT_EQ(withChange([](TriangleModel& model) { model.put(model.commands + 2 + 2, std::int16_t{1}); }),
              MdlError::BadIndex)
        << "normal 1 of 1";
    EXPECT_EQ(
        withChange([](TriangleModel& model) { model.put(model.commands + 2 + 24, std::int16_t{32767}); }),
        MdlError::OutOfBounds)
        << "a run past the end of the file";
}

} // namespace
