// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.ecs;

namespace {

using nx::ecs::EventReader;
using nx::ecs::Events;
using nx::ecs::EventWriter;
using nx::ecs::World;

struct PlayerKilled {
    int victim = 0;
};

[[nodiscard]] auto readAll(EventReader<PlayerKilled>& reader) -> std::vector<int> {
    std::vector<int> victims;
    reader.forEach([&victims](const PlayerKilled& event) { victims.push_back(event.victim); });
    return victims;
}

TEST(EventTest, AReaderSeesEachEventOnce) {
    Events<PlayerKilled> events;
    std::uint64_t cursor = 0;
    EventReader<PlayerKilled> reader{events, cursor};
    EventWriter<PlayerKilled> writer{events};

    writer.send({.victim = 1});
    writer.send({.victim = 2});
    EXPECT_TRUE(reader.hasUnread());
    EXPECT_EQ(readAll(reader), (std::vector{1, 2}));
    EXPECT_FALSE(reader.hasUnread());
    EXPECT_TRUE(readAll(reader).empty());

    writer.send({.victim = 3});
    EXPECT_EQ(readAll(reader), std::vector{3});
}

TEST(EventTest, EventsLiveUntilTheEndOfTheNextTick) {
    Events<PlayerKilled> events;
    events.send({.victim = 7});
    events.advance(); // end of tick N

    std::uint64_t lateCursor = 0;
    EventReader<PlayerKilled> late{events, lateCursor};
    EXPECT_EQ(readAll(late), std::vector{7}); // still readable during tick N+1

    events.advance(); // end of tick N+1
    std::uint64_t laterCursor = 0;
    EventReader<PlayerKilled> later{events, laterCursor};
    EXPECT_FALSE(later.hasUnread());
    EXPECT_TRUE(readAll(later).empty());
}

TEST(EventTest, ReadersAreIndependent) {
    Events<PlayerKilled> events;
    std::uint64_t firstCursor = 0;
    std::uint64_t secondCursor = 0;
    EventReader<PlayerKilled> first{events, firstCursor};
    EventReader<PlayerKilled> second{events, secondCursor};

    events.send({.victim = 4});
    EXPECT_EQ(readAll(first), std::vector{4});
    EXPECT_EQ(readAll(second), std::vector{4});
}

TEST(EventTest, TheWorldAdvancesEveryEventQueue) {
    World world;
    world.addEvents<PlayerKilled>();
    world.resource<Events<PlayerKilled>>()->send({.victim = 9});
    world.addEvents<PlayerKilled>(); // already there: kept as it is

    world.advanceEvents();
    world.advanceEvents();
    std::uint64_t cursor = 0;
    EventReader<PlayerKilled> reader{*world.resource<Events<PlayerKilled>>(), cursor};
    EXPECT_FALSE(reader.hasUnread());
}

} // namespace
