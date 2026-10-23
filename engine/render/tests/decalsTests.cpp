// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Decals on the faces of a map's models, without a GPU: the BSP seed of the fuzz corpus, a 64-unit box
// whose one node lies on the floor's plane (z = 0) and lists all six faces. The floor (face 0) is
// CRATE01, its texture's s along x and t along y; faces 2 and 4 are CRATE01 too; faces 1, 3 and 5 are
// {BLUE, masked, and take no decals. The seed with an entity adds a func_wall, model 1: a square of
// CRATE01 at x = 16 (face 6), the one face of its own tree. And the images decals are made of.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.assets;
import nx.render;

#include "contractRecorder.hpp"

namespace {

using nx::render::DecalFace;
using nx::render::DecalPool;
using nx::render::DecalShot;
using nx::render::DecalSize;
using nx::render::PlacedDecal;

constexpr DecalSize kShot{.width = 16, .height = 16};
constexpr std::uint32_t kFloor = 0;
constexpr float kTolerance = 1e-5F;
/// The seed's faces, and its entity's square.
constexpr std::size_t kBoxFaces = 6;
constexpr std::uint32_t kSquare = 6;
constexpr nx::assets::ModelIndex kWall{1};

[[nodiscard]] auto readSeed(std::string_view name) -> std::vector<std::byte> {
    auto bytes = nx::platform::readFile(std::filesystem::path{NX_FUZZ_CORPUS_DIRECTORY} / "bsp" / name);
    EXPECT_TRUE(bytes.has_value()) << nx::describe(bytes.error());
    return bytes.value_or(std::vector<std::byte>{});
}

/// The faces of the box as decals see them, and `more` past them: the CRATE01 ones take them, unlit.
[[nodiscard]] auto boxFaces(std::size_t more = 0) -> std::vector<DecalFace> {
    std::vector<DecalFace> faces(kBoxFaces + more);
    for (const std::size_t crate : {0U, 2U, 4U}) {
        faces[crate].takesDecals = true;
    }
    return faces;
}

class DecalPoolTest : public testing::Test {
protected:
    explicit DecalPoolTest(std::string_view seed = "boxEmbedded.bsp") : m_file{readSeed(seed)} {
        auto map = nx::assets::BspView::parse(m_file);
        EXPECT_TRUE(map.has_value()) << nx::describe(map.error());
        m_map.emplace(std::move(*map));
    }

    [[nodiscard]] auto pool(std::size_t limit = DecalPool::kMostDecals,
                            std::vector<DecalFace> faces = boxFaces()) const -> DecalPool {
        return DecalPool{*m_map, std::move(faces), limit};
    }

