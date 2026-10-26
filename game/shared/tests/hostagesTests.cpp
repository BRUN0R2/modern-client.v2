// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The hostages as entities of the game's world, as the original's CHostage makes and plays them: an
// idle sequence drawn by weight, dropped to the floor, its frame advanced as it thinks a thirtieth of
// a second apart, put back where it started as a round starts while the bonus counted lets it, a box
// that stops a player, its hitboxes met where its last think left them, and how the client shows it.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.ecs;
import nx.collision;
import nx.game;
import cs.shared;

#include "triangleModel.hpp"

namespace {

using nx::Vec3;
using nx::collision::BrushHulls;
using nx::collision::Contents;
using nx::collision::HullNode;
using nx::collision::HullNodeIndex;
using nx::collision::HullRoots;

/// The server's trace stops this far before what stops it.
constexpr float kStopDistance = 0.03125F;
constexpr float kTolerance = 1e-4F;

/// A world whose standing hull is solid below z = 36: a floor at z = 0 for a standing box.
[[nodiscard]] auto floorHulls() -> BrushHulls {
    auto hulls = BrushHulls::create(nx::collision::BrushHullsDesc{
        .planes = {nx::Plane{.normal = {.z = 1.0F}, .distance = 36.0F}},
        .clipNodes = {HullNode{.plane = {0}, .children = {Contents::Empty, Contents::Solid}}},
        .models = {nx::collision::BrushModel{
            .hulls = HullRoots{{Contents::Empty, HullNodeIndex{0}, Contents::Empty, Contents::Empty}}}}});
    EXPECT_TRUE(hulls.has_value());
    return std::move(hulls).value();
}

/// The triangle model, as the game plays it: two idle sequences of a weight of 1 each, "idle" of two
/// frames that loops and "wave" of one, a hitbox on its second bone.
class PlayedTriangle {
public:
    PlayedTriangle() {
        auto view = nx::assets::MdlView::parse(m_model.bytes);
        EXPECT_TRUE(view.has_value());
        m_view.emplace(std::move(view).value());
        for (std::uint32_t index = 0; index < m_view->sequences().size(); ++index) {
            const bool isInGroup = m_view->sequences()[index].group.value != 0;
            auto frames = m_view->decodeAnimation(nx::assets::SequenceIndex{index},
                                                  isInGroup ? m_model.groupFile : m_model.bytes);
            EXPECT_TRUE(frames.has_value());
            m_animations.push_back(std::move(frames).value());
        }
        m_played = {cs::PlayedModel{.view = &*m_view, .animations = m_animations}};
    }

