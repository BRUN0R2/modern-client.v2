// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// What the client sets up as the game would: the sky's light and the sprite entities of a map, the BSP seed
// of the fuzz corpus given other entities, and how those sprites animate.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.assets;
import nx.render;
import cs.client;

namespace {

using cs::MapSprite;
using cs::SpriteClass;
using nx::render::RenderMode;

constexpr float kTolerance = 1e-5F;
/// Where the header of a BSP keeps the offset and the length of its entities.
constexpr std::size_t kEntitiesLump = 4;

/// A map of the BSP seed whose entities are `entities`, the text of its entities lump.
class MapOf {
public:
    explicit MapOf(std::string_view entities) {
        auto seed = nx::platform::readFile(std::filesystem::path{NX_FUZZ_CORPUS_DIRECTORY} / "bsp" /
                                           "boxEmbedded.bsp");
        EXPECT_TRUE(seed.has_value());
        m_bytes = seed.value_or(std::vector<std::byte>{});
        // The new lump at the end of the file, its terminating zero included.
        const auto offset = static_cast<std::int32_t>(m_bytes.size());
        const auto length = static_cast<std::int32_t>(entities.size() + 1);
        std::ranges::copy(std::as_bytes(std::span{entities}), std::back_inserter(m_bytes));
        m_bytes.push_back(std::byte{0});
        const auto header =
            std::bit_cast<std::array<std::byte, sizeof(std::int32_t) * 2>>(std::array{offset, length});
        std::ranges::copy(header, m_bytes.begin() + static_cast<std::ptrdiff_t>(kEntitiesLump));
        auto map = nx::assets::BspView::parse(m_bytes);
        EXPECT_TRUE(map.has_value()) << (map ? "" : nx::describe(map.error()));
        if (map) {
            m_map.emplace(std::move(*map));
        }
    }

