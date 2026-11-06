// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// What the game gives navigation: the repository's data/cstrike/navigation.toml and what its reader
// refuses, and the geometry of the box maps of the fuzz corpus, the world's faces and a solid brush
// entity's, facing where they do, under water or not.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.ecs;
import nx.assets;
import nx.collision;
import nx.navigation;
import nx.game;

namespace {

using nx::Vec3;
using nx::game::GameError;
using nx::game::NavigationData;
using nx::navigation::AreaKind;
using nx::navigation::Geometry;
using nx::navigation::GeometryTriangle;

[[nodiscard]] auto readSeed(std::string_view name) -> std::vector<std::byte> {
    auto bytes = nx::platform::readFile(std::filesystem::path{NX_FUZZ_CORPUS_DIRECTORY} / "bsp" / name);
    EXPECT_TRUE(bytes.has_value());
    return bytes.value_or(std::vector<std::byte>{});
}

/// Makes the brush entities of `map` in `world`, as the game makes them by the repository's classes.
void spawnEntitiesOf(const nx::assets::BspView& map, nx::ecs::World& world) {
    EXPECT_TRUE(nx::game::registerComponents(world).has_value());
    auto text =
        nx::platform::readTextFile(std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "entities.toml");
    EXPECT_TRUE(text.has_value());
    const auto classes = nx::game::readEntityClasses(text.value_or(""), "entities.toml");
    EXPECT_TRUE(classes.has_value());
    EXPECT_TRUE(nx::game::spawnBrushEntities(world, map.entities(), map.models(),
                                             classes.value_or(nx::game::EntityClasses{}))
                    .has_value());
}

/// The unit normal of `triangle` by the right-hand rule.
[[nodiscard]] auto normalOf(const Geometry& geometry, const GeometryTriangle& triangle) -> Vec3 {
    const Vec3 a = geometry.vertices[triangle.corners[0]];
    return nx::normalized(
        nx::cross(geometry.vertices[triangle.corners[1]] - a, geometry.vertices[triangle.corners[2]] - a));
}

/// How many triangles of `geometry` face `facing`, and are of `kind`.
[[nodiscard]] auto countFacing(const Geometry& geometry, Vec3 facing, AreaKind kind = AreaKind::Floor)
    -> std::ptrdiff_t {
    return std::ranges::count_if(geometry.triangles, [&](const GeometryTriangle& triangle) {
        return triangle.kind == kind && nx::dot(normalOf(geometry, triangle), facing) > 0.99F;
    });
}

TEST(NavigationDataTest, TheRepositorysPlayerProfileIsRead) {
    const std::filesystem::path file =
        std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "navigation.toml";
    auto text = nx::platform::readTextFile(file);
    ASSERT_TRUE(text.has_value());
    const auto read = nx::game::readNavigationData(*text, "navigation.toml");
    ASSERT_TRUE(read.has_value()) << nx::describe(read.error());
    ASSERT_EQ(read->profiles.size(), 1U);
    const nx::navigation::AgentProfile& player = read->profiles.front();
    EXPECT_EQ(player.name, "player");
    EXPECT_EQ(player.jumpHeight, nx::HammerUnits{41.8F});
    EXPECT_EQ(player.crouchedJumpHeight, nx::HammerUnits{62.5F});
    EXPECT_EQ(player.dropHeight, nx::HammerUnits{156.25F});
    EXPECT_EQ(player.leapSpeed, nx::UnitsPerSecond{230.0F});
    EXPECT_EQ(player.gravity, nx::UnitsPerSecondSquared{800.0F});
    const nx::navigation::AreaCosts costs = nx::navigation::areaCostsOf(player);
    EXPECT_EQ(costs[AreaKind::Floor], 1.0F);
    EXPECT_EQ(costs[AreaKind::Crouch], 3.0F);
    EXPECT_EQ(costs[AreaKind::Water], 1.25F);
    EXPECT_EQ(costs[AreaKind::Edge], 2.0F);
    EXPECT_EQ(costs[AreaKind::Jump], 2.0F);
    EXPECT_EQ(costs[AreaKind::CrouchedJump], 2.0F);
    EXPECT_EQ(costs[AreaKind::Drop], 1.0F);
    EXPECT_EQ(costs[AreaKind::Door], 4.0F);
    EXPECT_EQ(player.wallClearance, nx::HammerUnits{16.0F});
    EXPECT_EQ(read->build.cellSize, nx::HammerUnits{4.0F});
    EXPECT_EQ(read->build.tileCells, 64U);
}

TEST(NavigationDataTest, AProfileTwiceOrBreakingItsRulesIsRefused) {
    constexpr std::string_view kBuild = R"(
[build]
cellSize = 4.0
cellHeight = 2.0
tileCells = 64
smallestIsland = 32.0
smallestRegion = 80.0
longestEdge = 96.0
edgeError = 5.0
detailSampleDistance = 24.0
detailHeightError = 2.0
)";
    constexpr std::string_view kPlayer = R"(
[[profiles]]
name = "player"
radius = 16.0
wallClearance = 0.0
standingHeight = 72.0
crouchedHeight = 36.0
stepHeight = 18.0
jumpHeight = 41.8
dropHeight = 156.25
leapSpeed = 230.0
gravity = 800.0
minGroundNormalZ = 0.7
areaCosts = []
)";
    constexpr std::string_view kCheap = R"(
