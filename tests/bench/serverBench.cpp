// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The server's tick in a match of bots on a real map of the installation Steam installed (none skips it),
// with no window and no GPU: what the bots decide, what the game makes of it and what it tells its player,
// each tick as long as it takes. Run a whole match and read its worst ticks, since a tick has 10 ms:
//
//     nxBenchServer --benchmark_filter=cs_assault/32 --benchmark_min_time=6000x
//
// Its label is a hash of where the players went: the same on any build and any number of workers while what
// the bots do does not change.
//
// A crowd on a floor as wide as it takes, far past any real match, to find what does not scale:
//
//     nxBenchServer --benchmark_filter=crowdTick/100000 --benchmark_min_time=200x

#include <benchmark/benchmark.h>
#include <cstring>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.console;
import nx.assets;
import nx.ecs;
import nx.jobs;
import nx.game;
import nx.navigation;
import nx.agents;
import cs.shared;
import cs.server;
import cs.bots;
import mod.zombies;

#include "consoleFixtures.hpp"

namespace {

/// How many ticks apart the players' places go into the label's hash.
constexpr std::uint32_t kHashedEvery = 50;
constexpr double kMillisecondsPerSecond = 1000.0;
constexpr std::uint64_t kHashStart = 14695981039346656037ULL;
constexpr std::uint64_t kHashPrime = 1099511628211ULL;
/// Bots are added at the skill of bot_difficulty's default.
constexpr cs::BotDifficulty kBotDifficulty = cs::BotDifficulty::Normal;
constexpr std::string_view kNoPrefix;

using Clock = std::chrono::steady_clock;

[[nodiscard]] auto secondsBetween(Clock::time_point from, Clock::time_point to) -> double {
    return std::chrono::duration<double>(to - from).count();
}

/// The time at `share` of `times`, which it sorts, in milliseconds.
[[nodiscard]] auto millisecondsAt(std::vector<double>& times, double share) -> double {
    if (times.empty()) {
        return 0.0;
    }
    std::ranges::sort(times);
    const auto index =
        std::min(static_cast<std::size_t>(share * static_cast<double>(times.size())), times.size() - 1);
    return times[index] * kMillisecondsPerSecond;
}

/// A model of the installation the settings of data/mods/zombies.toml name sequences of.
constexpr std::string_view kZombieModel = "models/player/leet/leet.mdl";
/// As many as `zombie_add` adds at once; how far from the terrorists' spawn points and how far apart they
/// are placed: all over the map.
constexpr std::uint32_t kZombieBatch = 64;
constexpr nx::HammerUnits kZombieReach{65536.0F};
constexpr nx::HammerUnits kZombieSpacing{1.0F};
/// The bots that play among a horde.
constexpr std::uint32_t kBotsAmongTheHorde = 8;

/// A match of the map `name`: a player that stands where it spawns, `bots` bots, and `zombies` zombies.
class BotMatch {
public:
    BotMatch(const std::string_view name, std::uint32_t bots, std::uint32_t zombies = 0) {
        auto executable = nx::platform::executablePath();
        if (!executable) {
            failure = nx::describe(executable.error());
            return;
        }
        const std::filesystem::path data = executable->parent_path() / "data";
        auto opened = cs::openGameContent(data, std::nullopt, std::nullopt);
        if (!opened) {
            failure = nx::describe(opened.error());
            return;
        }
        content.emplace(std::move(*opened));
        auto loaded = cs::GameMap::load(content->vfs, name);
        if (!loaded) {
            failure = nx::describe(loaded.error());
            return;
        }
        map = std::move(*loaded);
        auto registered = cs::GameCvars::registerIn(console);
        if (!registered) {
            failure = nx::describe(registered.error());
            return;
        }
        cvars.emplace(std::move(*registered));
        auto started = cs::Game::start(*content, *map, *cvars);
        if (!started) {
            failure = nx::describe(started.error());
            return;
        }
        game = std::move(*started);
        player = game->join(cs::spawnTeam(map->map()), cs::AutoSwitch::ToHeavier, "Player");
        auto workers = nx::jobs::JobSystem::create();
        if (!workers) {
            failure = "no Job System";
            return;
        }
        jobs = std::move(*workers);
        failure = makeBots(data);
        if (!failure.empty()) {
            return;
        }
        for (std::uint32_t bot = 0; bot < bots; ++bot) {
            std::ignore = botPlayers->add(std::nullopt, kBotDifficulty, kNoPrefix);
        }
        if (zombies > 0) {
            failure = makeHorde(data, zombies);
        }
    }