    std::vector<std::byte> m_file;
    std::optional<nx::assets::BspView> m_map;
};

/// The bounds of `decal`'s corners, and those of their places on the image.
struct Bounds {
    nx::Aabb positions;
    float leastU = 1.0F;
    float mostU = 0.0F;
    float leastV = 1.0F;
    float mostV = 0.0F;
};

[[nodiscard]] auto boundsOf(const PlacedDecal& decal) -> Bounds {
    Bounds bounds{
        .positions = {.min = decal.polygon.front().position, .max = decal.polygon.front().position}};
    for (const nx::render::DecalVertex& corner : decal.polygon) {
        bounds.positions = bounds.positions.including(corner.position);
        bounds.leastU = std::min(bounds.leastU, corner.u);
        bounds.mostU = std::max(bounds.mostU, corner.u);
        bounds.leastV = std::min(bounds.leastV, corner.v);
        bounds.mostV = std::max(bounds.mostV, corner.v);
    }
    return bounds;
}

TEST_F(DecalPoolTest, ADecalIsCutToTheFaceItLiesOnATexelAUnit) {
    DecalPool decals = pool();
    EXPECT_EQ(decals.shoot({.texture = 3, .size = kShot, .center = {.x = 32.0F, .y = 32.0F, .z = 0.0F}}), 1U);
    EXPECT_EQ(decals.decalsOn(kFloor), 1U);
    const std::vector<const PlacedDecal*> placed = decals.inOrder();
    ASSERT_EQ(placed.size(), 1U);
    EXPECT_EQ(placed.front()->texture, 3U);
    EXPECT_EQ(placed.front()->face, kFloor);
    ASSERT_EQ(placed.front()->polygon.size(), 4U) << "the floor cut to a square";
    const Bounds bounds = boundsOf(*placed.front());
    EXPECT_EQ(bounds.positions.min, (nx::Vec3{.x = 24.0F, .y = 24.0F, .z = 0.0F}));
    EXPECT_EQ(bounds.positions.max, (nx::Vec3{.x = 40.0F, .y = 40.0F, .z = 0.0F}));
    EXPECT_FLOAT_EQ(bounds.leastU, 0.0F);
    EXPECT_FLOAT_EQ(bounds.mostU, 1.0F);
    EXPECT_FLOAT_EQ(bounds.leastV, 0.0F);
    EXPECT_FLOAT_EQ(bounds.mostV, 1.0F);
    // Unlit: no lightmap was given.
    EXPECT_LT(placed.front()->polygon.front().lightmapX, 0.0F);
}

TEST_F(DecalPoolTest, ADecalHalfTwiceAsSmallCoversHalfTheUnits) {
    DecalPool decals = pool();
    ASSERT_EQ(
        decals.shoot(
            {.texture = 0, .size = kShot, .center = {.x = 32.0F, .y = 32.0F, .z = 0.0F}, .scale = 2.0F}),
        1U);
    const Bounds bounds = boundsOf(*decals.inOrder().front());
    EXPECT_EQ(bounds.positions.min, (nx::Vec3{.x = 28.0F, .y = 28.0F, .z = 0.0F}));
    EXPECT_EQ(bounds.positions.max, (nx::Vec3{.x = 36.0F, .y = 36.0F, .z = 0.0F}));
}

TEST_F(DecalPoolTest, AtAFacesEdgeTheDecalIsCutThere) {
    DecalPool decals = pool();
    // 4 units from the y = 0 edge of the floor: its square from y = -4 is cut at 0, a quarter in.
    std::ignore = decals.shoot({.texture = 0, .size = kShot, .center = {.x = 32.0F, .y = 4.0F, .z = 0.0F}});
    ASSERT_EQ(decals.decalsOn(kFloor), 1U);
    const std::vector<const PlacedDecal*> placed = decals.inOrder();
    const auto floor = std::ranges::find(placed, kFloor, &PlacedDecal::face);
    ASSERT_NE(floor, placed.end());
    const Bounds bounds = boundsOf(**floor);
    EXPECT_FLOAT_EQ(bounds.positions.min.y, 0.0F);
    EXPECT_FLOAT_EQ(bounds.positions.max.y, 12.0F);
    EXPECT_NEAR(bounds.leastV, 0.25F, kTolerance);
}

TEST_F(DecalPoolTest, ANodesFacesAreTriedOnlyWhenItsPlanePassesWithinFourUnits) {
    DecalPool decals = pool();
    EXPECT_EQ(decals.shoot({.texture = 0, .size = kShot, .center = {.x = 32.0F, .y = 32.0F, .z = 3.9F}}), 1U)
        << "3.9 units above";
    EXPECT_EQ(decals.shoot({.texture = 0, .size = kShot, .center = {.x = 32.0F, .y = 32.0F, .z = 4.0F}}), 0U)
        << "4 units above";
    EXPECT_EQ(decals.shoot({.texture = 0, .size = kShot, .center = {.x = 32.0F, .y = 32.0F, .z = 20.0F}}), 0U)
        << "past the reach";
}

TEST_F(DecalPoolTest, AFaceThatTakesNoDecalsHasNone) {
    std::vector<DecalFace> faces = boxFaces();
    faces[kFloor].takesDecals = false;
    DecalPool decals = pool(DecalPool::kMostDecals, std::move(faces));
    EXPECT_EQ(decals.shoot({.texture = 0, .size = kShot, .center = {.x = 32.0F, .y = 32.0F, .z = 0.0F}}), 0U);
    EXPECT_TRUE(decals.inOrder().empty());
}

TEST_F(DecalPoolTest, ADecalIsLitAsItsFaceIs) {
    std::vector<DecalFace> faces = boxFaces();
    faces[kFloor].extent =
        nx::render::DecalFaceExtent{.minS = 0.0, .minT = 0.0, .spanS = 64.0, .spanT = 64.0};
    faces[kFloor].lightmapCorner = std::array<std::uint32_t, 2>{100, 7};
    faces[kFloor].styles = {0, 5, 255, 255};
    DecalPool decals = pool(DecalPool::kMostDecals, std::move(faces));
    ASSERT_EQ(decals.shoot({.texture = 0, .size = kShot, .center = {.x = 32.0F, .y = 32.0F, .z = 0.0F}}), 1U);
    for (const nx::render::DecalVertex& corner : decals.inOrder().front()->polygon) {
        // Luxel centers 16 texels apart from texturemins, past the face's corner in the atlas.
        EXPECT_FLOAT_EQ(corner.lightmapX, (corner.position.x / 16.0F) + 0.5F + 100.0F);
        EXPECT_FLOAT_EQ(corner.lightmapY, (corner.position.y / 16.0F) + 0.5F + 7.0F);
        EXPECT_EQ(corner.styles, (std::array<std::uint8_t, 4>{0, 5, 255, 255}));
    }
}

TEST_F(DecalPoolTest, OverSixDecalsItCoversMostlyADecalReplacesTheOneItCoversLeast) {
    DecalPool decals = pool();
    for (std::uint32_t texture = 0; texture < 6; ++texture) {
        ASSERT_EQ(
            decals.shoot({.texture = texture, .size = kShot, .center = {.x = 32.0F, .y = 32.0F, .z = 0.0F}}),
            1U);
    }
    EXPECT_EQ(decals.decalsOn(kFloor), 6U);
    // A seventh 2 units aside covers 7/8 of each: it replaces the last of them, as the original.
    ASSERT_EQ(decals.shoot({.texture = 6, .size = kShot, .center = {.x = 34.0F, .y = 32.0F, .z = 0.0F}}), 1U);
    EXPECT_EQ(decals.decalsOn(kFloor), 6U);
    const std::vector<std::uint32_t> textures =
        decals.inOrder() | std::views::transform(&PlacedDecal::texture) | std::ranges::to<std::vector>();
    EXPECT_EQ(textures, (std::vector<std::uint32_t>{0, 1, 2, 3, 4, 6}))
        << "the newest last, drawn over the others";
    // One covering less than 60% of them takes a slot of its own.
    ASSERT_EQ(decals.shoot({.texture = 7, .size = kShot, .center = {.x = 44.0F, .y = 32.0F, .z = 0.0F}}), 1U);
    EXPECT_EQ(decals.decalsOn(kFloor), 7U);
}

TEST_F(DecalPoolTest, AFullPoolReusesItsOldestSlot) {
    // The floor alone: a face the decal's box reaches takes a slot before it is cut, as in the
    // original, so the walls next to it would take slots too.
    std::vector<DecalFace> faces(6);
    faces[kFloor].takesDecals = true;
    DecalPool decals = pool(3, std::move(faces));
    const std::uint64_t empty = decals.revision();
    for (const auto& [texture, x] : {std::pair{0U, 8.0F}, std::pair{1U, 32.0F}, std::pair{2U, 56.0F}}) {
        ASSERT_EQ(decals.shoot({.texture = texture, .size = kShot, .center = {.x = x, .y = 8.0F, .z = 0.0F}}),
                  1U);
    }
    EXPECT_NE(decals.revision(), empty);
    ASSERT_EQ(decals.shoot({.texture = 3, .size = kShot, .center = {.x = 32.0F, .y = 56.0F, .z = 0.0F}}), 1U);
    const std::vector<std::uint32_t> textures =
        decals.inOrder() | std::views::transform(&PlacedDecal::texture) | std::ranges::to<std::vector>();
    EXPECT_EQ(textures, (std::vector<std::uint32_t>{1, 2, 3})) << "the first went";
    EXPECT_EQ(decals.decalsOn(kFloor), 3U);
}

/// The textures of the decals of `decals`, oldest first.
[[nodiscard]] auto texturesOf(const DecalPool& decals) -> std::vector<std::uint32_t> {
    return decals.inOrder() | std::views::transform(&PlacedDecal::texture) | std::ranges::to<std::vector>();
}

/// A shot of texture `texture` on the floor at `x`, 8 units from its side, for `life`.
[[nodiscard]] auto onTheFloor(std::uint32_t texture, float x,
                              nx::render::DecalLife life = nx::render::DecalLife::Temporary) -> DecalShot {
    return DecalShot{.texture = texture, .size = kShot, .center = {.x = x, .y = 8.0F}, .life = life};
}

/// The box's faces, the floor alone taking decals: a decal near it takes one slot.
[[nodiscard]] auto floorOnly() -> std::vector<DecalFace> {
    std::vector<DecalFace> faces(kBoxFaces);
    faces[kFloor].takesDecals = true;
    return faces;
}

TEST_F(DecalPoolTest, APermanentDecalKeepsItsSlotWhileAnotherIsFree) {
    DecalPool decals = pool(2, floorOnly());
    ASSERT_EQ(decals.shoot(onTheFloor(0, 8.0F, nx::render::DecalLife::Permanent)), 1U);
    ASSERT_EQ(decals.shoot(onTheFloor(1, 32.0F)), 1U);
    ASSERT_EQ(decals.shoot(onTheFloor(2, 56.0F)), 1U);
    EXPECT_EQ(texturesOf(decals), (std::vector<std::uint32_t>{0, 2})) << "the temporary one went";
}

TEST_F(DecalPoolTest, WhenEverySlotIsPermanentTheRingTakesTheLastItTried) {
    DecalPool decals = pool(2, floorOnly());
    ASSERT_EQ(decals.shoot(onTheFloor(0, 8.0F, nx::render::DecalLife::Permanent)), 1U);
    ASSERT_EQ(decals.shoot(onTheFloor(1, 32.0F, nx::render::DecalLife::Permanent)), 1U);
    ASSERT_EQ(decals.shoot(onTheFloor(2, 56.0F)), 1U);
    EXPECT_EQ(texturesOf(decals), (std::vector<std::uint32_t>{0, 2})) << "as R_DecalAlloc gives up";
}

TEST_F(DecalPoolTest, NoDecalReplacesAPermanentOneItCovers) {
    DecalPool decals = pool(DecalPool::kMostDecals, floorOnly());
    for (std::uint32_t texture = 0; texture < 6; ++texture) {
        ASSERT_EQ(decals.shoot(onTheFloor(texture, 32.0F, nx::render::DecalLife::Permanent)), 1U);
    }
    // Over six it covers mostly, which a temporary one would replace the last of.
    ASSERT_EQ(decals.shoot(onTheFloor(6, 34.0F)), 1U);
    EXPECT_EQ(decals.decalsOn(kFloor), 7U);
    EXPECT_EQ(texturesOf(decals), (std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5, 6}));
}

