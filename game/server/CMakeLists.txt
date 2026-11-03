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
        src/gameState.cppm
        src/playerPose.cppm
        src/playerSpawns.cppm
        src/purchases.cppm
        src/playerSnapshots.cppm
        src/gameRules.cppm
        src/playerHarms.cppm
        src/playerCommands.cppm
        src/game.cppm
        src/entityBodies.cppm
        src/agentNavigation.cppm
        src/agentHearing.cppm
        src/agentTouch.cppm
    SOURCES
        src/gameCvars.cpp
        src/gameModels.cpp
        src/playerBodies.cpp
        src/playerNames.cpp
        src/gameState.cpp
        src/playerPose.cpp
        src/playerSpawns.cpp
        src/purchases.cpp
        src/playerSnapshots.cpp
        src/gameRules.cpp
        src/playerHarms.cpp
        src/playerCommands.cpp
        src/game.cpp
        src/entityBodies.cpp
        src/agentNavigation.cpp
        src/agentHearing.cpp
        src/agentTouch.cpp
    DEPENDS
        nx::core
        nx::meta
        nx::math
        nx::console
        nx::jobs
        nx::assets
        nx::ecs
        nx::collision
        nx::navigation
        nx::agents
        nx::game
        cs::shared
    LATERAL
        cs::shared)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
