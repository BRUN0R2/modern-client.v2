# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(csSharedTests
    SOURCES
        breathTests.cpp
        bulletsTests.cpp
        economyTests.cpp
        footstepsTests.cpp
        hostagesTests.cpp
        inventoryTests.cpp
        movementTests.cpp
        navigationProfileTests.cpp
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
        nx::navigation
        nx::platform)

# Tests read the repository's data/cstrike/ammo.toml, breath.toml, economy.toml, footsteps.toml,
# loadouts.toml, navigation.toml, playerHarm.toml, use.toml and weapons/.
target_compile_definitions(csSharedTests PRIVATE NX_DATA_DIRECTORY="${PROJECT_SOURCE_DIR}/data")
