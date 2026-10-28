# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(csBotsTests
    SOURCES
        botRulesTests.cpp
        botSightTests.cpp
        botAimTests.cpp
        botTriggerTests.cpp
        botsTests.cpp
        botCommandsTests.cpp
    DEPENDS
        cs::bots
        cs::server
        cs::shared
        nx::core
        nx::math
        nx::console
        nx::assets
        nx::jobs
        nx::agents
        nx::ecs
        nx::game
        nx::platform)

# The maps of the tests are the BSP seed of the fuzz corpus, given other entities; the game's data is the
# repository's data/.
target_compile_definitions(csBotsTests PRIVATE NX_FUZZ_CORPUS_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/fuzz/corpus"
                                               NX_DATA_DIRECTORY="${PROJECT_SOURCE_DIR}/data")
