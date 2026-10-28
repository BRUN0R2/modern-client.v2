# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# Counter-Strike's bot, over the agents and the server's game (docs/01-architecture.md, section 1.1).
nxAddModuleLibrary(csBots
    ALIAS cs::bots
    LAYER 5
    MODULES
        src/cs.bots.cppm
        src/botRules.cppm
        src/botSight.cppm
        src/bots.cppm
        src/botCommands.cppm
    SOURCES
        src/botRules.cpp
        src/botSight.cpp
        src/bots.cpp
        src/botCommands.cpp
    DEPENDS
        nx::core
        nx::math
        nx::meta
        nx::data
        nx::console
        nx::jobs
        nx::game
        nx::navigation
        nx::agents
        cs::shared
        cs::server
    LATERAL
        cs::shared
        cs::server)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
