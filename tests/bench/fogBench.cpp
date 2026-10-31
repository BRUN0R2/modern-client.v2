// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The client's per-frame fog update with unchanged cvars, including a refused configuration.

#include <benchmark/benchmark.h>

import std;
import nx.core;
import nx.console;
import cs.client;

#include "consoleFixtures.hpp"

namespace {

constexpr nx::Seconds kFrameTime{0.01F};

enum class FogScenario : std::uint8_t { Disabled, Enabled, Refused };

void updateFog(benchmark::State& state, FogScenario scenario) {
    nx::test::RecordingOutput output;
    nx::test::MemoryFiles files;
    nx::console::Console console{output, files};
    auto registered = cs::FogCvars::registerIn(console);
    nx::invariant(registered.has_value(), "fogBench: the cvars register");
    const cs::FogCvars cvars = *registered;
    if (scenario != FogScenario::Disabled) {
        const auto enabled = cvars.enabled.set(true);
        nx::invariant(enabled.has_value(), "fogBench: the fog can be enabled");
    }
    if (scenario == FogScenario::Refused) {
        const auto curve = cvars.curve.set("linear");
        nx::invariant(curve.has_value(), "fogBench: linear is a fog curve");
    }
    cs::FogControl control;
    control.update(cvars, kFrameTime);
    for ([[maybe_unused]] const auto iteration : state) {
        control.update(cvars, kFrameTime);
        benchmark::DoNotOptimize(control);
    }
}
BENCHMARK_CAPTURE(updateFog, disabled, FogScenario::Disabled);
BENCHMARK_CAPTURE(updateFog, enabled, FogScenario::Enabled);
BENCHMARK_CAPTURE(updateFog, refused, FogScenario::Refused);

} // namespace
