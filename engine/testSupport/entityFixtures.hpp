// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Synthetic entity content. Include after importing cs.shared, nx.math and nx.platform.
#include "temporaryDirectory.hpp"
#include "triangleModel.hpp"

namespace nx::test {
[[nodiscard]] inline auto entityDefinition() -> cs::EntityDefinition {
    return {.id = "syntheticEntity",
            .model = "models/entity.mdl",
            .health = nx::HitPoints{100},
            .speed = nx::UnitsPerSecond{160},
            .meleeDamage = nx::Damage{20},
            .meleeReach = nx::HammerUnits{64},
            .meleeInterval = nx::Seconds{1},
            .idleSequence = "idle",
            .walkSequence = "idle",
            .attackSequence = "wave",
            .deathSequence = "wave",
            .hitGroups = {.head = cs::DamageShare{2.0}}};
}

inline void writeEntityModel(const TemporaryDirectory& directory) {
    TriangleModel model;
    model.put(model.hitbox + 8, nx::Vec3{.x = -8, .y = -8, .z = -40});
    model.put(model.hitbox + 20, nx::Vec3{.x = 8, .y = 8, .z = 20});
    // The second sequence is a one-shot attack/death; the idle sequence keeps looping.
    constexpr std::size_t kSequenceRecordSize = 176;
    constexpr std::size_t kSequenceFlagsOffset = 36;
    model.put(model.sequences + kSequenceRecordSize + kSequenceFlagsOffset, std::int32_t{0});
    const auto textOf = [](const Bytes& bytes) {
        return bytes | std::views::transform([](std::byte value) { return static_cast<char>(value); }) |
               std::ranges::to<std::string>();
    };
    directory.write("models/entity.mdl", textOf(model.bytes));
    directory.write("models/triangle01.mdl", textOf(model.groupFile));
}
} // namespace nx::test
