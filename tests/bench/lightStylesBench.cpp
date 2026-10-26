// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The light-style payload copied to each client every tick, compared on receipt and evaluated each
// frame (docs/05-rendering.md, section 8), with the world's styles and with every style animated.

#include <benchmark/benchmark.h>

import std;
import nx.core;
import nx.assets;
import nx.render;
import cs.shared;

namespace {

constexpr std::int64_t kWorldStyleCount = 12;
constexpr auto kAllStyleCount = nx::narrow<std::int64_t>(nx::assets::kLightStyleCount);
/// A long world pattern, so the payload includes strings that do not fit in their inline storage.
constexpr std::string_view kAnimatedPattern = "mmmaaaabcdefgmmmmaaaammmaamm";
constexpr nx::Seconds kFrameTime{0.15F};

[[nodiscard]] auto patternsOf(std::int64_t count) -> cs::MapLightStyles {
    cs::MapLightStyles styles;
    const std::size_t active = nx::narrow<std::size_t>(count);
    nx::expects(active <= styles.patterns.size(), "lightStylesBench: a count within the map's styles");
    for (std::size_t style = 0; style < active; ++style) {
        styles.patterns[style] = kAnimatedPattern;
    }
    return styles;
}

void copySnapshotStyles(benchmark::State& state) {
    const cs::MapLightStyles patterns = patternsOf(state.range(0));
    for ([[maybe_unused]] auto iteration : state) {
        cs::MapLightStyles snapshot = patterns;
        benchmark::DoNotOptimize(snapshot);
    }
}
BENCHMARK(copySnapshotStyles)->Arg(kWorldStyleCount)->Arg(kAllStyleCount);

void compareSnapshotStyles(benchmark::State& state) {
    const cs::MapLightStyles previous = patternsOf(state.range(0));
    cs::MapLightStyles incoming = previous;
    for ([[maybe_unused]] auto iteration : state) {
        benchmark::DoNotOptimize(incoming);
        bool unchanged = incoming == previous;
        benchmark::DoNotOptimize(unchanged);
    }
}
BENCHMARK(compareSnapshotStyles)->Arg(kWorldStyleCount)->Arg(kAllStyleCount);

void evaluateFrameStyles(benchmark::State& state) {
    const cs::MapLightStyles patterns = patternsOf(state.range(0));
    nx::render::LightStyles styles;
    styles.setPatterns(patterns.patterns);
    for ([[maybe_unused]] auto iteration : state) {
        nx::render::LightStyleValues values = styles.valuesAt(kFrameTime);
        benchmark::DoNotOptimize(values);
    }
}
BENCHMARK(evaluateFrameStyles)->Arg(kWorldStyleCount)->Arg(kAllStyleCount);

} // namespace
