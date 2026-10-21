# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(nxGame
    ALIAS nx::game
    LAYER 4
    MODULES
        src/nx.game.cppm
        src/components.cppm
        src/solids.cppm
        src/brushEntities.cppm
        src/movers.cppm
        src/placement.cppm
    SOURCES
        src/components.cpp
        src/solids.cpp
        src/brushEntities.cpp
        src/movers.cpp
        src/placement.cpp
    DEPENDS
        nx::core
        nx::math
        nx::meta
        nx::data
        nx::ecs
        nx::assets
        nx::collision)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