    /// One tick of the game, its bots' and its player's commands, each part's time kept.
    void tick() {
        const Clock::time_point start = Clock::now();
        std::vector<cs::PlayerCommand> commands = botPlayers->commands();
        const std::span<const cs::EntityCommand> entityCommands =
            horde ? horde->commands() : std::span<const cs::EntityCommand>{};
        const Clock::time_point decided = Clock::now();
        commandNumber = cs::CommandNumber{commandNumber.value + 1};
        commands.push_back(
            cs::PlayerCommand{.player = player,
                              .command = cs::UserCommand{.number = commandNumber,
                                                         .duration = nx::tickLengthOf(game->time().rate),
                                                         .viewAngles = game->ownStateOf(player).view}});
        game->tick(commands, entityCommands, jobs.get());
        const Clock::time_point ticked = Clock::now();
        cs::PlayerSnapshot snapshot = game->snapshotFor(player);
        benchmark::DoNotOptimize(snapshot);
        const Clock::time_point told = Clock::now();
        decisions.push_back(secondsBetween(start, decided));
        ticks.push_back(secondsBetween(decided, ticked));
        snapshots.push_back(secondsBetween(ticked, told));
        wholes.push_back(secondsBetween(start, told));
        if (ticks.size() % kHashedEvery == 0) {
            hashPlaces(commands);
        }
    }

    [[nodiscard]] auto lastWhole() const -> double { return wholes.back(); }

    /// Reports each part's median, 99th percentile and worst tick on `state`.
    void report(benchmark::State& state) {
        const auto add = [&state](const std::string& name, std::vector<double>& times) {
            state.counters[name + "Median"] = millisecondsAt(times, 0.5);
            state.counters[name + "P99"] = millisecondsAt(times, 0.99);
            state.counters[name + "Worst"] = millisecondsAt(times, 1.0);
        };
        add("bots", decisions);
        add("tick", ticks);
        add("snapshot", snapshots);
        add("whole", wholes);
        const auto overATick = std::ranges::count_if(wholes, [this](double whole) {
            return whole > static_cast<double>(nx::tickLengthOf(game->time().rate).count());
        });
        state.counters["ticksOverTheirTime"] = static_cast<double>(overATick);
        state.SetLabel(std::format("state {:016x}", placesHash));
    }

    std::string failure;

private:
    /// Makes the bots on the map's mesh, by the rules of `data`; what failed, or nothing.
    [[nodiscard]] auto makeBots(const std::filesystem::path& data) -> std::string {
        auto navigation = nx::game::readNavigationData(
            nx::platform::readTextFile(data / "cstrike" / "navigation.toml").value_or(std::string{}),
            "navigation.toml");
        if (!navigation) {
            return nx::describe(navigation.error());
        }
        auto rules = cs::readBotRules(
            nx::platform::readTextFile(data / "cstrike" / "bots.toml").value_or(std::string{}), "bots.toml");
        if (!rules) {
            return nx::describe(rules.error());
        }
        auto profile = cs::agentProfile(*navigation);
        if (!profile) {
            return nx::describe(profile.error());
        }
        auto built = nx::navigation::NavigationMesh::build(*jobs, game->navigationGeometry(), *profile,
                                                           navigation->build);
        if (!built) {
            return nx::describe(built.error());
        }
        mesh.emplace(std::move(*built));
        auto made = cs::Bots::create(*game, *mesh, *jobs, std::move(*rules));
        if (!made) {
            return nx::describe(made.error());
        }
        botPlayers = std::move(*made);
        return {};
    }

