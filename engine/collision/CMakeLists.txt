# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# The hulls come from the map as its compiler wrote them: the same-layer dependency on nx.assets the
# catalog declares (docs/01-architecture.md, section 1.1).
nxAddModuleLibrary(nxCollision
    ALIAS nx::collision
    LAYER 3
    MODULES
        src/nx.collision.cppm
        src/hulls.cppm
    SOURCES
        src/hulls.cpp
    DEPENDS
        nx::core
        nx::math
        nx::assets
    LATERAL
        nx::assets)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