TEST_F(DecalPoolTest, APermanentDecalCutAwayLeavesItsSlotPermanent) {
    // Its box reaches the floor, which takes the slot, but it lies past it: as the original, the slot
    // keeps its flags, and the ring passes over it.
    DecalPool decals = pool(2, floorOnly());
    ASSERT_EQ(decals.shoot(DecalShot{.texture = 0,
                                     .size = kShot,
                                     .center = {.x = -12.0F, .y = 8.0F},
                                     .life = nx::render::DecalLife::Permanent}),
              0U);
    ASSERT_EQ(decals.shoot(onTheFloor(1, 32.0F)), 1U);
    ASSERT_EQ(decals.shoot(onTheFloor(2, 56.0F)), 1U);
    EXPECT_EQ(texturesOf(decals), (std::vector<std::uint32_t>{2})) << "the second's slot went to the third";
}

TEST_F(DecalPoolTest, ALowerLimitKeepsTheRingWithinItAndTheDecalsPastIt) {
    DecalPool decals = pool(3, floorOnly());
    for (const auto& [texture, x] : {std::pair{0U, 8.0F}, std::pair{1U, 32.0F}, std::pair{2U, 56.0F}}) {
        ASSERT_EQ(decals.shoot(onTheFloor(texture, x)), 1U);
    }
    decals.setLimit(1);
    EXPECT_EQ(decals.limit(), 1U);
    ASSERT_EQ(decals.shoot(onTheFloor(3, 8.0F)), 1U);
    ASSERT_EQ(decals.shoot(onTheFloor(4, 8.0F)), 1U);
    EXPECT_EQ(texturesOf(decals), (std::vector<std::uint32_t>{1, 2, 4}))
        << "the first slot taken in turn, the others' decals left";

    // A higher one brings their slots back into the ring, where it goes on.
    decals.setLimit(3);
    ASSERT_EQ(decals.shoot(onTheFloor(5, 32.0F)), 1U);
    EXPECT_EQ(texturesOf(decals), (std::vector<std::uint32_t>{2, 4, 5}));
}

