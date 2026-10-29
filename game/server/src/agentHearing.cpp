// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.navigation;
import nx.agents;
import cs.shared;

namespace cs {

namespace {

[[nodiscard]] auto isPositive(float value) noexcept -> bool {
    return std::isfinite(value) && value > 0.0F;
}

} // namespace

auto isValid(const HearingRules& rules) noexcept -> bool {
    return isPositive(rules.footstep.value()) && isPositive(rules.landing.value()) &&
           isPositive(rules.shot.value()) && isPositive(rules.quietShot.value()) &&
           isPositive(rules.emptyShot.value()) && std::isfinite(rules.errorShare) && rules.errorShare >= 0.0F;
}

auto rangeOf(const HearingRules& rules, NoiseKind kind) noexcept -> nx::HammerUnits {
    switch (kind) {
    case NoiseKind::Footstep:
        return rules.footstep;
    case NoiseKind::Landing:
        return rules.landing;
    case NoiseKind::Shot:
        return rules.shot;
    case NoiseKind::QuietShot:
        return rules.quietShot;
    case NoiseKind::EmptyShot:
        return rules.emptyShot;
    case NoiseKind::Count:
        break;
    }
    return nx::HammerUnits{};
}

auto heardBy(const Listener& listener, std::span<const GameNoise> noises, const HearingRules& rules,
             nx::navigation::NavigationQuery& query, nx::Random& random)
    -> std::vector<nx::agents::Observation> {
    // The nearest noise of each enemy heard, in the order of the noises.
    std::vector<std::pair<const GameNoise*, float>> heard;
    for (const GameNoise& noise : noises) {
        if (listener.team == noise.team) {
            continue;
        }
        const float distance = nx::length(noise.eye - listener.ear);
        if (distance >= rangeOf(rules, noise.kind).value() * noise.loudness) {
            continue;
        }
        const auto same =
            std::ranges::find(heard, noise.entity, [](const auto& entry) { return entry.first->entity; });
        if (same == heard.end()) {
            heard.emplace_back(&noise, distance);
        } else if (distance < same->second) {
            *same = {&noise, distance};
        }
    }
    std::vector<nx::agents::Observation> observations;
    observations.reserve(heard.size());
    for (const auto& [noise, distance] : heard) {
        const float error = rules.errorShare * distance;
        const nx::Vec3 offset{.x = random.between(-error, error), .y = random.between(-error, error)};
        // Kept on the mesh from where it was made, as OnEvent keeps it on the noise's own area; off by its
        // error all the same where it was made off the mesh.
        const auto walked = query.raycast(noise->feet, noise->feet + offset);
        const nx::Vec3 position = walked ? walked->end : noise->feet + offset;
        observations.push_back(
            {.entity = noise->entity, .position = position, .aimAt = position + (noise->eye - noise->feet)});
    }
    return observations;
}

} // namespace cs
