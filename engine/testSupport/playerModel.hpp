// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Test helper: an MDL v10 file shaped as a player's model, built byte by byte: the bones a player's
// model has its gait split at (Bip01, Bip01 Pelvis, Bip01 Spine, and a leg, Bip01 L Thigh, a child of
// the pelvis), and the sequences a test gives, each with its label, activity, frames, blends and how far
// it moves the model; each blend of a sequence holds each bone at a position along X of its own, the
// whole sequence long. Include after `import std;` and `import nx.math;`.

#include "triangleModel.hpp" // the writing of an MDL's records

namespace nx::test {

/// The bones of a PlayerModel, in their order.
enum class PlayerBone : std::uint8_t {
    Root,   ///< Bip01.
    Pelvis, ///< Bip01 Pelvis, a child of the root.
    Spine,  ///< Bip01 Spine, a child of the pelvis.
    Thigh,  ///< Bip01 L Thigh, a child of the pelvis.
};

inline constexpr std::array<std::string_view, 4> kPlayerBoneNames{"Bip01", "Bip01 Pelvis", "Bip01 Spine",
                                                                  "Bip01 L Thigh"};
inline constexpr std::array<std::int32_t, 4> kPlayerBoneParents{-1, 0, 1, 1};

/// A sequence of a PlayerModel: its label, its activity (ACT_*), its frames, its looping, how far it
/// moves the model along X, and each blend's positions of the bones along X, by PlayerBone.
struct PlayerSequence {
    std::string label{};
    std::int32_t activity = 0;
    std::int32_t frames = 1;
    bool loops = true;
    float linearMovement = 0.0F;
    std::vector<std::array<std::int16_t, 4>> blends{{}};
};

/// The bytes of a player's model of `sequences`, in their order.
[[nodiscard]] inline auto playerModelOf(std::span<const PlayerSequence> sequences) -> Bytes {
    constexpr std::size_t kHeaderSize = 244;
    constexpr std::size_t kBoneCount = kPlayerBoneNames.size();
    constexpr std::size_t kAxes = 6;
    constexpr std::size_t kAnimationRecordSize = kAxes * sizeof(std::uint16_t);
    Bytes bytes(kHeaderSize);
    const auto put = [&bytes](std::size_t offset, std::int32_t value) {
        std::ranges::copy(std::bit_cast<std::array<std::byte, sizeof(value)>>(value),
                          bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    };
    const auto table = [&bytes, &put](std::size_t countOffset, std::size_t count, const Bytes& records) {
        const std::size_t offset = bytes.size();
        bytes.insert(bytes.end(), records.begin(), records.end());
        put(countOffset, static_cast<std::int32_t>(count));
        put(countOffset + 4, static_cast<std::int32_t>(offset));
    };
    std::ranges::copy(std::array{std::byte{'I'}, std::byte{'D'}, std::byte{'S'}, std::byte{'T'}},
                      bytes.begin());
    put(4, 10);

    Bytes boneRecords;
    for (std::size_t bone = 0; bone < kBoneCount; ++bone) {
        appendName(boneRecords, kPlayerBoneNames.at(bone), 32);
        append(boneRecords, kPlayerBoneParents.at(bone));
        append(boneRecords, std::int32_t{0});
        for (std::size_t axis = 0; axis < kAxes; ++axis) {
            append(boneRecords, std::int32_t{-1});
        }
        for (std::size_t axis = 0; axis < kAxes; ++axis) {
            append(boneRecords, 0.0F);
        }
        for (std::size_t axis = 0; axis < kAxes; ++axis) {
            append(boneRecords, 1.0F);
        }
    }
    table(140, kBoneCount, boneRecords);

    Bytes groups;
    appendName(groups, "default", 32);
    appendName(groups, "", 64);
    append(groups, std::int32_t{0});
    append(groups, std::int32_t{0});
    table(172, 1, groups);

    // Each sequence's animation: the records of every blend's bones, then a stream each: one value over
    // all the frames, along X.
    std::vector<std::size_t> animations;
    for (const PlayerSequence& sequence : sequences) {
        const std::size_t records = bytes.size();
        animations.push_back(records);
        const std::size_t recordCount = sequence.blends.size() * kBoneCount;
        bytes.resize(records + (recordCount * kAnimationRecordSize));
        for (std::size_t record = 0; record < recordCount; ++record) {
            const std::size_t recordAt = records + (record * kAnimationRecordSize);
            const auto offset = static_cast<std::uint16_t>(bytes.size() - recordAt);
            std::ranges::copy(std::bit_cast<std::array<std::byte, sizeof(offset)>>(offset),
                              bytes.begin() + static_cast<std::ptrdiff_t>(recordAt));
            bytes.push_back(std::byte{1});
            bytes.push_back(static_cast<std::byte>(sequence.frames));
            append(bytes, sequence.blends[record / kBoneCount].at(record % kBoneCount));
        }
    }

    Bytes sequenceRecords;
    for (std::size_t index = 0; index < sequences.size(); ++index) {
        const PlayerSequence& sequence = sequences[index];
        appendName(sequenceRecords, sequence.label, 32);
        append(sequenceRecords, 30.0F);                                // fps
        append(sequenceRecords, std::int32_t{sequence.loops ? 1 : 0}); // looping
        append(sequenceRecords, sequence.activity);
        append(sequenceRecords, std::int32_t{1}); // its weight
        append(sequenceRecords, std::int32_t{0}); // events
        append(sequenceRecords, std::int32_t{0});
        append(sequenceRecords, sequence.frames);
        append(sequenceRecords, std::int32_t{0}); // pivots
        append(sequenceRecords, std::int32_t{0});
        append(sequenceRecords, std::int32_t{0}); // motion type
        append(sequenceRecords, std::int32_t{0}); // motion bone
        appendVec3(sequenceRecords, {.x = sequence.linearMovement});
        append(sequenceRecords, std::int32_t{0});
        append(sequenceRecords, std::int32_t{0});
        appendVec3(sequenceRecords, {.x = -16.0F, .y = -16.0F, .z = -36.0F});
        appendVec3(sequenceRecords, {.x = 16.0F, .y = 16.0F, .z = 36.0F});
        append(sequenceRecords, static_cast<std::int32_t>(sequence.blends.size()));
        append(sequenceRecords, static_cast<std::int32_t>(animations[index]));
        for (std::size_t field = 0; field < 6; ++field) { // blend types, starts and ends
            append(sequenceRecords, std::int32_t{0});
        }
        append(sequenceRecords, std::int32_t{0});         // blend parent
        append(sequenceRecords, std::int32_t{0});         // group
        for (std::size_t field = 0; field < 4; ++field) { // entry and exit nodes, node flags, next
            append(sequenceRecords, std::int32_t{0});
        }
    }
    table(164, sequences.size(), sequenceRecords);
    return bytes;
}

/// The blends of an aiming sequence of the shape of Counter-Strike's: nine, the spine at 10 times each
/// blend's number along X, the root at kAimRoot (to tell the legs' gait, which takes the root, from it).
inline constexpr std::int16_t kAimRoot = 1000;
[[nodiscard]] inline auto aimBlends() -> std::vector<std::array<std::int16_t, 4>> {
    std::vector<std::array<std::int16_t, 4>> blends;
    for (std::int16_t blend = 0; blend < 9; ++blend) {
        blends.push_back({kAimRoot, 0, static_cast<std::int16_t>(blend * 10), 0});
    }
    return blends;
}

/// Where the gait sequences of playerSequences hold the root and the thigh along X.
inline constexpr std::int16_t kGaitRoot = 5;
inline constexpr std::int16_t kGaitThigh = 7;

/// Sequences of the shape of a Counter-Strike player's model, by their index in it: its first ones, the
/// legs' gait sequences by their activities (ACT_*), the swimming ones at 8 and 9, then the deaths, and
/// the torso's aiming, shooting and reloading ones of the knife and of a pistol held one-handed.
[[nodiscard]] inline auto playerSequences() -> std::vector<PlayerSequence> {
    const std::vector<std::array<std::int16_t, 4>> gait{{kGaitRoot, 0, 0, kGaitThigh}};
    return {
        {.label = "dummy"},
        {.label = "idle1", .activity = 1, .blends = gait},
        {.label = "crouch_idle", .activity = 18, .blends = gait},
        {.label = "walk", .activity = 3, .frames = 10, .linearMovement = 100.0F, .blends = gait},
        {.label = "run", .activity = 4, .frames = 10, .linearMovement = 200.0F, .blends = gait},
        {.label = "crouchrun", .activity = 17, .frames = 10, .linearMovement = 80.0F, .blends = gait},
        {.label = "jump", .activity = 7, .blends = gait},
        {.label = "longjump", .activity = 8, .blends = gait},
        {.label = "swim", .activity = 6},
        {.label = "treadwater", .activity = 50},
        {.label = "death1", .activity = 36, .frames = 5, .loops = false},
        {.label = "head", .activity = 66, .frames = 5, .loops = false},
        {.label = "gutshot", .activity = 68, .frames = 5, .loops = false},
        {.label = "back", .activity = 69, .frames = 5, .loops = false},
        {.label = "death2", .activity = 37, .frames = 5, .loops = false},
        {.label = "death3", .activity = 38, .frames = 5, .loops = false},
        {.label = "chestshot", .activity = 67, .frames = 5, .loops = false},
        {.label = "crouch_die", .frames = 5, .loops = false},
        {.label = "ref_aim_knife", .blends = aimBlends()},
        {.label = "crouch_aim_knife", .blends = aimBlends()},
        {.label = "ref_shoot_knife", .frames = 5, .loops = false, .blends = aimBlends()},
        {.label = "crouch_shoot_knife", .frames = 5, .loops = false, .blends = aimBlends()},
        {.label = "ref_aim_onehanded", .blends = aimBlends()},
        {.label = "crouch_aim_onehanded", .blends = aimBlends()},
        {.label = "ref_shoot_onehanded", .frames = 5, .loops = false, .blends = aimBlends()},
        {.label = "ref_reload_onehanded", .frames = 5, .loops = false, .blends = aimBlends()},
    };
}

/// The index in playerSequences of the sequence labelled `label`.
[[nodiscard]] inline auto playerSequenceIndex(std::string_view label) -> std::uint32_t {
    const std::vector<PlayerSequence> sequences = playerSequences();
    const auto found = std::ranges::find(sequences, label, &PlayerSequence::label);
    return static_cast<std::uint32_t>(found - sequences.begin());
}

} // namespace nx::test
