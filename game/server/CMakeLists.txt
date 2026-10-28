# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# What only the server runs, over cs.shared (docs/01-architecture.md, section 1.1): the game itself.
nxAddModuleLibrary(csServer
    ALIAS cs::server
    LAYER 5
    MODULES
        src/cs.server.cppm
        src/gameCvars.cppm
        src/gameModels.cppm
        src/playerBodies.cppm
        src/playerNames.cppm
        src/game.cppm
        src/entityBodies.cppm
        src/agentNavigation.cppm
    SOURCES
        src/gameCvars.cpp
        src/gameModels.cpp
        src/playerBodies.cpp
        src/playerNames.cpp
        src/game.cpp
        src/entityBodies.cpp
        src/agentNavigation.cpp
    DEPENDS
        nx::core
        nx::math
        nx::console
        nx::assets
        nx::ecs
        nx::collision
        nx::navigation
        nx::game
        cs::shared
    LATERAL
        cs::shared)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