TEST_F(DecalPoolTest, AHigherLimitGrowsThePoolUpToTheMost) {
    DecalPool decals = pool(1, floorOnly());
    ASSERT_EQ(decals.shoot(onTheFloor(0, 8.0F)), 1U);
    decals.setLimit(2);
    ASSERT_EQ(decals.shoot(onTheFloor(1, 56.0F)), 1U);
    EXPECT_EQ(texturesOf(decals), (std::vector<std::uint32_t>{0, 1})) << "a slot of its own";
    decals.setLimit(DecalPool::kMostDecals + 1);
    EXPECT_EQ(decals.limit(), DecalPool::kMostDecals);
}

TEST_F(DecalPoolTest, ALimitOfNoneStampsNoMoreAndKeepsWhatItHas) {
    DecalPool decals = pool(2, floorOnly());
    ASSERT_EQ(decals.shoot(onTheFloor(0, 8.0F)), 1U);
    decals.setLimit(0);
    EXPECT_EQ(decals.shoot(onTheFloor(1, 56.0F)), 0U);
    EXPECT_EQ(texturesOf(decals), (std::vector<std::uint32_t>{0}));
}

TEST_F(DecalPoolTest, APoolOfNoDecalsStampsNone) {
    DecalPool decals = pool(0);
    EXPECT_EQ(decals.shoot({.texture = 0, .size = kShot, .center = {.x = 32.0F, .y = 32.0F, .z = 0.0F}}), 0U);
    EXPECT_EQ(
        decals.shoot({.texture = 0, .size = DecalSize{}, .center = {.x = 32.0F, .y = 32.0F, .z = 0.0F}}), 0U);
}

