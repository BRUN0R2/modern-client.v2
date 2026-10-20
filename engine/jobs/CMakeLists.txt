# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(nxJobs
    ALIAS nx::jobs
    LAYER 1
    MODULES
        src/nx.jobs.cppm
        src/workStealingDeque.cppm
        src/concurrentPool.cppm
        src/intrusiveQueue.cppm
        src/job.cppm
        src/jobSystem.cppm
    SOURCES
        src/jobSystem.cpp
    DEPENDS
        nx::core)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
