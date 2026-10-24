// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The client events a view model's sequence passes from a frame to another, the attachment of each
// muzzle flash event, the style an event's options and its weapon's data give its flash (the
// original's number, Counter-Strike Online's keys), the flash as it plays and the light it casts, the
// flashes showing, one at an anchor, and the sprites the flashes show, read once by their paths.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.render;
import cs.shared;
import cs.client;

#include "temporaryDirectory.hpp"

namespace {

using nx::assets::StudioEvent;
using nx::assets::StudioSequence;

/// The Drakar III's shot's options, as Counter-Strike Online's v_transformgunadv.mdl has them.
constexpr std::string_view kDrakarOptions = "#I341 S0.16 R2 F0 P30 T999 A1 L0 O0 X0";

/// A sequence with the AK-47's shot's event, a sound's and one of the game's own.
[[nodiscard]] auto sequence() -> StudioSequence {
    StudioSequence made;
    made.frameCount = 30;
    made.events = {StudioEvent{.frame = 0, .event = 5001, .options = "22"},
                   StudioEvent{.frame = 11, .event = 5004, .options = "weapons/ak47_boltpull.wav"},
                   StudioEvent{.frame = 5, .event = 1004, .options = "a game's event"}};
    return made;
}

[[nodiscard]] auto framesOf(std::span<const StudioEvent* const> events) -> std::vector<std::int32_t> {
    return events | std::views::transform([](const StudioEvent* event) { return event->frame; }) |
           std::ranges::to<std::vector>();
}

/// The first attachment of the view model of the weapon of index 2.
constexpr cs::MuzzleFlashAnchor kAnchor{.weapon = cs::WeaponIndex{2}, .attachment = 0};

/// The flash `style` makes of a sprite of `frames` frames at `start` at `anchor`, which must make one.
[[nodiscard]] auto shown(const cs::MuzzleFlashStyle& style, std::size_t frames, nx::Seconds start,
                         nx::Random& random, cs::MuzzleFlashAnchor anchor = kAnchor) -> cs::MuzzleFlash {
    const std::optional<cs::MuzzleFlash> flash = cs::muzzleFlashOf(style, 4, frames, anchor, start, random);
    EXPECT_TRUE(flash.has_value());
    return flash.value_or(cs::MuzzleFlash{});
}

TEST(MuzzleFlashTest, AClientEventIsPassedOnceFromTheFrameBeforeToTheFrameShown) {
    const StudioSequence played = sequence();
    // A play anew passes its first frame's.
    EXPECT_EQ(framesOf(cs::clientEventsBetween(played, -0.01F, 0.5F)), (std::vector<std::int32_t>{0}));
    EXPECT_TRUE(cs::clientEventsBetween(played, 0.5F, 10.9F).empty());
    EXPECT_EQ(framesOf(cs::clientEventsBetween(played, 10.9F, 11.0F)), (std::vector<std::int32_t>{11}));
    EXPECT_TRUE(cs::clientEventsBetween(played, 11.0F, 12.0F).empty()) << "passed already";
    EXPECT_EQ(framesOf(cs::clientEventsBetween(played, -0.01F, 29.0F)), (std::vector<std::int32_t>{0, 11}))
        << "the game's own events are not the client's";
}

TEST(MuzzleFlashTest, EachMuzzleFlashEventShowsAtItsAttachment) {
    EXPECT_EQ(cs::muzzleFlashAttachmentOf(5001), 0U);
    EXPECT_EQ(cs::muzzleFlashAttachmentOf(5011), 1U);
    EXPECT_EQ(cs::muzzleFlashAttachmentOf(5021), 2U);
    EXPECT_EQ(cs::muzzleFlashAttachmentOf(5031), 3U);
    for (const std::int32_t other : {5000, 5002, 5004, 5041, 4991}) {
        EXPECT_EQ(cs::muzzleFlashAttachmentOf(other), std::nullopt) << other;
    }
}

TEST(MuzzleFlashTest, AnOriginalEventsNumberChoosesTheSpriteAndItsSize) {
    // The AK-47's 22: the third sprite, two tenths; the USP's 11: the second, a tenth; 0, the first, half.
    const cs::MuzzleFlashStyle rifle = cs::muzzleFlashStyleOf("22", std::nullopt);
    EXPECT_EQ(rifle.sprite, "sprites/muzzleflash3.spr");
    EXPECT_FLOAT_EQ(rifle.scale, 0.2F);
    EXPECT_EQ(rifle.firstFrame, std::nullopt) << "a frame at random";
    EXPECT_FLOAT_EQ(rifle.frameRate, 0.0F) << "held";
    EXPECT_FLOAT_EQ(rifle.time.count(), 0.01F);
    EXPECT_FLOAT_EQ(rifle.roll.value(), 359.0F);
    EXPECT_FLOAT_EQ(rifle.brightness, 1.0F);
    EXPECT_FALSE(rifle.light.has_value());
    const cs::MuzzleFlashStyle pistol = cs::muzzleFlashStyleOf("11", std::nullopt);
    EXPECT_EQ(pistol.sprite, "sprites/muzzleflash2.spr");
    EXPECT_FLOAT_EQ(pistol.scale, 0.1F);
    const cs::MuzzleFlashStyle none = cs::muzzleFlashStyleOf("0", std::nullopt);
    EXPECT_EQ(none.sprite, "sprites/muzzleflash1.spr");
    EXPECT_FLOAT_EQ(none.scale, 0.5F);
    EXPECT_FLOAT_EQ(none.roll.value(), 20.0F) << "a rifle's sprite rolls 20 degrees at most";
    EXPECT_EQ(cs::muzzleFlashStyleOf("not a number", std::nullopt).sprite, "sprites/muzzleflash1.spr");
    EXPECT_EQ(cs::muzzleFlashStyleOf("-3", std::nullopt).sprite, "sprites/muzzleflash1.spr");
}

TEST(MuzzleFlashTest, AnOriginalFlashShowsAFrameAtRandomRolledAtRandomForAHundredthOfASecond) {
    const cs::MuzzleFlashStyle rifle = cs::muzzleFlashStyleOf("10", std::nullopt);
    const cs::MuzzleFlashStyle pistol = cs::muzzleFlashStyleOf("11", std::nullopt);
    nx::Random random{1, "test"};
    for (int draw = 0; draw < 200; ++draw) {
        const cs::MuzzleFlash first = shown(rifle, 3, nx::Seconds{5.0F}, random);
        EXPECT_LE(first.roll.value(), 20.0F);
        EXPECT_EQ(first.roll.value(), std::trunc(first.roll.value())) << "whole degrees";
        EXPECT_LT(first.firstFrame, 3.0F);
        EXPECT_LT(shown(pistol, 3, nx::Seconds{}, random).roll.value(), 360.0F);
    }
    const cs::MuzzleFlash flash = shown(rifle, 3, nx::Seconds{5.0F}, random);
    EXPECT_EQ(flash.sprite, 4U);
    EXPECT_EQ(flash.anchor, kAnchor);
    EXPECT_FLOAT_EQ(flash.scale, 0.1F);
    EXPECT_FLOAT_EQ(flash.until.count(), 5.01F);
    EXPECT_EQ(flash.frameAt(nx::Seconds{5.0F}), flash.firstFrame);
    EXPECT_EQ(flash.frameAt(nx::Seconds{5.009F}), flash.firstFrame) << "held";
    EXPECT_EQ(flash.frameAt(nx::Seconds{5.02F}), std::nullopt);
    EXPECT_TRUE(flash.isOverAt(nx::Seconds{5.02F}));
    EXPECT_FALSE(flash.lightAt(nx::Seconds{5.0F}, {}).has_value()) << "the original's casts none of its own";
    EXPECT_EQ(cs::muzzleFlashOf(rifle, 0, 0, {}, nx::Seconds{}, random), std::nullopt)
        << "a sprite of no frame";
}

TEST(MuzzleFlashTest, CounterStrikeOnlinesKeysStyleItsFlash) {
    const cs::MuzzleFlashStyle drakar = cs::muzzleFlashStyleOf(kDrakarOptions, std::nullopt);
    EXPECT_EQ(drakar.sprite, "sprites/muzzleflash341.spr");
    EXPECT_FLOAT_EQ(drakar.scale, 0.16F);
    EXPECT_EQ(drakar.firstFrame, 0U);
    EXPECT_FLOAT_EQ(drakar.frameRate, 30.0F);
    EXPECT_FLOAT_EQ(drakar.time.count(), 999.0F);
    EXPECT_FLOAT_EQ(drakar.roll.value(), 20.0F) << "rolled at first as the original's of 0";
    EXPECT_FLOAT_EQ(drakar.rollRate.value(), 60.0F) << "R2: 2 degrees a frame of its 30 a second";
    EXPECT_FLOAT_EQ(drakar.brightness, 1.0F);

    // A key the options lack, or whose value is not a number of 0 or more, is the original's of 0.
    const cs::MuzzleFlashStyle partial = cs::muzzleFlashStyleOf("#S0.3  Pfast T-1 U Iten F2", std::nullopt);
    EXPECT_EQ(partial.sprite, "sprites/muzzleflash1.spr");
    EXPECT_FLOAT_EQ(partial.scale, 0.3F);
    EXPECT_FLOAT_EQ(partial.frameRate, 0.0F);
    EXPECT_FLOAT_EQ(partial.time.count(), 0.01F);
    EXPECT_EQ(partial.firstFrame, 2U);
    EXPECT_FLOAT_EQ(partial.rollRate.value(), 0.0F) << "no R: it does not turn";
    EXPECT_FLOAT_EQ(cs::muzzleFlashStyleOf("#R2 P30 Rfast", std::nullopt).rollRate.value(), 60.0F)
        << "a value that is not a number leaves the one before";
}

TEST(MuzzleFlashTest, AFlashTurnsCounterClockwiseAsItShows) {
    nx::Random random{5, "test"};
    const cs::MuzzleFlash flash =
        shown(cs::muzzleFlashStyleOf(kDrakarOptions, std::nullopt), 12, nx::Seconds{1.0F}, random);
    EXPECT_FLOAT_EQ(flash.rollAt(nx::Seconds{1.0F}).value(), flash.roll.value());
    EXPECT_NEAR(flash.rollAt(nx::Seconds{1.25F}).value(), flash.roll.value() + 15.0F, 1e-3F);
    EXPECT_FLOAT_EQ(flash.rollAt(nx::Seconds{0.5F}).value(), flash.roll.value()) << "not before it shows";

    cs::MuzzleFlashStyle clockwise = cs::muzzleFlashStyleOf("22", std::nullopt);
    clockwise.rollRate = nx::DegreesPerSecond{-90.0F};
    const cs::MuzzleFlash turned = shown(clockwise, 3, nx::Seconds{}, random);
    EXPECT_NEAR(turned.rollAt(nx::Seconds{0.01F}).value(), turned.roll.value() - 0.9F, 1e-4F);
}

TEST(MuzzleFlashTest, AFlashPlaysItsFramesOnceAtItsRateForItsTimeAtMost) {
    nx::Random random{2, "test"};
    const cs::MuzzleFlashStyle drakar = cs::muzzleFlashStyleOf(kDrakarOptions, std::nullopt);
    const cs::MuzzleFlash flash = shown(drakar, 12, nx::Seconds{1.0F}, random);
    EXPECT_FLOAT_EQ(flash.frameAt(nx::Seconds{1.0F}).value_or(-1.0F), 0.0F);
    EXPECT_NEAR(flash.frameAt(nx::Seconds{1.1F}).value_or(-1.0F), 3.0F, 1e-4F);
    EXPECT_NEAR(flash.frameAt(nx::Seconds{1.39F}).value_or(-1.0F), 11.7F, 1e-3F) << "its last";
    EXPECT_EQ(flash.frameAt(nx::Seconds{1.41F}), std::nullopt) << "past its last frame";
    EXPECT_TRUE(flash.isOverAt(nx::Seconds{1.41F}));

    cs::MuzzleFlashStyle brief = drakar;
    brief.time = nx::Seconds{0.1F};
    brief.firstFrame = 5;
    const cs::MuzzleFlash cut = shown(brief, 12, nx::Seconds{1.0F}, random);
    EXPECT_NEAR(cut.frameAt(nx::Seconds{1.05F}).value_or(-1.0F), 6.5F, 1e-3F);
    EXPECT_EQ(cut.frameAt(nx::Seconds{1.11F}), std::nullopt) << "past its time";

    brief.firstFrame = 12;
    EXPECT_EQ(cs::muzzleFlashOf(brief, 0, 12, {}, nx::Seconds{}, random), std::nullopt)
        << "past the sprite's";
}

TEST(MuzzleFlashTest, AWeaponsLookIsOverItsEventsStyle) {
    const cs::MuzzleFlashLight light{
        .color = {3000.0F, 1800.0F, 600.0F}, .reach = nx::HammerUnits{96.0F}, .time = nx::Seconds{0.06F}};
    const cs::MuzzleFlashLook look{.sprite = "sprites/drakarFlash.spr",
                                   .firstFrame = 2,
                                   .frameRate = 0.0F,
                                   .roll = nx::Degrees{0.0F},
                                   .rollRate = nx::DegreesPerSecond{-30.0F},
                                   .brightness = 6.0F,
                                   .light = light};
    const cs::MuzzleFlashStyle style = cs::muzzleFlashStyleOf(kDrakarOptions, look);
    EXPECT_EQ(style.sprite, "sprites/drakarFlash.spr");
    EXPECT_FLOAT_EQ(style.scale, 0.16F) << "the event's";
    EXPECT_EQ(style.firstFrame, 2U);
    EXPECT_FLOAT_EQ(style.frameRate, 0.0F) << "held";
    EXPECT_FLOAT_EQ(style.time.count(), 999.0F) << "the event's";
    EXPECT_FLOAT_EQ(style.roll.value(), 0.0F);
    EXPECT_FLOAT_EQ(style.rollRate.value(), -30.0F) << "over the events' R";
    EXPECT_FLOAT_EQ(style.brightness, 6.0F);
    ASSERT_TRUE(style.light.has_value());
    EXPECT_EQ(style.light->color, light.color);

    // Over the original's too: its random frame and its time stay when the look does not give them.
    const cs::MuzzleFlashStyle original = cs::muzzleFlashStyleOf("22", cs::MuzzleFlashLook{.scale = 0.4F});
    EXPECT_EQ(original.sprite, "sprites/muzzleflash3.spr");
    EXPECT_FLOAT_EQ(original.scale, 0.4F);
    EXPECT_EQ(original.firstFrame, std::nullopt);
    EXPECT_FLOAT_EQ(original.time.count(), 0.01F);
}

TEST(MuzzleFlashTest, ItsLightDimsToNothingOverItsTimeOutlastingItsFrames) {
    cs::MuzzleFlashStyle style = cs::muzzleFlashStyleOf("22", std::nullopt);
    style.light = cs::MuzzleFlashLight{
        .color = {1000.0F, 500.0F, 250.0F}, .reach = nx::HammerUnits{128.0F}, .time = nx::Seconds{0.1F}};
    nx::Random random{3, "test"};
    const cs::MuzzleFlash flash = shown(style, 3, nx::Seconds{2.0F}, random);

    // From where its anchor is when it is drawn.
    const nx::Vec3 muzzle{.x = 31.0F, .z = -1.0F};
    const std::optional<nx::render::StudioLight> full = flash.lightAt(nx::Seconds{2.0F}, muzzle);
    ASSERT_TRUE(full.has_value());
    EXPECT_EQ(full->position, muzzle);
    EXPECT_FLOAT_EQ(full->reach, 128.0F);
    EXPECT_FLOAT_EQ(full->color.x, 1000.0F);
    const std::optional<nx::render::StudioLight> half = flash.lightAt(nx::Seconds{2.05F}, muzzle);
    ASSERT_TRUE(half.has_value());
    EXPECT_NEAR(half->color.y, 250.0F, 0.1F);
    EXPECT_EQ(flash.frameAt(nx::Seconds{2.05F}), std::nullopt) << "its sprite is over";
    EXPECT_FALSE(flash.isOverAt(nx::Seconds{2.05F})) << "its light is not";
    EXPECT_EQ(flash.lightAt(nx::Seconds{2.11F}, muzzle), std::nullopt);
    EXPECT_TRUE(flash.isOverAt(nx::Seconds{2.11F}));
}

[[nodiscard]] auto anchorsOf(const cs::MuzzleFlashes& flashes) -> std::vector<cs::MuzzleFlashAnchor> {
    return flashes.shown() | std::views::transform(&cs::MuzzleFlash::anchor) | std::ranges::to<std::vector>();
}

TEST(MuzzleFlashTest, AMuzzleShowsOneFlashAtATimeAndTheWeaponInHandsAlone) {
    nx::Random random{4, "test"};
    const cs::MuzzleFlashStyle drakar = cs::muzzleFlashStyleOf(kDrakarOptions, std::nullopt);
    const cs::MuzzleFlashAnchor second{.weapon = kAnchor.weapon, .attachment = 1};
    cs::MuzzleFlashes flashes;
    flashes.show(shown(drakar, 12, nx::Seconds{1.0F}, random));
    flashes.show(shown(drakar, 12, nx::Seconds{1.0F}, random, second));
    flashes.show(shown(drakar, 12, nx::Seconds{1.1F}, random));
    EXPECT_EQ(anchorsOf(flashes), (std::vector{second, kAnchor})) << "a shot restarts its muzzle's flash";
    EXPECT_FLOAT_EQ(flashes.shown().back().start.count(), 1.1F);

    flashes.forget(nx::Seconds{1.2F}, kAnchor.weapon);
    EXPECT_EQ(flashes.shown().size(), 2U) << "both still playing";
    flashes.forget(nx::Seconds{1.45F}, kAnchor.weapon);
    EXPECT_EQ(anchorsOf(flashes), (std::vector{kAnchor})) << "the other muzzle's flash played its last frame";
    flashes.forget(nx::Seconds{1.46F}, cs::WeaponIndex{3});
    EXPECT_TRUE(flashes.shown().empty()) << "another weapon in hand";

    flashes.show(shown(drakar, 12, nx::Seconds{2.0F}, random));
    flashes.forget(nx::Seconds{2.0F}, std::nullopt);
    EXPECT_TRUE(flashes.shown().empty()) << "no weapon in hand";
}

/// The bytes of an SPR v2 file of one frame of one pixel.
[[nodiscard]] auto onePixelSprite() -> std::string {
    std::string bytes = "IDSP";
    const auto append = [&bytes](auto value) {
        const auto raw = std::bit_cast<std::array<char, sizeof(value)>>(value);
        bytes.append(raw.begin(), raw.end());
    };
    for (const std::int32_t field : {2, 2, 1}) { // version, parallel, additive
        append(field);
    }
    append(1.0F);                                // radius
    for (const std::int32_t field : {1, 1, 1}) { // the largest frame, the frames
        append(field);
    }
    append(0.0F);            // beam length
    append(std::int32_t{0}); // synchronized
    append(std::uint16_t{256});
    bytes.append(std::size_t{256} * 3, '\x7F');
    for (const std::int32_t field : {0, 0, 0, 1, 1}) { // a single frame: origin, size
        append(field);
    }
    bytes.push_back('\x01');
    return bytes;
}

TEST(MuzzleFlashTest, ItsSpritesAreReadOnceByTheirPathsThoseMissingLeftOut) {
    const nx::test::TemporaryDirectory files;
    files.write("sprites/muzzleflash1.spr", onePixelSprite());
    files.write("sprites/broken.spr", "IDSQ");
    auto vfs = nx::assets::Vfs::create(std::array{files.path()});
    ASSERT_TRUE(vfs.has_value()) << nx::describe(vfs.error());

    cs::MuzzleFlashSprites sprites;
    const std::vector<std::string> paths{"sprites/muzzleflash1.spr", "sprites/missing.spr",
                                         "sprites/broken.spr"};
    const std::vector<std::string> missing = sprites.read(*vfs, paths);
    ASSERT_EQ(missing.size(), 2U);
    EXPECT_TRUE(missing[0].starts_with("sprites/missing.spr (")) << missing[0];
    EXPECT_TRUE(missing[1].starts_with("sprites/broken.spr (")) << missing[1];
    EXPECT_EQ(sprites.count(), 3U);
    EXPECT_EQ(sprites.indexOf("sprites/muzzleflash1.spr"), 0U);
    EXPECT_EQ(sprites.indexOf("sprites/broken.spr"), 2U) << "left out, yet read";
    EXPECT_EQ(sprites.indexOf("sprites/muzzleflash2.spr"), std::nullopt);

    EXPECT_TRUE(sprites.read(*vfs, paths).empty()) << "each read once";
    EXPECT_EQ(sprites.count(), 3U);
    EXPECT_TRUE(sprites.read(*vfs, std::vector<std::string>{"sprites/muzzleflash1.spr"}).empty());
}

} // namespace
