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
    return std::isfinite(value.value()) && value > HammerUnits{};
}

/// Whether `value` is 0 or more: a NaN is not.
[[nodiscard]] auto isNotNegative(HammerUnits value) noexcept -> bool {
    return std::isfinite(value.value()) && value >= HammerUnits{};
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
        const double cells =
            static_cast<double>(value.value()) / static_cast<double>(settings.cellSize.value());
        if (cells > std::numeric_limits<int>::max()) {
            return fail(MeshBuildError::InvalidSettings,
                        std::format("{} exceeds Recast's integer cell range", name));
        }
    }
    const double largestCellArea = std::numeric_limits<int>::max();
    for (const HammerUnits side : {settings.smallestIsland, settings.smallestRegion}) {
        const double cells =
            static_cast<double>(side.value()) / static_cast<double>(settings.cellSize.value());
        if (cells * cells > largestCellArea) {
            return fail(MeshBuildError::InvalidSettings,
                        "a region's area exceeds Recast's integer cell range");
        }
    }
    if (static_cast<double>(settings.tileCells) * static_cast<double>(settings.cellSize.value()) >
        static_cast<double>(std::numeric_limits<float>::max())) {
        return fail(MeshBuildError::InvalidSettings, "a tile's side exceeds the coordinate range");
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
    if (!isPositive(profile.standingHeight) || !isPositive(profile.jumpHeight)) {
        return invalidProfile(profile, "standing and jump heights must be finite and positive");
    }
    // Recast allocates width * height cells using signed integers; a tile includes its radius
    // and the three-cell watershed border on either side (build.cpp's border pad).
    const double radiusCells = std::ceil(static_cast<double>(profile.radius.value()) /
                                         static_cast<double>(settings.cellSize.value()));
    const double tileWidth =
        static_cast<double>(settings.tileCells) + 2.0 * (radiusCells + detail::borderPadCells);
    if (tileWidth * tileWidth > std::numeric_limits<int>::max() ||
        static_cast<double>(profile.stepHeight.value()) / static_cast<double>(settings.cellHeight.value()) >
            std::numeric_limits<int>::max()) {
        return invalidProfile(profile, "the bordered tile or step exceeds Recast's integer range");
    }
    if (!(profile.minGroundNormalZ > 0.0F && profile.minGroundNormalZ <= 1.0F)) {
        return invalidProfile(profile, std::format("a floor's normal of {}, not more than 0 up to 1",
                                                   profile.minGroundNormalZ));
    }
    if (static_cast<double>(profile.standingHeight.value()) /
            static_cast<double>(settings.cellHeight.value()) >
        static_cast<double>(kMostRoomVoxels)) {
        return invalidProfile(profile,
                              std::format("{} tall, more than {} voxels of {}", profile.standingHeight,
                                          kMostRoomVoxels, settings.cellHeight));
    }
    EnumArray<AreaKind, bool> isListed{false};
    for (const AreaCost& listed : profile.areaCosts) {
        if (std::to_underlying(listed.area) >= std::to_underlying(AreaKind::Count)) {
            return invalidProfile(profile, "an unknown area kind");
        }
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
