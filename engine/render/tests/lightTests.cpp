// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The light of the world where things are, in the BSP seed of the fuzz corpus: a 64-unit box whose
// one node, on the floor's plane, holds its six faces, each with one lightmap of style 0. Three have
// CRATE01 (the floor first), the other three {BLUE, whose texture info is special. The floor's
// lightmap is 5 x 5 luxels from its corner at the origin, its k-th byte 7k modulo 256.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.assets;
import nx.render;

namespace {

using nx::Vec3;
using nx::render::LightColor;
using nx::render::LightStyleValues;
using nx::render::ModelLight;
using nx::render::WorldLight;

constexpr float kTolerance = 1e-4F;
/// Far enough down to cross the floor from inside the box.
constexpr float kDown = -2048.0F;

// Where the seed's records are: the lumps' directory after the version, and in their lumps the
// fields the tests change.
constexpr std::size_t kLumpDirectory = 4;
constexpr std::size_t kLumpEntryBytes = 8;
constexpr std::size_t kTextureInfoLump = 6;
constexpr std::size_t kFacesLump = 7;
constexpr std::size_t kLightingLump = 8;
constexpr std::size_t kTextureInfoBytes = 40;
constexpr std::size_t kTextureInfoFlags = 36;
constexpr std::size_t kFaceBytes = 20;
constexpr std::size_t kFaceStyles = 12;
constexpr std::size_t kFaceLightmapOffset = 16;
constexpr std::size_t kSeedFaces = 6;
constexpr std::uint32_t kSpecial = 1;
constexpr std::uint32_t kNotSpecial = 0;
constexpr std::int32_t kNoLightmap = -1;

[[nodiscard]] auto readSeed() -> std::vector<std::byte> {
    auto bytes =
        nx::platform::readFile(std::filesystem::path{NX_FUZZ_CORPUS_DIRECTORY} / "bsp" / "boxEmbedded.bsp");
    EXPECT_TRUE(bytes.has_value()) << nx::describe(bytes.error());
    return bytes.value_or(std::vector<std::byte>{});
}

/// Writes `value` at `offset` of `file`, little-endian as the BSP is.
template <class T>
void write(std::vector<std::byte>& file, std::size_t offset, T value) {
    const auto bytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
    std::ranges::copy(bytes, file.begin() + static_cast<std::ptrdiff_t>(offset));
}

/// Where lump `lump` starts in `file`.
[[nodiscard]] auto lumpAt(const std::vector<std::byte>& file, std::size_t lump) -> std::size_t {
    std::array<std::byte, sizeof(std::int32_t)> bytes{};
    std::ranges::copy_n(file.begin() + static_cast<std::ptrdiff_t>(kLumpDirectory + (lump * kLumpEntryBytes)),
                        bytes.size(), bytes.begin());
    return static_cast<std::size_t>(std::bit_cast<std::int32_t>(bytes));
}

/// Renames the texture `name` of the map in `file` to `to`: its 16-byte name field.
void renameTexture(std::vector<std::byte>& file, std::string_view name, std::string_view to) {
    const auto found = std::ranges::search(file, std::as_bytes(std::span{name}));
    ASSERT_FALSE(found.empty()) << name;
    const auto field = std::span{file}.subspan(static_cast<std::size_t>(found.begin() - file.begin()), 16);
    std::ranges::fill(field, std::byte{0});
    std::ranges::copy(std::as_bytes(std::span{to}), field.begin());
}

/// The styles of a map with style 0 at `letter`.
[[nodiscard]] auto stylesAt(std::string_view letter) -> LightStyleValues {
    nx::render::LightStyles styles;
    styles.set(0, letter);
    return styles.valuesAt(nx::Seconds{0.0F});
}

/// The light straight down from `x`, `y` inside the box: the floor's, at (x, y, 0).
[[nodiscard]] auto lightBelow(const WorldLight& light, float x, float y, const LightStyleValues& styles)
    -> LightColor {
    return light.lightAlong({.x = x, .y = y, .z = 32.0F}, {.x = x, .y = y, .z = kDown}, styles);
}

/// The seed's map as `edit` leaves its bytes, and its light.
class World {
public:
    explicit World(std::vector<std::byte> bytes, nx::assets::BspView map)
        : m_bytes{std::move(bytes)}, m_map{std::move(map)}, m_light{m_map} {}

