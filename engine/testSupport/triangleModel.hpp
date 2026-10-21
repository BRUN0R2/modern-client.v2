// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Test helper: a small MDL v10 file built byte by byte, which the reader's tests break record by
// record and the renderer's tests draw. Include after `import std;` and `import nx.math;`.

namespace nx::test {

using Bytes = std::vector<std::byte>;

template <class Value>
void append(Bytes& bytes, Value value) {
    const auto raw =
        std::bit_cast<std::array<std::byte, sizeof(Value)>>(value); // the tests run on little-endian hosts
    bytes.insert(bytes.end(), raw.begin(), raw.end());
}

inline void appendName(Bytes& bytes, std::string_view name, std::size_t size) {
    for (std::size_t index = 0; index < size; ++index) {
        bytes.push_back(index < name.size() ? static_cast<std::byte>(name[index]) : std::byte{0});
    }
}

inline void appendVec3(Bytes& bytes, nx::Vec3 vector) {
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

} // namespace nx::test
