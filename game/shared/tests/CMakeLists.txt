# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(csSharedTests
    SOURCES
        inventoryTests.cpp
        movementTests.cpp
        studioTests.cpp
        weaponsTests.cpp
    DEPENDS
        cs::shared
        nx::game
        nx::platform)

# Tests read the repository's data/cstrike/ammo.toml, loadouts.toml and weapons/.
target_compile_definitions(csSharedTests PRIVATE NX_DATA_DIRECTORY="${PROJECT_SOURCE_DIR}/data")
