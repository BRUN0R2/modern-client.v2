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
    SOURCES
        src/movement.cpp
        src/studio.cpp
    DEPENDS
        nx::core
        nx::math
        nx::ecs
        nx::assets
        nx::collision
        nx::game)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