TEST_F(DecalPoolTest, AShotAtAModelTheMapDoesNotHaveIsAContractViolation) {
    DecalPool decals = pool();
    const nx::test::ContractRecorder recorder;
    EXPECT_EQ(decals.shoot({.texture = 0, .size = kShot, .model = kWall, .center = {.x = 16.0F}}), 0U);
    EXPECT_EQ(nx::test::ContractRecorder::count(), 1);
    EXPECT_TRUE(decals.inOrder().empty());
}

class EntityDecalPoolTest : public DecalPoolTest {
protected:
    EntityDecalPoolTest() : DecalPoolTest{"boxWithEntity.bsp"} {}

    /// The box's faces and the square, CRATE01, which takes decals.
    [[nodiscard]] static auto faces() -> std::vector<DecalFace> {
        std::vector<DecalFace> all = boxFaces(1);
        all[kSquare].takesDecals = true;
        return all;
    }
};

TEST_F(EntityDecalPoolTest, ADecalOnABrushEntityLiesOnItsModelsFacesInItsSpace) {
    DecalPool decals = pool(DecalPool::kMostDecals, faces());
    ASSERT_EQ(
        decals.shoot(
            {.texture = 2, .size = kShot, .model = kWall, .center = {.x = 16.0F, .y = 32.0F, .z = 32.0F}}),
        1U);
    ASSERT_EQ(decals.inOrder().size(), 1U);
    const PlacedDecal& placed = *decals.inOrder().front();
    EXPECT_EQ(placed.face, kSquare);
    EXPECT_EQ(placed.model, kWall);
    // 16 units square about the center, on the square's plane.
    const Bounds bounds = boundsOf(placed);
    EXPECT_NEAR(bounds.positions.min.x, 16.0F, kTolerance);
    EXPECT_NEAR(bounds.positions.max.x, 16.0F, kTolerance);
    EXPECT_NEAR(bounds.positions.min.y, 24.0F, kTolerance);
    EXPECT_NEAR(bounds.positions.max.y, 40.0F, kTolerance);
    EXPECT_NEAR(bounds.positions.min.z, 24.0F, kTolerance);
    EXPECT_NEAR(bounds.positions.max.z, 40.0F, kTolerance);
    EXPECT_EQ(decals.decalsOn(kSquare), 1U);
}

