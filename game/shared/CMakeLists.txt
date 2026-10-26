# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# One library, linked by the server and the client alike, so that prediction runs the very code the
# server does (docs/07-simulation-and-physics.md, section 6).
nxAddModuleLibrary(csShared
    ALIAS cs::shared
    LAYER 5
    MODULES
        src/cs.shared.cppm
        src/movement.cppm
        src/studio.cppm
        src/player.cppm
        src/sounds.cppm
        src/weapons.cppm
        src/loadouts.cppm
        src/inventory.cppm
        src/bullets.cppm
        src/rounds.cppm
        src/economy.cppm
        src/materials.cppm
        src/footsteps.cppm
        src/hitGroups.cppm
        src/hostages.cppm
    SOURCES
        src/movement.cpp
        src/studio.cpp
        src/sounds.cpp
        src/weapons.cpp
        src/loadouts.cpp
        src/inventory.cpp
        src/bullets.cpp
        src/rounds.cpp
        src/economy.cpp
        src/materials.cpp
        src/footsteps.cpp
        src/hitGroups.cpp
        src/hostages.cpp
    DEPENDS
        nx::core
        nx::math
        nx::meta
        nx::data
        nx::ecs
        nx::assets
        nx::collision
        nx::game)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
