# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(nxGame
    ALIAS nx::game
    LAYER 4
    MODULES
        src/nx.game.cppm
        src/clock.cppm
        src/entityKeys.cppm
        src/components.cppm
        src/links.cppm
        src/thinking.cppm
        src/targets.cppm
        src/triggers.cppm
        src/toggles.cppm
        src/doors.cppm
        src/buttons.cppm
        src/use.cppm
        src/solids.cppm
        src/brushEntities.cppm
        src/movers.cppm
        src/placement.cppm
        src/decals.cppm
        src/damage.cppm
    SOURCES
        src/clock.cpp
        src/entityKeys.cpp
        src/components.cpp
        src/links.cpp
        src/thinking.cpp
        src/targets.cpp
        src/triggers.cpp
        src/toggles.cpp
        src/doors.cpp
        src/buttons.cpp
        src/use.cpp
        src/solids.cpp
        src/brushEntities.cpp
        src/movers.cpp
        src/placement.cpp
        src/decals.cpp
        src/damage.cpp
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