TEST_F(EntityDecalPoolTest, EachModelsDecalsComeFromItsOwnTree) {
    DecalPool decals = pool(DecalPool::kMostDecals, faces());
    EXPECT_EQ(decals.shoot({.texture = 0, .size = kShot, .center = {.x = 16.0F, .y = 32.0F, .z = 32.0F}}), 0U)
        << "the world's tree does not reach the square";
    EXPECT_EQ(
        decals.shoot(
            {.texture = 0, .size = kShot, .model = kWall, .center = {.x = 32.0F, .y = 32.0F, .z = 0.0F}}),
        0U)
        << "nor the wall's the floor";
    EXPECT_EQ(decals.shoot({.texture = 0, .size = kShot, .center = {.x = 32.0F, .y = 32.0F, .z = 0.0F}}), 1U);
    EXPECT_EQ(decals.inOrder().front()->model, nx::assets::kWorldModel);
}

TEST_F(EntityDecalPoolTest, TheWorldAndTheBrushEntitiesShareThePool) {
    // The floor and the square alone take decals.
    std::vector<DecalFace> floorAndSquare = faces();
    floorAndSquare[2].takesDecals = false;
    floorAndSquare[4].takesDecals = false;
    DecalPool decals = pool(2, std::move(floorAndSquare));
    ASSERT_EQ(decals.shoot({.texture = 0, .size = kShot, .center = {.x = 32.0F, .y = 32.0F, .z = 0.0F}}), 1U);
    for (const auto& [texture, height] : {std::pair{1U, 32.0F}, std::pair{2U, 20.0F}}) {
        ASSERT_EQ(decals.shoot({.texture = texture,
                                .size = kShot,
                                .model = kWall,
                                .center = {.x = 16.0F, .y = 32.0F, .z = height}}),
                  1U);
    }
    const std::vector<std::uint32_t> textures =
        decals.inOrder() | std::views::transform(&PlacedDecal::texture) | std::ranges::to<std::vector>();
    EXPECT_EQ(textures, (std::vector<std::uint32_t>{1, 2})) << "the world's, the oldest, went";
    EXPECT_EQ(decals.decalsOn(kFloor), 0U);
}