[[profiles]]
name = "cheap"
radius = 16.0
wallClearance = 0.0
standingHeight = 72.0
crouchedHeight = 36.0
stepHeight = 18.0
jumpHeight = 41.8
dropHeight = 156.25
leapSpeed = 230.0
gravity = 800.0
minGroundNormalZ = 0.7
areaCosts = [{ area = "water", cost = 0.5 }]
)";
    EXPECT_TRUE(
        nx::game::readNavigationData(std::format("{}{}", kPlayer, kBuild), "navigation.toml").has_value());
    for (const std::string& text : {std::format("{}{}{}", kPlayer, kPlayer, kBuild),
                                    std::format("{}{}", kCheap, kBuild), std::format("{}", kPlayer)}) {
        const auto read = nx::game::readNavigationData(text, "navigation.toml");
        ASSERT_FALSE(read.has_value()) << text;
        EXPECT_EQ(read.error().code, GameError::ConfigInvalid);
    }
}

TEST(NavigationGeometryTest, TheWorldsFacesFaceWhereTheyLookButTheSkys) {
    const std::vector<std::byte> bytes = readSeed("boxWithEntity.bsp");
    const auto map = nx::assets::BspView::parse(bytes);
    ASSERT_TRUE(map.has_value()) << nx::describe(map.error());
    nx::ecs::World empty;
    ASSERT_TRUE(nx::game::registerComponents(empty).has_value());
    const Geometry geometry = nx::game::navigationGeometryOf(*map, empty);
    // The box's floor, and two of its walls; its ceiling and its other walls are special, left out.
    EXPECT_EQ(geometry.triangles.size(), 6U);
    EXPECT_EQ(countFacing(geometry, {.z = 1.0F}), 2);
    EXPECT_EQ(countFacing(geometry, {.x = 1.0F}), 2);
    EXPECT_EQ(countFacing(geometry, {.y = 1.0F}), 2);
    for (const Vec3 vertex : geometry.vertices) {
        EXPECT_TRUE(vertex.x >= 0.0F && vertex.x <= 64.0F && vertex.z >= 0.0F && vertex.z <= 64.0F);
    }
}

TEST(NavigationGeometryTest, ASolidBrushEntityAddsItsFacesWhereItIs) {
    const std::vector<std::byte> bytes = readSeed("boxWithEntity.bsp");
    const auto map = nx::assets::BspView::parse(bytes);
    ASSERT_TRUE(map.has_value());
    nx::ecs::World world;
    spawnEntitiesOf(*map, world);
    const Geometry geometry = nx::game::navigationGeometryOf(*map, world);
    // The func_wall's face, at x = 16, facing +x besides the world's wall at x = 0.
    EXPECT_EQ(geometry.triangles.size(), 8U);
    EXPECT_EQ(countFacing(geometry, {.x = 1.0F}), 4);
    EXPECT_TRUE(std::ranges::any_of(geometry.vertices, [](Vec3 vertex) { return vertex.x == 16.0F; }));
    EXPECT_EQ(geometry.brushBodyHulls.size(), 1U);
    EXPECT_EQ(geometry.crouchedBrushBodyHulls.size(), 1U);
}

TEST(NavigationGatesTest, ADoorIsOpenWalkedIntoAndClosedWithWhatOpensItWhenOthersOrAUseOpenIt) {
    const std::vector<std::byte> bytes = readSeed("boxWithEntity.bsp");
    const auto map = nx::assets::BspView::parse(bytes);
    ASSERT_TRUE(map.has_value()) << nx::describe(map.error());
    nx::ecs::World world;
    ASSERT_TRUE(nx::game::registerComponents(world).has_value());
    using KeyValues = std::vector<std::pair<std::string, std::string>>;
    constexpr std::string_view kUseOnlyFlag = "256";
    const auto doorWith = [](KeyValues extra) {
        KeyValues keys{{"classname", "func_door"}, {"model", "*1"}, {"angles", "0 0 0"}};
        keys.insert(keys.end(), extra.begin(), extra.end());
        return nx::assets::BspEntity{.keyValues = std::move(keys)};
    };
    const std::vector entities{doorWith({{"targetname", "vault"}}), doorWith({}),
                               doorWith({{"spawnflags", std::string{kUseOnlyFlag}}}),
                               nx::assets::BspEntity{.keyValues = {{"classname", "func_button"},
                                                                   {"model", "*1"},
                                                                   {"angles", "0 0 0"},
                                                                   {"target", "vault"}}}};
    const nx::game::EntityClasses classes{
        .brushClasses = {nx::game::BrushClass{.classname = "func_button",
                                              .solid = nx::game::Solid::Bsp,
                                              .behavior = nx::game::BrushBehavior::Button},
                         nx::game::BrushClass{.classname = "func_door",
                                              .solid = nx::game::Solid::Bsp,
                                              .behavior = nx::game::BrushBehavior::SlidingDoor}}};
    ASSERT_TRUE(nx::game::spawnBrushEntities(world, entities, map->models(), classes).has_value());

    const std::vector<nx::navigation::Gate> gates = nx::game::gatesOf(*map, world);
    ASSERT_EQ(gates.size(), 3U) << "the doors, not the button";
    const auto openWalkedInto = std::ranges::count_if(
        gates, [](const nx::navigation::Gate& gate) { return gate.isOpen && gate.openers.empty(); });
    EXPECT_EQ(openWalkedInto, 1);
    const auto closed = std::ranges::count_if(
        gates, [](const nx::navigation::Gate& gate) { return !gate.isOpen && gate.openers.size() == 1; });
    EXPECT_EQ(closed, 2) << "by a button, and by a use of itself";
    EXPECT_TRUE(std::ranges::any_of(gates, [](const nx::navigation::Gate& gate) {
        return !gate.openers.empty() && gate.openers.front() == gate.middle;
    }));

    nx::ecs::Query<nx::game::Door&>{world}.forEach(
        [](nx::game::Door& door) { door.state = nx::game::DoorState::Open; });
    EXPECT_TRUE(std::ranges::all_of(nx::game::gatesOf(*map, world), &nx::navigation::Gate::isOpen))
        << "open, however it opens";

    const Geometry geometry = nx::game::navigationGeometryOf(*map, world);
    EXPECT_EQ(geometry.doors.size(), 3U);
}

