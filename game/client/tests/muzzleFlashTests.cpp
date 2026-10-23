// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The client events a view model's sequence passes from a frame to another, the attachment of each
// muzzle flash event, and the flash an event's number makes: its sprite, size, roll and frame.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.assets;
import cs.client;

namespace {

using nx::assets::StudioEvent;
using nx::assets::StudioSequence;

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

TEST(MuzzleFlashTest, AnEventsNumberChoosesTheSpriteAndItsSize) {
    const std::array<std::size_t, 3> frames{3, 3, 3};
    const nx::Vec3 origin{.x = 1.0F, .y = 2.0F, .z = 3.0F};
    nx::Random random{1, "test"};
    // The AK-47's 22: the third sprite, two tenths; the USP's 11: the second, a tenth; 0, the first, half.
    const cs::MuzzleFlash rifle = cs::muzzleFlashOf("22", origin, nx::Seconds{5.0F}, frames, random);
    EXPECT_EQ(rifle.sprite, 2U);
    EXPECT_FLOAT_EQ(rifle.scale, 0.2F);
    EXPECT_EQ(rifle.origin, origin);
    EXPECT_FLOAT_EQ(rifle.until.count(), 5.01F);
    const cs::MuzzleFlash pistol = cs::muzzleFlashOf("11", origin, nx::Seconds{}, frames, random);
    EXPECT_EQ(pistol.sprite, 1U);
    EXPECT_FLOAT_EQ(pistol.scale, 0.1F);
    const cs::MuzzleFlash none = cs::muzzleFlashOf("0", origin, nx::Seconds{}, frames, random);
    EXPECT_EQ(none.sprite, 0U);
    EXPECT_FLOAT_EQ(none.scale, 0.5F);
    EXPECT_EQ(cs::muzzleFlashOf("not a number", origin, nx::Seconds{}, frames, random).sprite, 0U);

    for (int draw = 0; draw < 200; ++draw) {
        const cs::MuzzleFlash first = cs::muzzleFlashOf("10", origin, nx::Seconds{}, frames, random);
        EXPECT_LE(first.roll.value(), 20.0F) << "a rifle's flash rolls 20 degrees at most";
        EXPECT_LT(first.frame, 3.0F);
        const cs::MuzzleFlash other = cs::muzzleFlashOf("11", origin, nx::Seconds{}, frames, random);
        EXPECT_LT(other.roll.value(), 360.0F);
    }
}

} // namespace