/// A 2 x 2 mip texture of `indices` with `palette`, levels of one texel past the first.
struct MipFixture {
    std::array<std::byte, 4> indices{};
    std::array<std::byte, 1> smaller{};
    std::vector<std::byte> palette = std::vector<std::byte>(256 * 3, std::byte{0});

    [[nodiscard]] auto texture() const -> nx::assets::MipTexture {
        return nx::assets::MipTexture{.name = "{shot1",
                                      .width = 2,
                                      .height = 2,
                                      .levels = {indices, smaller, smaller, smaller},
                                      .palette = palette};
    }
    void setColor(std::size_t entry, std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
        palette[entry * 3] = std::byte{red};
        palette[(entry * 3) + 1] = std::byte{green};
        palette[(entry * 3) + 2] = std::byte{blue};
    }
};

[[nodiscard]] auto identityGamma() -> nx::render::ChannelTable {
    nx::render::ChannelTable table{};
    std::ranges::iota(table, std::uint8_t{0});
    return table;
}

TEST(DecalImageTest, ADecalIsItsLastColorAsOpaqueAsEachIndex) {
    MipFixture mip{.indices = {std::byte{0}, std::byte{100}, std::byte{254}, std::byte{255}}};
    mip.setColor(255, 10, 20, 30);
    mip.setColor(100, 200, 200, 200); // not shown: a tinted decal is its last color throughout
    const nx::assets::RgbaImage image = nx::render::decalImageOf(mip.texture(), identityGamma());
    EXPECT_EQ(image.width, 2U);
    EXPECT_EQ(image.height, 2U);
    const auto texel = [&image](std::size_t index) {
        return std::array{std::to_integer<int>(image.pixels[index * 4]),
                          std::to_integer<int>(image.pixels[(index * 4) + 1]),
                          std::to_integer<int>(image.pixels[(index * 4) + 2]),
                          std::to_integer<int>(image.pixels[(index * 4) + 3])};
    };
    EXPECT_EQ(texel(0), (std::array{10, 20, 30, 0}));
    EXPECT_EQ(texel(1), (std::array{10, 20, 30, 100}));
    EXPECT_EQ(texel(2), (std::array{10, 20, 30, 254}));
    EXPECT_EQ(texel(3)[3], 0) << "index 255 is clear";
}

TEST(DecalImageTest, ADecalWhoseLastColorIsPureBlueIsMasked) {
    MipFixture mip{.indices = {std::byte{1}, std::byte{2}, std::byte{255}, std::byte{1}}};
    mip.setColor(255, 0, 0, 255);
    mip.setColor(1, 40, 50, 60);
    mip.setColor(2, 70, 80, 90);
    nx::render::ChannelTable doubled{};
    for (std::size_t value = 0; value < doubled.size(); ++value) {
        doubled[value] = static_cast<std::uint8_t>(std::min<std::size_t>(value * 2, 255));
    }
    const nx::assets::RgbaImage image = nx::render::decalImageOf(mip.texture(), doubled);
    const auto texel = [&image](std::size_t index) {
        return std::array{std::to_integer<int>(image.pixels[index * 4]),
                          std::to_integer<int>(image.pixels[(index * 4) + 1]),
                          std::to_integer<int>(image.pixels[(index * 4) + 2]),
                          std::to_integer<int>(image.pixels[(index * 4) + 3])};
    };
    EXPECT_EQ(texel(0), (std::array{80, 100, 120, 255})) << "its color through texgamma, opaque";
    EXPECT_EQ(texel(1), (std::array{140, 160, 180, 255}));
    EXPECT_EQ(texel(2), (std::array{0, 0, 0, 0})) << "index 255 is clear";
}

} // namespace
