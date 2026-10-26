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
import nx.platform;
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

/// Where the triangle model's header counts its hitboxes; how long a sequence's record is, and where
/// its activity is in it.
constexpr std::size_t kHitboxCountAt = 156;
constexpr std::size_t kSequenceRecordSize = 176;
constexpr std::size_t kActivityAt = 40;

/// The triangle model, as the game plays it: two idle sequences of a weight of 1 each, "idle" of two
/// frames that loops and "wave" of one, a hitbox on its second bone, or none; "wave" of `waveActivity`
/// instead when given.
class PlayedTriangle {
public:
    explicit PlayedTriangle(bool hasHitboxes = true,
                            std::optional<cs::Activity> waveActivity = std::nullopt) {
        if (!hasHitboxes) {
            m_model.put(kHitboxCountAt, std::int32_t{0});
        }
        if (waveActivity) {
            m_model.put(m_model.sequences + kSequenceRecordSize + kActivityAt,
                        std::to_underlying(*waveActivity));
        }
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

/// The repository's data/cstrike/hitGroups.toml, read once.
[[nodiscard]] auto repositoryHitGroups() -> const cs::HitGroupRules& {
    static const cs::HitGroupRules rules = [] {
        const std::filesystem::path file =
            std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "hitGroups.toml";
        auto text = nx::platform::readTextFile(file);
        EXPECT_TRUE(text.has_value());
        auto read = cs::readHitGroupRules(text.value_or(std::string{}), "hitGroups.toml");
        EXPECT_TRUE(read.has_value()) << (read ? "" : nx::describe(read.error()));
        return read.value_or(cs::HitGroupRules{});
    }();
    return rules;
}

/// `world` made ready for hostages that play `models`, taking attacks by the repository's hit groups.
void makeReady(nx::ecs::World& world, std::span<const cs::PlayedModel> models) {
    EXPECT_TRUE(nx::game::registerComponents(world).has_value());
    EXPECT_TRUE(cs::registerHostages(world, models, repositoryHitGroups()).has_value());
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

/// `count` hostages 40 units apart along x, from x = 0, 100 units up: none on another's head.
[[nodiscard]] auto spawnsApart(std::size_t count) -> std::vector<cs::HostageSpawn> {
    std::vector<cs::HostageSpawn> spawns;
    for (std::size_t index = 0; index < count; ++index) {
        spawns.push_back(cs::HostageSpawn{.spawn = nx::narrow<std::uint32_t>(index),
                                          .origin = {.x = 40.0F * static_cast<float>(index), .z = 100.0F}});
    }
    return spawns;
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
    cs::spawnHostages(world, hulls, spawnsApart(64));

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
    cs::spawnHostages(world, hulls, spawnsApart(3));
    const std::vector<nx::ecs::Entity> hostages = hostagesOf(world);
    for (const nx::ecs::Entity entity : hostages) {
        nx::game::Transform& transform = *world.get<nx::game::Transform>(entity);
        transform.origin = {.x = transform.origin.x + 1000.0F, .z = 200.0F};
        transform.angles = {.yaw = nx::Degrees{90.0F}};
    }
    runTicks(world, hulls, 30);

    // Two of 150 reach 300: the third is left where it is.
    cs::repositionHostages(world, hulls,
                           cs::HostageBonus{.perHostage = nx::Money{150}, .upTo = nx::Money{300}});
    const nx::game::TickTime time = nx::game::tickTimeOf(world);
    for (std::size_t index = 0; index < 2; ++index) {
        const nx::game::Transform& transform = *world.get<nx::game::Transform>(hostages[index]);
        EXPECT_FLOAT_EQ(transform.origin.x, 40.0F * static_cast<float>(index)) << index;
        EXPECT_NEAR(transform.origin.z, kStopDistance, kTolerance) << index;
        EXPECT_FLOAT_EQ(transform.angles.yaw.value(), 0.0F) << index;
        const nx::GameTime think = world.get<nx::game::NextThink>(hostages[index])->time;
        EXPECT_GE(think, time.now.after(nx::Seconds{0.1F}, time.rate)) << index;
        EXPECT_LE(think, time.now.after(nx::Seconds{0.2F}, time.rate)) << index;
    }
    EXPECT_FLOAT_EQ(world.get<nx::game::Transform>(hostages[2])->origin.x, 1080.0F)
        << "past the bonus's most";
}

TEST(HostagesTest, AHostageDroppedWhereAnotherIsLandsOnItsHead) {
    const PlayedTriangle triangle;
    nx::ecs::World world;
    makeReady(world, triangle.models());
    const BrushHulls hulls = floorHulls();
    const std::vector<cs::HostageSpawn> spawns(2, cs::HostageSpawn{.origin = {.z = 100.0F}});
    cs::spawnHostages(world, hulls, spawns);

    const std::vector<nx::ecs::Entity> hostages = hostagesOf(world);
    EXPECT_NEAR(world.get<nx::game::Transform>(hostages[0])->origin.z, kStopDistance, kTolerance);
    EXPECT_NEAR(world.get<nx::game::Transform>(hostages[1])->origin.z,
                kStopDistance + cs::kHostageBox.max.z + kStopDistance, kTolerance)
        << "DROP_TO_FLOOR meets the other's box";
}

TEST(HostagesTest, AHostageStopsAPlayerAsABox) {
    const PlayedTriangle triangle;
    nx::ecs::World world;
    makeReady(world, triangle.models());
    const BrushHulls hulls = floorHulls();
    const std::array spawns{cs::HostageSpawn{.origin = {.x = 30.0F, .z = 100.0F}}};
    cs::spawnHostages(world, hulls, spawns);

    const std::vector<cs::SolidBox> boxes = cs::solidBoxesOf(world);
    ASSERT_EQ(boxes.size(), 1U);
    EXPECT_FLOAT_EQ(boxes[0].origin.x, 30.0F);
    EXPECT_EQ(boxes[0].box.max.z, cs::kHostageBox.max.z);
    const nx::game::Classified* const classified = world.get<nx::game::Classified>(hostagesOf(world).front());
    ASSERT_NE(classified, nullptr);
    EXPECT_EQ(classified->classification, nx::game::Classification::HumanPassive) << "Classify";
}

TEST(HostagesTest, ATraceMeetsAHostageAsSvHullForStudioModelChooses) {
    const PlayedTriangle triangle;
    nx::ecs::World world;
    makeReady(world, triangle.models());
    const BrushHulls hulls = floorHulls();
    const std::array spawns{cs::HostageSpawn{.origin = {.z = 100.0F}}};
    cs::spawnHostages(world, hulls, spawns);
    const nx::ecs::Entity hostage = hostagesOf(world).front();

    // A hull meets its box grown by the hull's.
    const nx::game::SolidHit boxed =
        nx::game::traceSolidsHit(world, hulls,
                                 nx::game::hullTrace(nx::collision::Hull::Standing,
                                                     {.x = -100.0F, .z = 40.0F}, {.x = 100.0F, .z = 40.0F}));
    EXPECT_EQ(boxed.entity, hostage);
    EXPECT_NEAR(boxed.trace.end.x, -10.0F - 16.0F, 0.1F);
    // A line meets nothing where its box is far from the hostage's.
    EXPECT_FALSE(
        nx::game::traceSolidsHit(world, hulls,
                                 nx::game::hullTrace(nx::collision::Hull::Point, {.x = -100.0F, .z = 500.0F},
                                                     {.x = 100.0F, .z = 500.0F}))
            .entity.has_value());
}

TEST(HostagesTest, ALineThroughAHostageOfAModelWithoutHitboxesIsAllInSolid) {
    const PlayedTriangle triangle{false};
    nx::ecs::World world;
    makeReady(world, triangle.models());
    const BrushHulls hulls = floorHulls();
    const std::array spawns{cs::HostageSpawn{.origin = {.z = 100.0F}}};
    cs::spawnHostages(world, hulls, spawns);

    // The original's loop over the hitboxes of none leaves the trace all in solid, not stopped.
    const nx::game::SolidHit hit =
        nx::game::traceSolidsHit(world, hulls,
                                 nx::game::hullTrace(nx::collision::Hull::Point, {.x = -100.0F, .z = 30.0F},
                                                     {.x = 100.0F, .z = 30.0F}));
    EXPECT_EQ(hit.entity, hostagesOf(world).front());
    EXPECT_TRUE(hit.trace.isAllSolid);
    EXPECT_EQ(hit.trace.fraction, 1.0F);
}

TEST(HostagesTest, TheClientShowsAHostageWhereItEstimatesItFromWhatTheServerTellsOfItsLastThink) {
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

    const std::vector<cs::HostageState> states = cs::hostageStatesOf(world);
    ASSERT_EQ(states.size(), 1U);
    EXPECT_EQ(states[0].spawn, 0U);
    EXPECT_FLOAT_EQ(states[0].origin.x, 8.0F);
    EXPECT_EQ(states[0].sequence, animation.sequence);
    EXPECT_EQ(states[0].frame, animation.frame);
    EXPECT_EQ(states[0].advancedAt, animation.advancedAt);
    const nx::assets::StudioSequence& sequence = triangle.view().sequences()[animation.sequence.value];
    const cs::ShownHostage shown = cs::shownHostageOf(states[0], sequence, later, rate);
    EXPECT_EQ(shown.spawn, 0U);
    EXPECT_FLOAT_EQ(shown.origin.x, 8.0F);
    EXPECT_EQ(shown.sequence, animation.sequence);
    EXPECT_FLOAT_EQ(
        shown.frame.value(),
        cs::estimatedFrameOf(sequence, animation.frame, nx::Seconds{0.02F}, animation.framerate).value());
}

/// An attack of `damage` that met a hostage in hit group `group`.
[[nodiscard]] auto attackOf(float damage, cs::HitGroup group) -> nx::game::Attack {
    return nx::game::Attack{
        .damage = nx::Damage{damage},
        .trace = nx::collision::Trace{.fraction = 0.5F, .hitGroup = std::to_underlying(group)}};
}

TEST(HitGroupRulesTest, TheRepositorysSharesAreTheOriginals) {
    const cs::HitGroupRules& rules = repositoryHitGroups();
    // CBaseMonster::TraceAttack.
    EXPECT_EQ(rules.monster.of(std::to_underlying(cs::HitGroup::Head)), 3.0);
    EXPECT_EQ(rules.monster.of(std::to_underlying(cs::HitGroup::Stomach)), 1.5);
    EXPECT_EQ(rules.monster.of(std::to_underlying(cs::HitGroup::RightLeg)), 0.75);
    EXPECT_EQ(rules.monster.of(std::to_underlying(cs::HitGroup::Shield)), 0.0);
    EXPECT_EQ(rules.monster.of(42), 1.0) << "a group the game names none of";
    // CHostage::GetModifiedDamage.
    EXPECT_EQ(rules.hostage.of(std::to_underlying(cs::HitGroup::Generic)), 1.75);
    EXPECT_EQ(rules.hostage.of(std::to_underlying(cs::HitGroup::Head)), 2.5);
    EXPECT_EQ(rules.hostage.of(std::to_underlying(cs::HitGroup::LeftArm)), 0.75);
    EXPECT_EQ(rules.hostage.of(std::to_underlying(cs::HitGroup::LeftLeg)), 0.6);
    EXPECT_EQ(rules.hostage.of(42), 1.5);
}

TEST(HostageDamageTest, AnAttackIsScaledByWhereItMeetsTheHostage) {
    const PlayedTriangle triangle;
    nx::ecs::World world;
    makeReady(world, triangle.models());
    const BrushHulls hulls = floorHulls();
    cs::spawnHostages(world, hulls, spawnsApart(5));
    const std::vector<nx::ecs::Entity> hostages = hostagesOf(world);

    // The monsters' share, then the hostages', each on a hostage of its own.
    const std::array cases{std::tuple{cs::HitGroup::Generic, 15.0F * 1.75F},
                           std::tuple{cs::HitGroup::Chest, 15.0F * 1.5F * 1.5F},
                           std::tuple{cs::HitGroup::Stomach, 15.0F * 1.5F * 1.75F},
                           std::tuple{cs::HitGroup::LeftArm, 15.0F * 0.75F},
                           std::tuple{cs::HitGroup::RightLeg, 15.0F * 0.75F * 0.6F}};
    for (std::size_t index = 0; index < cases.size(); ++index) {
        const auto& [group, expected] = cases.at(index);
        const nx::game::Harm harm = nx::game::attackEntity(world, hostages[index], attackOf(15.0F, group));
        EXPECT_NEAR(harm.damage.value(), expected, 1e-4F) << std::to_underlying(group);
        EXPECT_FALSE(harm.isKilled);
        EXPECT_NEAR(world.get<cs::Hostage>(hostages[index])->health.value(),
                    cs::kHostageHealth.value() - expected, 1e-3F);
        EXPECT_EQ(world.get<cs::Hostage>(hostages[index])->lastHitGroup, std::to_underlying(group));
    }
    const nx::game::Harm shielded =
        nx::game::attackEntity(world, hostages.front(), attackOf(15.0F, cs::HitGroup::Shield));
    EXPECT_EQ(shielded.damage.value(), 0.0F) << "a shield takes it all";
}

TEST(HostageDamageTest, AHurtHostageFlinchesThenIdlesAgain) {
    const PlayedTriangle triangle{true, cs::Activity::SmallFlinch};
    nx::ecs::World world;
    makeReady(world, triangle.models());
    const BrushHulls hulls = floorHulls();
    cs::spawnHostages(world, hulls, spawnsApart(1));
    const nx::ecs::Entity hostage = hostagesOf(world).front();
    runTicks(world, hulls, 30);

    std::ignore = nx::game::attackEntity(world, hostage, attackOf(15.0F, cs::HitGroup::Chest));
    const nx::game::TickTime time = nx::game::tickTimeOf(world);
    EXPECT_EQ(world.get<cs::Hostage>(hostage)->activity, cs::Activity::SmallFlinch);
    EXPECT_EQ(world.get<cs::Animation>(hostage)->sequence, nx::assets::SequenceIndex{1});
    EXPECT_EQ(world.get<cs::Hostage>(hostage)->flinchUntil, time.now.after(nx::Seconds{0.75F}, time.rate));
    runTicks(world, hulls, 70);
    EXPECT_EQ(world.get<cs::Hostage>(hostage)->activity, cs::Activity::SmallFlinch) << "for 0.75 seconds";
    runTicks(world, hulls, 20);
    EXPECT_EQ(world.get<cs::Hostage>(hostage)->activity, cs::Activity::Idle)
        << "its tenth of a second's part";
    EXPECT_EQ(world.get<cs::Animation>(hostage)->sequence, nx::assets::SequenceIndex{0});
}

TEST(HostageDamageTest, AKilledHostagePlaysItsDeathAndIsNoLongerSolidNorThinks) {
    const PlayedTriangle triangle{true, cs::Activity::DieHeadshot};
    nx::ecs::World world;
    makeReady(world, triangle.models());
    const BrushHulls hulls = floorHulls();
    cs::spawnHostages(world, hulls, spawnsApart(1));
    const nx::ecs::Entity hostage = hostagesOf(world).front();

    // 15 times 3 then 2.5 is more than its health, which is all it takes.
    const nx::game::Harm harm = nx::game::attackEntity(world, hostage, attackOf(15.0F, cs::HitGroup::Head));
    EXPECT_TRUE(harm.isKilled);
    EXPECT_FLOAT_EQ(harm.damage.value(), cs::kHostageHealth.value());
    const cs::Hostage& dead = *world.get<cs::Hostage>(hostage);
    EXPECT_TRUE(dead.isDead);
    EXPECT_EQ(dead.health, nx::HitPoints{});
    EXPECT_EQ(dead.activity, cs::Activity::DieHeadshot);
    EXPECT_FALSE(world.has<nx::game::BoxSolidity>(hostage)) << "SOLID_NOT";
    EXPECT_FALSE(world.has<nx::game::NextThink>(hostage));
    EXPECT_TRUE(cs::solidBoxesOf(world).empty());
    const nx::game::Harm again = nx::game::attackEntity(world, hostage, attackOf(15.0F, cs::HitGroup::Head));
    EXPECT_EQ(again.damage.value(), 0.0F) << "a dead hostage takes no damage";
    EXPECT_FALSE(again.isKilled);
}

TEST(HostageDamageTest, ARoundPutsADeadHostageBackWholeButCountsItNot) {
    const PlayedTriangle triangle{true, cs::Activity::DieHeadshot};
    nx::ecs::World world;
    makeReady(world, triangle.models());
    const BrushHulls hulls = floorHulls();
    cs::spawnHostages(world, hulls, spawnsApart(2));
    const std::vector<nx::ecs::Entity> hostages = hostagesOf(world);
    std::ignore = nx::game::attackEntity(world, hostages[0], attackOf(100.0F, cs::HitGroup::Head));

    const std::uint32_t held = cs::repositionHostages(
        world, hulls, cs::HostageBonus{.perHostage = nx::Money{150}, .upTo = nx::Money{2000}});
    EXPECT_EQ(held, 1U) << "the dead one is not held";
    const cs::Hostage& revived = *world.get<cs::Hostage>(hostages[0]);
    EXPECT_FALSE(revived.isDead);
    EXPECT_EQ(revived.health, cs::kHostageHealth);
    EXPECT_EQ(revived.activity, cs::Activity::Idle);
    EXPECT_TRUE(world.has<nx::game::BoxSolidity>(hostages[0]));
    EXPECT_TRUE(world.has<nx::game::NextThink>(hostages[0]));
    EXPECT_EQ(cs::solidBoxesOf(world).size(), 2U);
}

TEST(HostageDamageTest, HurtingAHostageCostsItsAttacker) {
    const cs::HostageHarmCosts costs{.perDamage = nx::Money{20}, .perKill = nx::Money{500}};
    EXPECT_EQ(cs::hostageHarmCost(nx::game::Harm{.damage = nx::Damage{26.25F}}, costs), nx::Money{520})
        << "each whole point";
    EXPECT_EQ(cs::hostageHarmCost(nx::game::Harm{.damage = nx::Damage{47.5F}, .isKilled = true}, costs),
              nx::Money{940 + 500});
}

} // namespace