    /// Makes a horde of `zombies` all over the map, by the settings of `data`; what failed, or nothing.
    [[nodiscard]] auto makeHorde(const std::filesystem::path& data, std::uint32_t zombies) -> std::string {
        auto settings = zombies::readSettings(
            nx::platform::readTextFile(data / "mods" / "zombies.toml").value_or(std::string{}),
            "zombies.toml");
        if (!settings) {
            return nx::describe(settings.error());
        }
        settings->body.model = std::string{kZombieModel};
        settings->spawnBatchLimit = kZombieBatch;
        settings->spawnRadius = kZombieReach;
        settings->spawnSpacing = kZombieSpacing;
        auto type = content->entities.add(content->vfs, settings->body, cs::ModelTextures::Read);
        if (!type) {
            return nx::describe(type.error());
        }
        auto made = zombies::Horde::create(*game, *mesh, *jobs, *type, *settings);
        if (!made) {
            return nx::describe(made.error());
        }
        horde = std::move(*made);
        while (horde->size() < zombies) {
            const auto batch = std::min<std::size_t>(kZombieBatch, zombies - horde->size());
            if (auto spawned =
                    horde->spawn(map->spawnPoints(cs::Team::Terrorists), nx::narrow<std::uint32_t>(batch));
                !spawned) {
                return nx::describe(spawned.error());
            }
        }
        return {};
    }

    void hashPlaces(std::span<const cs::PlayerCommand> commands) {
        const auto add = [this](float value) {
            placesHash = (placesHash ^ std::bit_cast<std::uint32_t>(value)) * kHashPrime;
        };
        for (const cs::PlayerCommand& command : commands) {
            const cs::PlayerOwnState self = game->ownStateOf(command.player);
            add(self.feet.x);
            add(self.feet.y);
            add(self.feet.z);
            add(self.view.yaw.value());
        }
    }

    nx::log::Logger logger; ///< Of no sinks: what the game logs is no part of its tick here.
    nx::log::DefaultLoggerScope loggerScope{logger};
    nx::test::RecordingOutput output;
    nx::test::MemoryFiles files;
    nx::console::Console console{output, files};
    std::optional<cs::GameContent> content;
    std::optional<cs::GameCvars> cvars;
    std::unique_ptr<cs::GameMap> map;
    std::unique_ptr<cs::Game> game;
    std::unique_ptr<nx::jobs::JobSystem> jobs;
    std::optional<nx::navigation::NavigationMesh> mesh;
    std::unique_ptr<cs::Bots> botPlayers;  ///< Before the game it plays in and its mesh go.
    std::unique_ptr<zombies::Horde> horde; ///< As the bots.
    cs::PlayerIndex player{};
    cs::CommandNumber commandNumber{};
    std::vector<double> decisions;
    std::vector<double> ticks;
    std::vector<double> snapshots;
    std::vector<double> wholes;
    std::uint64_t placesHash = kHashStart;
};

/// How far apart a crowd's bodies stand, wider than their boxes, and how wide its floor is.
constexpr float kCrowdSpacing = 40.0F;
constexpr float kCrowdFloor = 16384.0F;
/// How high above the floor of the corpus's box map a body's origin is placed; it drops to the floor.
constexpr float kCrowdHeight = 100.0F;
/// The side of the corpus's box map, and where a BSP's vertices and entities lumps are in its header.
constexpr float kCorpusBoxSide = 64.0F;
constexpr std::size_t kEntitiesLumpAt = sizeof(std::int32_t);
constexpr std::size_t kVerticesLumpAt = sizeof(std::int32_t) + (3 * 2 * sizeof(std::int32_t));
/// How far a body of a crowd without brains turns each tick, each its own way, so that it walks a circle.
constexpr float kCrowdTurnPerTick = 0.2F;
constexpr float kFullTurnDegrees = 360.0F;
/// Far enough apart that the yaws of a crowd's bodies do not line up.
constexpr float kYawPerBody = 137.5F;

/// The corpus's box map, the floor of its hulls across all of x and y, its faces stretched to kCrowdFloor
/// each way so that a mesh may be built on them, and a player's spawn point in their middle.
[[nodiscard]] auto crowdFloorMap() -> std::vector<std::byte> {
    auto read =
        nx::platform::readFile(std::filesystem::path{NX_FUZZ_CORPUS_DIRECTORY} / "bsp" / "boxEmbedded.bsp");
    std::vector<std::byte> bytes = std::move(read).value_or(std::vector<std::byte>{});
    if (bytes.size() < kVerticesLumpAt + (2 * sizeof(std::int32_t))) {
        return bytes;
    }
    const std::string entities = std::format(R"({{ "classname" "worldspawn" }}
{{ "classname" "info_player_start" "origin" "{0} {0} {1}" }})",
                                             kCrowdFloor / 2.0F, kCrowdHeight);
    const auto entitiesAt = nx::narrow<std::int32_t>(bytes.size());
    std::ranges::copy(std::as_bytes(std::span{entities}), std::back_inserter(bytes));
    bytes.push_back(std::byte{0});
    const std::array entitiesLump{entitiesAt, nx::narrow<std::int32_t>(entities.size() + 1)};
    std::memcpy(bytes.data() + kEntitiesLumpAt, entitiesLump.data(), sizeof(entitiesLump));
    std::array<std::int32_t, 2> vertices{};
    std::memcpy(vertices.data(), bytes.data() + kVerticesLumpAt, sizeof(vertices));
    using Vertex = std::array<float, 3>;
    const auto first = static_cast<std::size_t>(vertices[0]);
    for (std::size_t at = first; at + sizeof(Vertex) <= first + static_cast<std::size_t>(vertices[1]);
         at += sizeof(Vertex)) {
        Vertex point{};
        std::memcpy(point.data(), bytes.data() + at, sizeof(point));
        point[0] *= kCrowdFloor / kCorpusBoxSide;
        point[1] *= kCrowdFloor / kCorpusBoxSide;
        std::memcpy(bytes.data() + at, point.data(), sizeof(point));
    }
    return bytes;
}

