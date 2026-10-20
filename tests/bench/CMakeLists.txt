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
