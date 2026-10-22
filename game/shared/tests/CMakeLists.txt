# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(csSharedTests
    SOURCES
        movementTests.cpp
        studioTests.cpp
        weaponsTests.cpp
    DEPENDS
        cs::shared
        nx::platform)

# A test reads the repository's data/cstrike/ammo.toml and data/cstrike/weapons/.
target_compile_definitions(csSharedTests PRIVATE NX_DATA_DIRECTORY="${PROJECT_SOURCE_DIR}/data")