/// What a crowd's bodies do: walk a circle each, or go across the floor by brains, each to the far side of
/// its middle, on a mesh, steering around each other.
enum class CrowdKind : std::uint8_t {
    Walking,
    Thinking,
};

/// `bodies` bodies of a player's model on a floor, in rows from its middle, and a player that stands among
/// them.
class Crowd {
public:
    Crowd(std::uint32_t bodies, CrowdKind crowdKind) : kind{crowdKind} {
        auto executable = nx::platform::executablePath();
        if (!executable) {
            failure = nx::describe(executable.error());
            return;
        }
        const std::filesystem::path data = executable->parent_path() / "data";
        auto opened = cs::openGameContent(data, std::nullopt, std::nullopt);
        if (!opened) {
            failure = nx::describe(opened.error());
            return;
        }
        content.emplace(std::move(*opened));
        auto settings = zombies::readSettings(
            nx::platform::readTextFile(data / "mods" / "zombies.toml").value_or(std::string{}),
            "zombies.toml");
        if (!settings) {
            failure = nx::describe(settings.error());
            return;
        }
        settings->body.model = std::string{kZombieModel};
        auto type = content->entities.add(content->vfs, settings->body, cs::ModelTextures::Read);
        if (!type) {
            failure = nx::describe(type.error());
            return;
        }
        auto loaded = cs::GameMap::read(crowdFloorMap(), "crowd");
        if (!loaded) {
            failure = nx::describe(loaded.error());
            return;
        }
        map = std::move(*loaded);
        auto registered = cs::GameCvars::registerIn(console);
        if (!registered) {
            failure = nx::describe(registered.error());
            return;
        }
        cvars.emplace(std::move(*registered));
        auto started = cs::Game::start(*content, *map, *cvars);
        if (!started) {
            failure = nx::describe(started.error());
            return;
        }
        game = std::move(*started);
        player = game->join(cs::spawnTeam(map->map()), cs::AutoSwitch::ToHeavier, "Player");
        auto workers = nx::jobs::JobSystem::create();
        if (!workers) {
            failure = "no Job System";
            return;
        }
        jobs = std::move(*workers);
        const Clock::time_point spawning = Clock::now();
        const auto rows = static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<float>(bodies))));
        const float fromMiddle = (kCrowdFloor - (static_cast<float>(rows) * kCrowdSpacing)) / 2.0F;
        entities.reserve(bodies);
        spawned.reserve(bodies);
        for (std::uint32_t body = 0; body < bodies; ++body) {
            const nx::Vec3 origin{.x = fromMiddle + (static_cast<float>(body % rows) * kCrowdSpacing),
                                  .y = fromMiddle + (static_cast<float>(body / rows) * kCrowdSpacing),
                                  .z = kCrowdHeight};
            auto made = game->createEntity(*type, {.origin = origin});
            if (!made) {
                failure = nx::describe(made.error());
                return;
            }
            entities.push_back(*made);
            spawned.push_back(origin);
        }
        spawnSeconds = secondsBetween(spawning, Clock::now());
        speed = settings->body.speed;
        if (kind == CrowdKind::Thinking) {
            failure = makeHorde(data, std::move(*settings), *type);
        }
    }

    /// One tick of the game, what its bodies ask for and its player's command, each part's time kept.
    void tick() {
        const Clock::time_point start = Clock::now();
        std::vector<cs::EntityCommand> walking;
        if (kind == CrowdKind::Walking) {
            walking = walked();
        }
        const std::span<const cs::EntityCommand> commands =
            kind == CrowdKind::Thinking ? horde->commands() : std::span<const cs::EntityCommand>{walking};
        const Clock::time_point decided = Clock::now();
        commandNumber = cs::CommandNumber{commandNumber.value + 1};
        const std::array playerCommands{
            cs::PlayerCommand{.player = player,
                              .command = cs::UserCommand{.number = commandNumber,
                                                         .duration = nx::tickLengthOf(game->time().rate),
                                                         .viewAngles = game->ownStateOf(player).view}}};
        game->tick(playerCommands, commands, jobs.get());
        const Clock::time_point ticked = Clock::now();
        cs::PlayerSnapshot snapshot = game->snapshotFor(player);
        benchmark::DoNotOptimize(snapshot);
        const Clock::time_point told = Clock::now();
        decisions.push_back(secondsBetween(start, decided));
        ticks.push_back(secondsBetween(decided, ticked));
        snapshots.push_back(secondsBetween(ticked, told));
        wholes.push_back(secondsBetween(start, told));
    }

    [[nodiscard]] auto lastWhole() const -> double { return wholes.back(); }

    /// Reports each part's median, 99th percentile and worst tick on `state`.
    void report(benchmark::State& state) {
        const auto add = [&state](const std::string& name, std::vector<double>& times) {
            state.counters[name + "Median"] = millisecondsAt(times, 0.5);
            state.counters[name + "P99"] = millisecondsAt(times, 0.99);
            state.counters[name + "Worst"] = millisecondsAt(times, 1.0);
        };
        add("asked", decisions);
        add("tick", ticks);
        add("snapshot", snapshots);
        add("whole", wholes);
        state.counters["spawnSeconds"] = spawnSeconds;
    }

    std::string failure;

