# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# Navigation meshes for every kind of agent (docs/adr/0023-navigation.md).
nxAddModuleLibrary(nxNavigation
    ALIAS nx::navigation
    LAYER 3
    MODULES
        src/nx.navigation.cppm
        src/profile.cppm
        src/geometry.cppm
        src/mesh.cppm
        src/query.cppm
        src/corridor.cppm
    SOURCES
        src/profile.cpp
        src/build.cpp
        src/detour.cpp
    DEPENDS
        nx::core
        nx::math
        nx::jobs)

# Recast builds the meshes and Detour keeps and searches them, its path corridor among them: details of
# src/build.cpp and src/detour.cpp, never part of the interface.
target_link_libraries(nxNavigation PRIVATE RecastNavigation::Recast RecastNavigation::Detour
                                           RecastNavigation::DetourCrowd)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
