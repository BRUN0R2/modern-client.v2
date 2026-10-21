# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(nxGame
    ALIAS nx::game
    LAYER 4
    MODULES
        src/nx.game.cppm
        src/brushEntities.cppm
    SOURCES
        src/brushEntities.cpp
    DEPENDS
        nx::core
        nx::math
        nx::meta
        nx::data
        nx::ecs
        nx::assets)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