    [[nodiscard]] auto get() const -> const nx::assets::BspView& { return *m_map; }

private:
    std::vector<std::byte> m_bytes;
    std::optional<nx::assets::BspView> m_map;
};

/// The engine's light of a compilers' level, as the game turns it.
[[nodiscard]] auto engineLevel(double level) -> float {
    return static_cast<float>(std::trunc(std::pow(level / 114.0, 0.6) * 264.0));
}

void expectNear(nx::Vec3 actual, nx::Vec3 expected, std::string_view what = {}) {
    EXPECT_NEAR(actual.x, expected.x, kTolerance) << what;
    EXPECT_NEAR(actual.y, expected.y, kTolerance) << what;
    EXPECT_NEAR(actual.z, expected.z, kTolerance) << what;
}

TEST(SkyLightTest, LightEnvironmentsLightIsMadeTheEnginesAndAimsByItsPitch) {
    const MapOf map{R"({ "classname" "worldspawn" }
{ "classname" "light_environment" "angles" "0 90 0" "pitch" "-90" "_light" "255 128 64 200" })"};
    const nx::render::SkyLight sky = cs::skyLightOf(map.get());
    // Each channel scaled by 200 / 255 and cut: 200, 100 and 50.
    expectNear(sky.color, {.x = engineLevel(200.0), .y = engineLevel(100.0), .z = engineLevel(50.0)});
    expectNear(sky.direction, {.z = -1.0F}, "a pitch of -90 aims straight down");
}

TEST(SkyLightTest, OneLevelIsGrayAndLaterKeysWin) {
    // `pitch` before `angles`: the angles set later win, their pitch 0.
    const MapOf map{R"({ "classname" "worldspawn" }
{ "classname" "light_environment" "pitch" "-90" "angles" "0 180 0" "_light" "100" })"};
    const nx::render::SkyLight sky = cs::skyLightOf(map.get());
    expectNear(sky.color, {.x = engineLevel(100.0), .y = engineLevel(100.0), .z = engineLevel(100.0)});
    expectNear(sky.direction, {.x = -1.0F}, "level, toward a yaw of 180");
}

TEST(SkyLightTest, AMapWithoutLightEnvironmentHasNoSkyLight) {
    const MapOf map{R"({ "classname" "worldspawn" })"};
    expectNear(cs::skyLightOf(map.get()).color, {});
}

TEST(MapSpriteTest, SpriteEntitiesAreReadAsTheGameMakesThem) {
    const MapOf map{R"({ "classname" "worldspawn" }
{ "classname" "env_sprite" "model" "sprites/flare2.spr" "origin" "1 2 3" "angles" "0 45 0" "rendermode" "5" "renderamt" "90" "rendercolor" "255 128 0" "scale" ".5" "framerate" "10" "spawnflags" "1" "targetname" "lamp" }
{ "classname" "env_sprite" "model" "sprites/off.spr" "targetname" "dark" }
{ "classname" "env_glow" "model" "sprites/glow01.SPR" "rendermode" "3" "renderfx" "14" "renderamt" "300" }
{ "classname" "cycler_sprite" "model" "models/palmtree.mdl" }
{ "classname" "info_target" "model" "sprites/ignored.spr" })"};
    const std::vector<MapSprite> sprites = cs::mapSpritesOf(map.get());
    ASSERT_EQ(sprites.size(), 3U) << "the sprites of env_sprite and env_glow; not a model, nor another class";

    const MapSprite& flare = sprites[0];
    EXPECT_EQ(flare.kind, SpriteClass::Sprite);
    expectNear(flare.origin, {.x = 1.0F, .y = 2.0F, .z = 3.0F});
    EXPECT_FLOAT_EQ(flare.angles.roll.value(), 45.0F) << "the yaw alone becomes the roll";
    EXPECT_FLOAT_EQ(flare.angles.yaw.value(), 0.0F);
    EXPECT_EQ(flare.mode, RenderMode::TransAdd);
    EXPECT_EQ(flare.amount, 90);
    EXPECT_EQ(flare.color, (nx::render::RenderColor{.red = 255, .green = 128, .blue = 0}));
    EXPECT_FLOAT_EQ(flare.scale, 0.5F);
    EXPECT_TRUE(flare.isOn) << "named, but it starts on";

    EXPECT_FALSE(sprites[1].isOn) << "named, it waits to be turned on";
    EXPECT_EQ(sprites[2].kind, SpriteClass::Glow);
    EXPECT_EQ(sprites[2].effect, nx::render::RenderEffect::NoDissipation);
    EXPECT_EQ(sprites[2].amount, 255) << "held to a byte";
}

TEST(MapSpriteTest, AnEnvSpriteAnimatesTenTimesASecondAndLoopsShortOfItsLastFrame) {
    const MapSprite flare{.kind = SpriteClass::Sprite, .framerate = 10.0F};
    EXPECT_EQ(cs::frameAt(flare, 10, nx::Seconds{0.05F}), 0.0F) << "no think has added a frame";
    EXPECT_EQ(cs::frameAt(flare, 10, nx::Seconds{0.35F}), 3.0F) << "three thinks";
    EXPECT_EQ(cs::frameAt(flare, 10, nx::Seconds{1.05F}), 1.0F) << "frame 10 is past 9: 10 modulo 9";
    EXPECT_EQ(cs::frameAt(flare, 2, nx::Seconds{1.0F}), 0.0F) << "two frames do not animate";

    const MapSprite once{.kind = SpriteClass::Sprite, .framerate = 10.0F, .playsOnce = true};
    EXPECT_EQ(cs::frameAt(once, 10, nx::Seconds{0.95F}), 9.0F);
    EXPECT_EQ(cs::frameAt(once, 10, nx::Seconds{1.05F}), std::nullopt) << "past its last frame it turns off";

    const MapSprite off{.kind = SpriteClass::Sprite, .isOn = false};
    EXPECT_EQ(cs::frameAt(off, 10, nx::Seconds{1.0F}), std::nullopt);
}

TEST(MapSpriteTest, GlowsAnimateWithAFramerateAndCyclersWithout) {
    const MapSprite still{.kind = SpriteClass::Glow};
    EXPECT_EQ(cs::frameAt(still, 10, nx::Seconds{1.0F}), 0.0F);
    const MapSprite glow{.kind = SpriteClass::Glow, .framerate = 20.0F};
    EXPECT_EQ(cs::frameAt(glow, 10, nx::Seconds{0.25F}), 4.0F) << "two thinks of two frames";
    const MapSprite cycler{.kind = SpriteClass::Cycler, .framerate = 10.0F};
    EXPECT_EQ(cs::frameAt(cycler, 3, nx::Seconds{0.25F}), 0.0F) << "frame 2 of the loop's 2: 0 again";
}

TEST(MapModelTest, ModelEntitiesAreReadAsTheGameMakesThem) {
    const MapOf map{R"({ "classname" "worldspawn" }
{ "classname" "cycler_sprite" "model" "models/palmtree.mdl" "origin" "1 2 3" "angles" "0 90 0" "framerate" "10" "rendermode" "4" "renderamt" "255" }
{ "classname" "cycler_sprite" "model" "sprites/glow01.spr" }
{ "classname" "hostage_entity" "model" "/models/hostage.mdl" "angle" "270" "skin" "2" }
{ "classname" "hostage_entity" "skin" "-1" })"};
    const std::vector<cs::MapModel> models = cs::mapModelsOf(map.get());
    ASSERT_EQ(models.size(), 3U) << "the cycler of a model and the hostages; not the cycler of a sprite";

    const cs::MapModel& palm = models[0];
    EXPECT_EQ(palm.kind, cs::ModelClass::Cycler);
    EXPECT_EQ(palm.model, "models/palmtree.mdl");
    expectNear(palm.origin, {.x = 1.0F, .y = 2.0F, .z = 3.0F});
    EXPECT_FLOAT_EQ(palm.angles.yaw.value(), 90.0F) << "a cycler keeps its yaw";
    EXPECT_FLOAT_EQ(palm.framerate, 10.0F);
    EXPECT_EQ(palm.mode, RenderMode::TransAlpha);
    EXPECT_EQ(palm.amount, 255);

    const cs::MapModel& named = models[1];
    EXPECT_EQ(named.kind, cs::ModelClass::Hostage);
    EXPECT_EQ(named.model, "/models/hostage.mdl");
    EXPECT_FLOAT_EQ(named.angles.yaw.value(), 270.0F) << "\"angle\" is the yaw";
    EXPECT_EQ(named.skin, 2U);
    EXPECT_FLOAT_EQ(named.framerate, 1.0F) << "at its sequence's own speed";

    EXPECT_EQ(models[2].model, "models/scientist.mdl") << "a hostage without a model";
    EXPECT_EQ(models[2].skin, 0U) << "a negative skin is 0";
}

} // namespace