TEST(NavigationGeometryTest, AFloorUnderWaterIsWater) {
    // The box's room made water: the contents of its empty leaf, leaf 1.
    std::vector<std::byte> bytes = readSeed("boxEmbedded.bsp");
    constexpr std::size_t kLeavesLump = 10;
    constexpr std::size_t kLumpDirectory = 4;
    constexpr std::size_t kLumpEntry = 8;
    constexpr std::size_t kLeafSize = 28;
    const auto leaves = nx::loadLittleEndian<std::int32_t>(
        std::span<const std::byte>{bytes}.subspan(kLumpDirectory + (kLeavesLump * kLumpEntry)));
    constexpr std::array kWater{std::byte{0xFD}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}}; // -3
    std::ranges::copy(kWater, bytes.begin() + leaves + static_cast<std::ptrdiff_t>(kLeafSize));
    const auto map = nx::assets::BspView::parse(bytes);
    ASSERT_TRUE(map.has_value()) << nx::describe(map.error());
    nx::ecs::World empty;
    ASSERT_TRUE(nx::game::registerComponents(empty).has_value());
    const Geometry geometry = nx::game::navigationGeometryOf(*map, empty);
    EXPECT_EQ(countFacing(geometry, {.z = 1.0F}, AreaKind::Water), 2);
    EXPECT_EQ(countFacing(geometry, {.z = 1.0F}, AreaKind::Floor), 0);
}

TEST(NavigationGeometryTest, ItsBodyHullsAreTheWorldsStandingAndDuckedOnes) {
    const std::vector<std::byte> bytes = readSeed("boxWithEntity.bsp");
    const auto map = nx::assets::BspView::parse(bytes);
    ASSERT_TRUE(map.has_value()) << nx::describe(map.error());
    const auto hulls = nx::collision::BrushHulls::create(*map);
    ASSERT_TRUE(hulls.has_value());
    nx::ecs::World empty;
    ASSERT_TRUE(nx::game::registerComponents(empty).has_value());
    const Geometry geometry = nx::game::navigationGeometryOf(*map, empty);
    constexpr int kSamples = 16;
    constexpr float kSpan = 256.0F;
    constexpr float kLeast = -96.0F;
    const auto solidOfSameAs = [&](nx::collision::Hull hull, const nx::navigation::BodyHull& made) {
        std::ptrdiff_t solid = 0;
        std::ptrdiff_t open = 0;
        for (int x = 0; x < kSamples; ++x) {
            for (int y = 0; y < kSamples; ++y) {
                for (int z = 0; z < kSamples; ++z) {
                    const Vec3 point = Vec3{.x = kLeast, .y = kLeast, .z = kLeast} +
                                       (Vec3{.x = static_cast<float>(x),
                                             .y = static_cast<float>(y),
                                             .z = static_cast<float>(z)} *
                                        (kSpan / static_cast<float>(kSamples)));
                    const bool isSolid = hulls->contentsAt(nx::collision::kWorldModel, hull, point) ==
                                         nx::collision::Contents::Solid;
                    EXPECT_EQ(nx::navigation::isSolidAt(made, point), isSolid) << std::format("{}", point);
                    ++(isSolid ? solid : open);
                }
            }
        }
        EXPECT_GT(open, 0);
        return solid;
    };
    // This map's ducked hull has no solid, which tells the two apart.
    EXPECT_GT(solidOfSameAs(nx::collision::Hull::Standing, geometry.bodyHull), 0);
    EXPECT_EQ(solidOfSameAs(nx::collision::Hull::Ducked, geometry.crouchedBodyHull), 0);
}

} // namespace