    [[nodiscard]] auto models() const noexcept -> std::span<const cs::PlayedModel> { return m_played; }
    [[nodiscard]] auto view() const noexcept -> const nx::assets::MdlView& { return *m_view; }

private:
    nx::test::TriangleModel m_model;
    std::optional<nx::assets::MdlView> m_view;
    std::vector<nx::assets::AnimationFrames> m_animations;
    std::array<cs::PlayedModel, 1> m_played{};
};

/// `world` made ready for hostages that play `models`.
void makeReady(nx::ecs::World& world, std::span<const cs::PlayedModel> models) {
    EXPECT_TRUE(nx::game::registerComponents(world).has_value());
    EXPECT_TRUE(cs::registerHostages(world, models).has_value());
}

/// The hostages of `world`, in the map's order.
[[nodiscard]] auto hostagesOf(nx::ecs::World& world) -> std::vector<nx::ecs::Entity> {
    std::vector<std::pair<std::uint32_t, nx::ecs::Entity>> found;
    nx::ecs::Query<nx::ecs::Entity, const cs::Hostage&>{world}.forEach(
        [&found](nx::ecs::Entity entity, const cs::Hostage& hostage) {
            found.emplace_back(hostage.spawn, entity);
        });
    std::ranges::sort(found);
    return found | std::views::values | std::ranges::to<std::vector>();
}

/// Runs `world` for `ticks` ticks: what thinks, then the clock on.
void runTicks(nx::ecs::World& world, const BrushHulls& hulls, std::int64_t ticks) {
    for (std::int64_t tick = 0; tick < ticks; ++tick) {
        nx::game::runThinks(world, hulls);
        nx::game::advanceTick(world);
    }
}

TEST(HostagesTest, AHostageIdlesInASequenceDrawnByWeightDroppedToTheFloor) {
    const PlayedTriangle triangle;
    nx::ecs::World world;
    makeReady(world, triangle.models());
    const BrushHulls hulls = floorHulls();
    const std::vector<cs::HostageSpawn> spawns(64, cs::HostageSpawn{.origin = {.x = 4.0F, .z = 100.0F}});
    cs::spawnHostages(world, hulls, spawns);

    std::array<std::uint32_t, 2> drawn{};
    for (const nx::ecs::Entity entity : hostagesOf(world)) {
        const cs::Animation& animation = *world.get<cs::Animation>(entity);
        ASSERT_LT(animation.sequence.value, drawn.size());
        ++drawn.at(animation.sequence.value);
        EXPECT_FLOAT_EQ(animation.frame, 0.0F);
        EXPECT_EQ(world.get<cs::Hostage>(entity)->activity, cs::Activity::Idle);
        EXPECT_NEAR(world.get<nx::game::Transform>(entity)->origin.z, kStopDistance, kTolerance);
        EXPECT_NEAR(world.get<cs::Hostage>(entity)->start.z, kStopDistance, kTolerance)
            << "it starts where it was dropped";
    }
    EXPECT_GT(drawn[0], 16U) << "each of the two weighs as much";
    EXPECT_GT(drawn[1], 16U);
}

TEST(HostagesTest, AHostageThinksFirstIn100To200MillisecondsThenAThirtiethOfASecondApart) {
    const PlayedTriangle triangle;
    nx::ecs::World world;
    makeReady(world, triangle.models());
    const BrushHulls hulls = floorHulls();
    const std::array spawns{cs::HostageSpawn{.origin = {.z = 100.0F}}};
    cs::spawnHostages(world, hulls, spawns);
    const nx::ecs::Entity hostage = hostagesOf(world).front();
    const nx::TickRate rate = nx::game::tickTimeOf(world).rate;
    const nx::GameTime first = world.get<nx::game::NextThink>(hostage)->time;
    EXPECT_GE(first, nx::GameTime{}.after(nx::Seconds{0.1F}, rate));
    EXPECT_LE(first, nx::GameTime{}.after(nx::Seconds{0.2F}, rate));

    runTicks(world, hulls, first.tick() + 1);
    const nx::GameTime next = world.get<nx::game::NextThink>(hostage)->time;
    EXPECT_NEAR(next.since(first, rate).count(), 1.0F / 30.0F, kTolerance);
    const cs::Animation& animation = *world.get<cs::Animation>(hostage);
    EXPECT_EQ(animation.advancedAt, first) << "its frame advanced to when its think was due";
}

TEST(HostagesTest, AHostagesFrameAdvancesAsItThinks) {
    const PlayedTriangle triangle;
    nx::ecs::World world;
    makeReady(world, triangle.models());
    const BrushHulls hulls = floorHulls();
    const std::array spawns{cs::HostageSpawn{.origin = {.z = 100.0F}}};
    cs::spawnHostages(world, hulls, spawns);
    const nx::ecs::Entity hostage = hostagesOf(world).front();
    // "wave", of a single frame: its frame goes kFrameScale a second.
    cs::Animation& played = *world.get<cs::Animation>(hostage);
    played = cs::Animation{.sequence = nx::assets::SequenceIndex{1}};
    cs::resetSequence(played, triangle.view().sequences()[1], nx::GameTime{});
    const nx::TickRate rate = nx::game::tickTimeOf(world).rate;
    runTicks(world, hulls, 50);

    const cs::Animation& animation = *world.get<cs::Animation>(hostage);
    EXPECT_GT(animation.advancedAt, nx::GameTime{}.after(nx::Seconds{0.4F}, rate));
    EXPECT_NEAR(animation.frame, animation.advancedAt.since(nx::GameTime{}, rate).count() * cs::kFrameScale,
                0.01F)
        << "advanced from 0 to its last think at its frame's speed";
}

TEST(HostagesTest, ARoundPutsTheHostagesBackWhereTheyStartedWhileTheBonusLetsIt) {
    const PlayedTriangle triangle;
    nx::ecs::World world;
    makeReady(world, triangle.models());
    const BrushHulls hulls = floorHulls();
    const std::vector<cs::HostageSpawn> spawns(3, cs::HostageSpawn{.origin = {.z = 100.0F}});
    cs::spawnHostages(world, hulls, spawns);
    const std::vector<nx::ecs::Entity> hostages = hostagesOf(world);
    for (const nx::ecs::Entity entity : hostages) {
        nx::game::Transform& transform = *world.get<nx::game::Transform>(entity);
        transform.origin = {.x = 50.0F, .z = 200.0F};
        transform.angles = {.yaw = nx::Degrees{90.0F}};
    }
    runTicks(world, hulls, 30);

    // Two of 150 reach 300: the third is left where it is.
    cs::repositionHostages(world, hulls,
                           cs::HostageBonus{.perHostage = nx::Money{150}, .upTo = nx::Money{300}});
    const nx::game::TickTime time = nx::game::tickTimeOf(world);
    for (std::size_t index = 0; index < 2; ++index) {
        const nx::game::Transform& transform = *world.get<nx::game::Transform>(hostages[index]);
        EXPECT_FLOAT_EQ(transform.origin.x, 0.0F) << index;
        EXPECT_NEAR(transform.origin.z, kStopDistance, kTolerance) << index;
        EXPECT_FLOAT_EQ(transform.angles.yaw.value(), 0.0F) << index;
        const nx::GameTime think = world.get<nx::game::NextThink>(hostages[index])->time;
        EXPECT_GE(think, time.now.after(nx::Seconds{0.1F}, time.rate)) << index;
        EXPECT_LE(think, time.now.after(nx::Seconds{0.2F}, time.rate)) << index;
    }
    EXPECT_FLOAT_EQ(world.get<nx::game::Transform>(hostages[2])->origin.x, 50.0F) << "past the bonus's most";
}

TEST(HostagesTest, AHostageStopsAPlayerAsABox) {
    const PlayedTriangle triangle;
    nx::ecs::World world;
    makeReady(world, triangle.models());
    const BrushHulls hulls = floorHulls();
    const std::array spawns{cs::HostageSpawn{.origin = {.x = 30.0F, .z = 100.0F}}};
    cs::spawnHostages(world, hulls, spawns);

    const std::vector<cs::SolidBox> boxes = cs::hostageBoxes(world);
    ASSERT_EQ(boxes.size(), 1U);
    EXPECT_FLOAT_EQ(boxes[0].origin.x, 30.0F);
    EXPECT_EQ(boxes[0].box.max.z, cs::kHostageBox.max.z);
}

TEST(HostagesTest, ATraceMeetsAHostageAsSvHullForStudioModelChooses) {
    const PlayedTriangle triangle;
    nx::ecs::World world;
    makeReady(world, triangle.models());
    const BrushHulls hulls = floorHulls();
    const std::array spawns{cs::HostageSpawn{.origin = {.z = 100.0F}}};
    cs::spawnHostages(world, hulls, spawns);
    const nx::collision::Trace nothing{.fraction = 1.0F};

    // A hull meets its box grown by the hull's.
    const std::optional<nx::collision::Trace> boxed = cs::traceHostages(
        world, nx::collision::Hull::Standing, {.x = -100.0F, .z = 40.0F}, {.x = 100.0F, .z = 40.0F}, nothing);
    ASSERT_TRUE(boxed.has_value());
    EXPECT_NEAR(boxed->end.x, -10.0F - 16.0F, 0.1F);
    // A line meets nothing where its box is far from the hostage's.
    EXPECT_FALSE(cs::traceHostages(world, nx::collision::Hull::Point, {.x = -100.0F, .z = 500.0F},
                                   {.x = 100.0F, .z = 500.0F}, nothing)
                     .has_value());
    // What met the way before it stays met.
    const nx::collision::Trace nearer{.fraction = 0.1F};
    EXPECT_FALSE(cs::traceHostages(world, nx::collision::Hull::Standing, {.x = -100.0F, .z = 40.0F},
                                   {.x = 100.0F, .z = 40.0F}, nearer)
                     .has_value());
}

TEST(HostagesTest, TheClientShowsAHostageWhereItEstimatesItFromItsLastThink) {
    const PlayedTriangle triangle;
    nx::ecs::World world;
    makeReady(world, triangle.models());
    const BrushHulls hulls = floorHulls();
    const std::array spawns{cs::HostageSpawn{.origin = {.x = 8.0F, .z = 100.0F}}};
    cs::spawnHostages(world, hulls, spawns);
    runTicks(world, hulls, 25);
    const nx::ecs::Entity hostage = hostagesOf(world).front();
    const cs::Animation& animation = *world.get<cs::Animation>(hostage);
    const nx::TickRate rate = nx::game::tickTimeOf(world).rate;
    const nx::GameTime later = animation.advancedAt.after(nx::Seconds{0.02F}, rate);

    const std::vector<cs::ShownHostage> shown = cs::hostagesShownAt(world, later);
    ASSERT_EQ(shown.size(), 1U);
    EXPECT_EQ(shown[0].spawn, 0U);
    EXPECT_FLOAT_EQ(shown[0].origin.x, 8.0F);
    EXPECT_EQ(shown[0].sequence, animation.sequence);
    const nx::assets::StudioSequence& sequence = triangle.view().sequences()[animation.sequence.value];
    EXPECT_FLOAT_EQ(
        shown[0].frame.value(),
        cs::estimatedFrameOf(sequence, animation.frame, nx::Seconds{0.02F}, animation.framerate).value());
}

} // namespace
