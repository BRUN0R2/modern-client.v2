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

#include <benchmark/benchmark.h>

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.console;
import nx.assets;
import nx.jobs;
import nx.game;
import nx.navigation;
import cs.shared;
import cs.server;
import cs.bots;

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

/// A match of the map `name`: a player that stands where it spawns, and `bots` bots.
class BotMatch {
public:
    BotMatch(const std::string_view name, std::uint32_t bots) {
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
    }

    /// One tick of the game, its bots' and its player's commands, each part's time kept.
    void tick() {
        const Clock::time_point start = Clock::now();
        std::vector<cs::PlayerCommand> commands = botPlayers->commands();
        const Clock::time_point decided = Clock::now();
        commandNumber = cs::CommandNumber{commandNumber.value + 1};
        commands.push_back(
            cs::PlayerCommand{.player = player,
                              .command = cs::UserCommand{.number = commandNumber,
                                                         .duration = nx::tickLengthOf(game->time().rate),
                                                         .viewAngles = game->ownStateOf(player).view}});
        game->tick(commands);
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
        auto profile = cs::standingPlayerProfile(*navigation);
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
    std::unique_ptr<cs::Bots> botPlayers; ///< Before the game it plays in and its mesh go.
    cs::PlayerIndex player{};
    cs::CommandNumber commandNumber{};
    std::vector<double> decisions;
    std::vector<double> ticks;
    std::vector<double> snapshots;
    std::vector<double> wholes;
    std::uint64_t placesHash = kHashStart;
};

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

} // namespace

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