private:
    /// The bodies made a horde, on the floor's mesh, that sees the player in the middle from anywhere; what
    /// failed, or nothing.
    [[nodiscard]] auto makeHorde(const std::filesystem::path& data, zombies::Settings settings,
                                 cs::EntityTypeIndex type) -> std::string {
        auto navigation = nx::game::readNavigationData(
            nx::platform::readTextFile(data / "cstrike" / "navigation.toml").value_or(std::string{}),
            "navigation.toml");
        if (!navigation) {
            return nx::describe(navigation.error());
        }
        auto profile = cs::agentProfile(*navigation);
        if (!profile) {
            return nx::describe(profile.error());
        }
        auto built = nx::navigation::NavigationMesh::build(*jobs, game->navigationGeometry(), *profile,
                                                           navigation->build);
        if (!built) {
            return nx::describe(built.error());
        }
        mesh.emplace(std::move(*built));
        settings.sightRange = nx::HammerUnits{2.0F * kCrowdFloor};
        settings.fieldOfView = nx::Degrees{kFullTurnDegrees};
        auto made = zombies::Horde::create(*game, *mesh, *jobs, type, std::move(settings));
        if (!made) {
            return nx::describe(made.error());
        }
        horde = std::move(*made);
        horde->adopt(entities);
        return {};
    }

    /// Each body walks a circle of its own, of the game's clock.
    [[nodiscard]] auto walked() const -> std::vector<cs::EntityCommand> {
        std::vector<cs::EntityCommand> commands;
        commands.reserve(entities.size());
        const nx::game::TickTime time = game->time();
        for (std::size_t body = 0; body < entities.size(); ++body) {
            const float yaw = std::fmod((static_cast<float>(body) * kYawPerBody) +
                                            (static_cast<float>(time.now.tick()) * kCrowdTurnPerTick),
                                        kFullTurnDegrees);
            commands.push_back({.entity = entities[body],
                                .command = {.duration = nx::tickLengthOf(time.rate),
                                            .viewAngles = {.yaw = nx::Degrees{yaw}},
                                            .move = {.forward = speed}}});
        }
        return commands;
    }

    nx::log::Logger logger; ///< Of no sinks: what the game logs is no part of its tick here.
    nx::log::DefaultLoggerScope loggerScope{logger};
    nx::test::RecordingOutput output;
    nx::test::MemoryFiles files;
    nx::console::Console console{output, files};
    CrowdKind kind;
    std::optional<cs::GameContent> content;
    std::optional<cs::GameCvars> cvars;
    std::unique_ptr<cs::GameMap> map;
    std::unique_ptr<cs::Game> game;
    std::unique_ptr<nx::jobs::JobSystem> jobs;
    std::optional<nx::navigation::NavigationMesh> mesh;
    std::unique_ptr<zombies::Horde> horde; ///< Of `entities`, on `mesh`, when they think.
    std::vector<nx::ecs::Entity> entities;
    std::vector<nx::Vec3> spawned;
    nx::UnitsPerSecond speed{};
    cs::PlayerIndex player{};
    cs::CommandNumber commandNumber{};
    double spawnSeconds = 0.0;
    std::vector<double> decisions;
    std::vector<double> ticks;
    std::vector<double> snapshots;
    std::vector<double> wholes;
};