    [[nodiscard]] auto light() const noexcept -> const WorldLight& { return m_light; }

private:
    std::vector<std::byte> m_bytes; ///< What the map views.
    nx::assets::BspView m_map;
    WorldLight m_light;
};

[[nodiscard]] auto worldOf(const std::function<void(std::vector<std::byte>&)>& edit = {})
    -> std::unique_ptr<World> {
    std::vector<std::byte> bytes = readSeed();
    if (edit) {
        edit(bytes);
    }
    auto map = nx::assets::BspView::parse(bytes);
    EXPECT_TRUE(map.has_value()) << (map ? "" : nx::describe(map.error()));
    return map ? std::make_unique<World>(std::move(bytes), std::move(*map)) : nullptr;
}

void expectNear(Vec3 actual, Vec3 expected, std::string_view what = {}) {
    EXPECT_NEAR(actual.x, expected.x, kTolerance) << what;
    EXPECT_NEAR(actual.y, expected.y, kTolerance) << what;
    EXPECT_NEAR(actual.z, expected.z, kTolerance) << what;
}

TEST(WorldLightTest, TheLightOfAPointIsItsNearestLuxelTimesItsStyle) {
    const auto world = worldOf();
    ASSERT_NE(world, nullptr);
    const LightStyleValues normal = stylesAt("m");
    // (8, 8) is halfway between luxels 0 and 1 on each axis: the nearest is 1, byte 18 on.
    EXPECT_EQ(lightBelow(world->light(), 8.0F, 8.0F, normal),
              (LightColor{.red = 129, .green = 137, .blue = 144}))
        << "126, 133 and 140, times 264 and divided by 256";
    EXPECT_EQ(lightBelow(world->light(), 0.5F, 0.5F, normal), (LightColor{.red = 0, .green = 7, .blue = 14}));
    EXPECT_EQ(lightBelow(world->light(), 32.0F, 32.0F, normal),
              (LightColor{.red = 255, .green = 3, .blue = 10}))
        << "252 times 264 is 259 once divided: held to 255";
}

TEST(WorldLightTest, AStylesBrightnessScalesTheLight) {
    const auto world = worldOf();
    ASSERT_NE(world, nullptr);
    EXPECT_EQ(lightBelow(world->light(), 0.5F, 0.5F, stylesAt("z")),
              (LightColor{.red = 0, .green = 15, .blue = 30}))
        << "7 and 14 times 550";
    EXPECT_EQ(lightBelow(world->light(), 0.5F, 0.5F, stylesAt("a")), LightColor{});
}

TEST(WorldLightTest, AStyleAboveTheGamesKeepsTheNormalBrightness) {
    const auto world = worldOf([](std::vector<std::byte>& bytes) {
        write(bytes, lumpAt(bytes, kFacesLump) + kFaceStyles, std::uint8_t{70});
    });
    ASSERT_NE(world, nullptr);
    EXPECT_EQ(lightBelow(world->light(), 8.0F, 8.0F, stylesAt("a")),
              (LightColor{.red = 129, .green = 137, .blue = 144}));
}

TEST(WorldLightTest, TheLightPassesSpecialFaces) {
    // CRATE01's texture info made special, {BLUE's not: of the node's faces, the first whose extent has
    // (32, 32, 0) is then the last wall's, at its luxel 2 of row 0: 107, 114 and 121.
    const auto world = worldOf([](std::vector<std::byte>& bytes) {
        const std::size_t infos = lumpAt(bytes, kTextureInfoLump);
        write(bytes, infos + kTextureInfoFlags, kSpecial);
        write(bytes, infos + kTextureInfoBytes + kTextureInfoFlags, kNotSpecial);
    });
    ASSERT_NE(world, nullptr);
    EXPECT_EQ(lightBelow(world->light(), 32.0F, 32.0F, stylesAt("m")),
              (LightColor{.red = 110, .green = 117, .blue = 124}));
}

TEST(WorldLightTest, AFaceWithoutSamplesIsBlack) {
    const auto world = worldOf([](std::vector<std::byte>& bytes) {
        write(bytes, lumpAt(bytes, kFacesLump) + kFaceLightmapOffset, kNoLightmap);
    });
    ASSERT_NE(world, nullptr);
    EXPECT_EQ(lightBelow(world->light(), 8.0F, 8.0F, stylesAt("m")), LightColor{});
}

TEST(WorldLightTest, AMapWithoutLightIsFullyBright) {
    const auto world = worldOf([](std::vector<std::byte>& bytes) {
        for (std::size_t face = 0; face < kSeedFaces; ++face) {
            write(bytes, lumpAt(bytes, kFacesLump) + (face * kFaceBytes) + kFaceLightmapOffset, kNoLightmap);
        }
        write(bytes, kLumpDirectory + (kLightingLump * kLumpEntryBytes) + sizeof(std::int32_t),
              std::int32_t{0});
    });
    ASSERT_NE(world, nullptr);
    EXPECT_EQ(lightBelow(world->light(), 8.0F, 8.0F, stylesAt("m")),
              (LightColor{.red = 255, .green = 255, .blue = 255}));
}

TEST(WorldLightTest, AModelIsLitFromBelowGoingTheWayTheLightDims) {
    const auto world = worldOf();
    ASSERT_NE(world, nullptr);
    const ModelLight light =
        world->light().modelLightAt({.origin = {.x = 32.0F, .y = 32.0F, .z = 32.0F}}, stylesAt("m"));

    // Under it, (255, 3, 10): its whole light is 255, 0.9 of it shade.
    EXPECT_NEAR(light.shade, 229.5F, kTolerance);
    EXPECT_NEAR(light.ambient, 25.5F, kTolerance);
    expectNear(light.color, {.x = 1.0F, .y = 3.0F / 255.0F, .z = 10.0F / 255.0F});
    // The corners 16 units about it sum 410 (back left), 540 (back right), 398 (front right) and 267
    // (front left), of 768.
    const Vec3 way{.x = (410.0F - 540.0F - 398.0F + 267.0F) / 768.0F,
                   .y = (540.0F + 410.0F - 398.0F - 267.0F) / 768.0F,
                   .z = -1.0F};
    expectNear(light.direction, nx::normalized(way), "towards the dimmer corners, and down");
}

TEST(WorldLightTest, UnderOpenSkyAModelTakesTheSkysLight) {
    const auto world = worldOf([](std::vector<std::byte>& bytes) { renameTexture(bytes, "CRATE01", "sky"); });
    ASSERT_NE(world, nullptr);
    const Vec3 origin{.x = 32.0F, .y = 32.0F, .z = 32.0F};
    // A sky below, its light going up: the first face met against its way is the floor, now sky.
    const nx::render::SkyLight upward{.color = {.x = 100.7F, .y = 150.0F, .z = 200.0F},
                                      .direction = {.z = 1.0F}};
    const ModelLight lit = world->light().modelLightAt({.origin = origin, .sky = upward}, stylesAt("m"));
    EXPECT_NEAR(lit.shade, 180.0F, kTolerance) << "0.9 of its brightest channel";
    EXPECT_NEAR(lit.ambient, 20.0F, kTolerance);
    expectNear(lit.color, {.x = 0.5F, .y = 0.75F, .z = 1.0F}, "100.7 taken as 100");
    expectNear(lit.direction, {.z = 1.0F});

    // Against a sky above, the segment meets nothing: the light under the model instead.
    const nx::render::SkyLight downward{.color = upward.color, .direction = {.z = -1.0F}};
    const ModelLight floor = world->light().modelLightAt({.origin = origin, .sky = downward}, stylesAt("m"));
    EXPECT_NEAR(floor.shade, 229.5F, kTolerance);
}

TEST(WorldLightTest, AModelsLightIsHeldToTheOriginalsRanges) {
    const auto world = worldOf([](std::vector<std::byte>& bytes) { renameTexture(bytes, "CRATE01", "sky"); });
    ASSERT_NE(world, nullptr);
    const nx::render::SkyLight bright{.color = {.x = 600.0F, .y = 600.0F, .z = 600.0F},
                                      .direction = {.z = 1.0F}};
    const Vec3 origin{.x = 32.0F, .y = 32.0F, .z = 32.0F};

    const ModelLight held = world->light().modelLightAt({.origin = origin, .sky = bright}, stylesAt("m"));
    EXPECT_NEAR(held.ambient, 60.0F, kTolerance);
    EXPECT_NEAR(held.shade, 195.0F, kTolerance) << "the two together no more than 255";

    const ModelLight direct =
        world->light().modelLightAt({.origin = origin, .sky = bright, .direct = 0.5F}, stylesAt("m"));
    EXPECT_NEAR(direct.ambient, 128.0F, kTolerance) << "direct held to 0.75: 150 of ambient, held to 128";
    EXPECT_NEAR(direct.shade, 127.0F, kTolerance);
}

} // namespace
