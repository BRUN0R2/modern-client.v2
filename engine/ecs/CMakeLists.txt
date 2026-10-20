# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(nxEcs
    ALIAS nx::ecs
    MODULES
        src/nx.ecs.cppm
        src/entity.cppm
        src/component.cppm
        src/archetype.cppm
        src/world.cppm
        src/query.cppm
    SOURCES
        src/entity.cpp
        src/component.cpp
        src/archetype.cpp
        src/world.cpp
    DEPENDS
        nx::core
        nx::jobs)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
