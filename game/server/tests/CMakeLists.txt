# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(csServerTests
    SOURCES
        gameTests.cpp
        entityBodiesTests.cpp
        playerNamesTests.cpp
        agentNavigationTests.cpp
        agentHearingTests.cpp
    DEPENDS
        cs::server
        cs::shared
        nx::agents
        nx::jobs
        nx::math
        nx::console
        nx::assets
        nx::ecs
        nx::game
        nx::navigation
        nx::collision
        nx::platform)

# The maps of the tests are the BSP seed of the fuzz corpus, given other entities; the game's data is the
# repository's data/.
target_compile_definitions(csServerTests PRIVATE NX_FUZZ_CORPUS_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/fuzz/corpus"
                                                 NX_DATA_DIRECTORY="${PROJECT_SOURCE_DIR}/data")
# What the agents hear is placed on meshes of the navigation tests' geometry.
target_include_directories(csServerTests PRIVATE "${PROJECT_SOURCE_DIR}/engine/navigation/tests")
