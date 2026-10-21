// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.ecs;

#include "contractRecorder.hpp"

namespace {

using nx::ecs::ComponentError;
using nx::ecs::ComponentRegistry;

struct Position {
    float x = 0;
    float y = 0;
};

struct Name {
    std::string text;
};

struct OnGround {};

struct alignas(128) OverAligned {
    float value = 0;
};

TEST(ComponentTest, DescribesTheType) {
    const auto position = nx::ecs::describeComponent<Position>("test::Position");
    EXPECT_EQ(position.name, "test::Position");
    EXPECT_EQ(position.stableId, nx::StableId::fromName("test::Position"));
    EXPECT_EQ(position.size, sizeof(Position));
    EXPECT_EQ(position.alignment, alignof(Position));
    EXPECT_EQ(position.relocate, nullptr); // trivially copyable: moved with memcpy
    EXPECT_EQ(position.destroy, nullptr);

    const auto name = nx::ecs::describeComponent<Name>("test::Name");
    EXPECT_NE(name.relocate, nullptr);
    EXPECT_NE(name.destroy, nullptr);

    const auto tag = nx::ecs::describeComponent<OnGround>("test::OnGround");
    EXPECT_TRUE(tag.isTag());
}

TEST(ComponentRegistryTest, AssignsDenseIdsInRegistrationOrder) {
    ComponentRegistry registry;
    const auto position = registry.add<Position>("test::Position");
    const auto name = registry.add<Name>("test::Name");
    ASSERT_TRUE(position.has_value());
    ASSERT_TRUE(name.has_value());

    EXPECT_EQ(std::to_underlying(*position), 0U);
    EXPECT_EQ(std::to_underlying(*name), 1U);
    EXPECT_EQ(registry.find<Position>(), *position);
    EXPECT_EQ(registry.findByStableId(nx::StableId::fromName("test::Name")), *name);
    EXPECT_EQ(registry.info(*name).name, "test::Name");
    EXPECT_EQ(registry.find<OnGround>(), std::nullopt);
}

TEST(ComponentRegistryTest, AnUnknownTypeBreaksTheContractAndReadsAsATag) {
    const nx::test::ContractRecorder recorder;
    ComponentRegistry registry;
    ASSERT_TRUE(registry.add<Position>("test::Position").has_value());

    const nx::ecs::ComponentInfo& unknown = registry.info(nx::ecs::ComponentTypeId{1});
    EXPECT_EQ(recorder.count(), 1);
    EXPECT_TRUE(unknown.isTag()) << "observed: no storage is touched";
}

TEST(ComponentRegistryTest, RejectsATypeRegisteredTwice) {
    ComponentRegistry registry;
    ASSERT_TRUE(registry.add<Position>("test::Position").has_value());
    const auto again = registry.add<Position>("test::OtherPosition");
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error().code, ComponentError::TypeAlreadyRegistered);
}

TEST(ComponentRegistryTest, RejectsANameRegisteredTwice) {
    ComponentRegistry registry;
    ASSERT_TRUE(registry.add<Position>("test::Shared").has_value());
    const auto again = registry.add<Name>("test::Shared");
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error().code, ComponentError::NameAlreadyRegistered);
}

TEST(ComponentRegistryTest, RejectsDifferentNamesWithTheSameStableId) {
    ComponentRegistry registry;
    ASSERT_TRUE(registry.add<Position>("test::Position").has_value());

    // Run-time components carry their own id, so a collision can be built on purpose.
    auto colliding = nx::ecs::describeComponent<Name>("mod.plugin::Name");
    colliding.stableId = nx::StableId::fromName("test::Position");
    const auto result = registry.add(std::move(colliding));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ComponentError::StableIdCollision);
}

TEST(ComponentRegistryTest, RejectsAlignmentBeyondTheChunkAlignment) {
    ComponentRegistry registry;
    const auto result = registry.add<OverAligned>("test::OverAligned");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ComponentError::InvalidAlignment);
}

TEST(ComponentRegistryTest, RejectsAnEmptyName) {
    ComponentRegistry registry;
    const auto result = registry.add<Position>("");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ComponentError::InvalidName);
}

} // namespace
