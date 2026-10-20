# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(nxEcs
    ALIAS nx::ecs
    LAYER 2
    MODULES
        src/nx.ecs.cppm
        src/entity.cppm
        src/component.cppm
        src/archetype.cppm
        src/event.cppm
        src/world.cppm
        src/access.cppm
        src/query.cppm
        src/commands.cppm
        src/system.cppm
        src/schedule.cppm
    SOURCES
        src/entity.cpp
        src/component.cpp
        src/archetype.cpp
        src/world.cpp
        src/commands.cpp
        src/schedule.cpp
    DEPENDS
        nx::core
        nx::jobs)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