void crowdTick(benchmark::State& state, CrowdKind kind) {
    Crowd crowd{nx::narrow<std::uint32_t>(state.range(0)), kind};
    if (!crowd.failure.empty()) {
        state.SkipWithError(crowd.failure);
        return;
    }
    for (auto ignored : state) {
        crowd.tick();
        state.SetIterationTime(crowd.lastWhole());
    }
    crowd.report(state);
}

void botMatchTick(benchmark::State& state, std::string_view map) {
    BotMatch match{map, nx::narrow<std::uint32_t>(state.range(0))};
    if (!match.failure.empty()) {
        state.SkipWithError(match.failure);
        return;
    }
    for (auto ignored : state) {
        match.tick();
        state.SetIterationTime(match.lastWhole());
    }
    match.report(state);
}

void hordeTick(benchmark::State& state, std::string_view map) {
    BotMatch match{map, kBotsAmongTheHorde, nx::narrow<std::uint32_t>(state.range(0))};
    if (!match.failure.empty()) {
        state.SkipWithError(match.failure);
        return;
    }
    for (auto ignored : state) {
        match.tick();
        state.SetIterationTime(match.lastWhole());
    }
    match.report(state);
}

} // namespace

BENCHMARK_CAPTURE(crowdTick, walking, CrowdKind::Walking)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(crowdTick, thinking, CrowdKind::Thinking)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(hordeTick, cs_assault, "cs_assault")
    ->Arg(256)
    ->Arg(1024)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(botMatchTick, cs_assault, "cs_assault")
    ->Arg(32)
    ->Arg(64)
    ->Arg(128)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(botMatchTick, de_dust2, "de_dust2")
    ->Arg(32)
    ->Arg(64)
    ->Arg(128)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);
