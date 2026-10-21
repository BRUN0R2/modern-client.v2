# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# The renderer draws the assets of its own layer: the same-layer dependency the catalog declares
# (docs/01-architecture.md, section 1.1).
nxAddModuleLibrary(nxRender
    ALIAS nx::render
    LAYER 3
    MODULES
        src/nx.render.cppm
        src/camera.cppm
        src/gamma.cppm
        src/lightStyle.cppm
        src/surface.cppm
        src/light.cppm
        src/world.cppm
    SOURCES
        src/camera.cpp
        src/gamma.cpp
        src/lightStyle.cpp
        src/surface.cpp
        src/light.cpp
        src/world.cpp
    DEPENDS
        nx::core
        nx::math
        nx::rhi
        nx::assets
    LATERAL
        nx::assets)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
