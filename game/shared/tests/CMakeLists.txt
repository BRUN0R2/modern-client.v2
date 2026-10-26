# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(csSharedTests
    SOURCES
        breathTests.cpp
        bulletsTests.cpp
        economyTests.cpp
        footstepsTests.cpp
        gameMapTests.cpp
        hostagesTests.cpp
        inventoryTests.cpp
        lightStylesTests.cpp
        movementTests.cpp
        playerHarmTests.cpp
        roundsTests.cpp
        studioTests.cpp
        useTests.cpp
        weaponsTests.cpp
        weaponTypesTests.cpp
    DEPENDS
        cs::shared
        nx::data
        nx::game
        nx::platform)

# Tests read the repository's data/cstrike/ammo.toml, breath.toml, economy.toml, footsteps.toml,
# lightStyles.toml, loadouts.toml, playerHarm.toml, use.toml and weapons/; their maps are the BSP seed of the fuzz corpus,
# given other entities.
target_compile_definitions(csSharedTests PRIVATE NX_DATA_DIRECTORY="${PROJECT_SOURCE_DIR}/data"
                                                 NX_FUZZ_CORPUS_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/fuzz/corpus")
