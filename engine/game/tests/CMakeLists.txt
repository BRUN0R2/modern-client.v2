# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(nxGameTests
    SOURCES
        brushEntitiesTests.cpp
        buttonTests.cpp
        damageTests.cpp
        decalsTests.cpp
        doorTests.cpp
        entityKeysTests.cpp
        navigationTests.cpp
        placementTests.cpp
        solidsTests.cpp
        thinkingTests.cpp
        triggerTests.cpp
    DEPENDS
        nx::game
        nx::assets
        nx::navigation
        nx::platform)

# Tests read the repository's data/cstrike/entities.toml and navigation.toml, and the BSP seeds of the
# fuzz corpus.
target_compile_definitions(nxGameTests PRIVATE NX_DATA_DIRECTORY="${PROJECT_SOURCE_DIR}/data"
                                               NX_FUZZ_CORPUS_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/fuzz/corpus")
