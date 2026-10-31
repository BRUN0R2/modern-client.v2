# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# Benchmarks of hot paths (docs/18-testing-and-quality.md, "Performance"). They measure only in
# release builds: build/release-<platform>/bin/nxBench<Module>.
nxAddBenchmark(nxBenchJobs
    SOURCES
        jobsBench.cpp
    DEPENDS
        nx::jobs)

nxAddBenchmark(nxBenchEcs
    SOURCES
        ecsBench.cpp
    DEPENDS
        nx::ecs)

nxAddBenchmark(nxBenchLightStyles
    SOURCES
        lightStylesBench.cpp
    DEPENDS
        nx::core
        nx::assets
        nx::render
        cs::shared)

nxAddBenchmark(nxBenchClient
    SOURCES
        clientBench.cpp
        fogBench.cpp
    DEPENDS
        nx::core
        nx::console
        nx::math
        nx::platform
        nx::assets
        nx::render
        nx::testSupport
        cs::shared
        cs::client)

target_compile_definitions(nxBenchClient PRIVATE NX_FUZZ_CORPUS_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/fuzz/corpus")

nxAddBenchmark(nxBenchNavigation
    SOURCES navigationBench.cpp
    DEPENDS nx::navigation nx::agents nx::ecs)
