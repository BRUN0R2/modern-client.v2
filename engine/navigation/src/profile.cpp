// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.navigation;

import std;
import nx.core;
import nx.math;

namespace nx::navigation {

namespace {

/// The most voxels of room above a floor Recast counts (rcCompactSpan::h, 8 bits): a standing height
/// taller than that could not be told from a crouched one.
constexpr float kMostRoomVoxels = 255.0F;

[[nodiscard]] auto invalidProfile(const AgentProfile& profile, std::string_view problem)
    -> std::unexpected<Failure<MeshBuildError>> {
    return fail(MeshBuildError::InvalidProfile, std::format("profile {:?}: {}", profile.name, problem));
}

/// Whether `value` is more than 0: a NaN is not.
[[nodiscard]] auto isPositive(HammerUnits value) noexcept -> bool {
    return value > HammerUnits{};
}

/// Whether `value` is 0 or more: a NaN is not.
[[nodiscard]] auto isNotNegative(HammerUnits value) noexcept -> bool {
    return value >= HammerUnits{};
}

} // namespace

auto areaCostsOf(const AgentProfile& profile) -> AreaCosts {
    AreaCosts costs{1.0F};
    for (const AreaCost& listed : profile.areaCosts) {
        costs[listed.area] = listed.cost;
    }
    return costs;
}

auto checkSettings(const BuildSettings& settings) -> Result<void, MeshBuildError> {
    if (!isPositive(settings.cellSize) || !isPositive(settings.cellHeight) || settings.tileCells == 0) {
        return fail(MeshBuildError::InvalidSettings,
                    std::format("cells of {} by {} in tiles of {}: each must be more than 0",
                                settings.cellSize, settings.cellHeight, settings.tileCells));
    }
    for (const auto& [name, value] :
         {std::pair{"smallestIsland", settings.smallestIsland},
          std::pair{"smallestRegion", settings.smallestRegion},
          std::pair{"longestEdge", settings.longestEdge}, std::pair{"edgeError", settings.edgeError},
          std::pair{"detailSampleDistance", settings.detailSampleDistance},
          std::pair{"detailHeightError", settings.detailHeightError}}) {
        if (!isNotNegative(value)) {
            return fail(MeshBuildError::InvalidSettings, std::format("{} is {}, less than 0", name, value));
        }
    }
    return {};
}

auto checkProfile(const AgentProfile& profile, const BuildSettings& settings)
    -> Result<void, MeshBuildError> {
    if (profile.name.empty()) {
        return invalidProfile(profile, "no name");
    }
    if (!isPositive(profile.radius) || !isPositive(profile.crouchedHeight) ||
        !isPositive(profile.stepHeight)) {
        return invalidProfile(profile,
                              std::format("a radius of {}, crouched {} tall, steps of {}: each must be "
                                          "more than 0",
                                          profile.radius, profile.crouchedHeight, profile.stepHeight));
    }
    if (!(profile.standingHeight >= profile.crouchedHeight) || !(profile.jumpHeight >= profile.stepHeight)) {
        return invalidProfile(profile,
                              std::format("{} tall standing and {} crouched, jumping {} and stepping {}: "
                                          "neither may be the less",
                                          profile.standingHeight, profile.crouchedHeight, profile.jumpHeight,
                                          profile.stepHeight));
    }
    if (!(profile.minGroundNormalZ > 0.0F && profile.minGroundNormalZ <= 1.0F)) {
        return invalidProfile(profile, std::format("a floor's normal of {}, not more than 0 up to 1",
                                                   profile.minGroundNormalZ));
    }
    if (profile.standingHeight.value() / settings.cellHeight.value() > kMostRoomVoxels) {
        return invalidProfile(profile,
                              std::format("{} tall, more than {} voxels of {}", profile.standingHeight,
                                          kMostRoomVoxels, settings.cellHeight));
    }
    EnumArray<AreaKind, bool> isListed{false};
    for (const AreaCost& listed : profile.areaCosts) {
        if (isListed[listed.area]) {
            return invalidProfile(profile, std::format("the cost of {} twice", enumName(listed.area)));
        }
        isListed[listed.area] = true;
        if (!(listed.cost >= 1.0F) || !std::isfinite(listed.cost)) {
            return invalidProfile(profile, std::format("{} costs {}, not a number of 1 or more",
                                                       enumName(listed.area), listed.cost));
        }
    }
    return {};
}

void addConvexPolygon(Geometry& geometry, std::span<const Vec3> corners, AreaKind kind) {
    if (corners.size() < 3) {
        return;
    }
    const auto first = narrow<std::uint32_t>(geometry.vertices.size());
    geometry.vertices.insert(geometry.vertices.end(), corners.begin(), corners.end());
    for (std::uint32_t corner = 1; corner + 1 < corners.size(); ++corner) {
        geometry.triangles.push_back(
            GeometryTriangle{.corners = {first, first + corner, first + corner + 1}, .kind = kind});
    }
}

} // namespace nx::navigation
