# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(csServerTests
    SOURCES
        gameTests.cpp
        entityBodiesTests.cpp
        playerNamesTests.cpp
    DEPENDS
        cs::server
        cs::shared
        nx::console
        nx::assets
        nx::ecs
        nx::game
        nx::collision
        nx::platform)

# The maps of the tests are the BSP seed of the fuzz corpus, given other entities; the game's data is the
# repository's data/.
target_compile_definitions(csServerTests PRIVATE NX_FUZZ_CORPUS_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/fuzz/corpus"
                                                 NX_DATA_DIRECTORY="${PROJECT_SOURCE_DIR}/data")
