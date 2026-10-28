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
        src/renderMode.cppm
        src/blendedOrder.cppm
        src/surface.cppm
        src/upload.cppm
        src/light.cppm
        src/decals.cppm
        src/world.cppm
        src/studio.cppm
        src/sprite.cppm
        src/particle.cppm
        src/screen.cppm
        src/post.cppm
        src/probes.cppm
    SOURCES
        src/camera.cpp
        src/gamma.cpp
        src/lightStyle.cpp
        src/blendedOrder.cpp
        src/surface.cpp
        src/upload.cpp
        src/light.cpp
        src/decals.cpp
        src/world.cpp
        src/studio.cpp
        src/sprite.cpp
        src/particle.cpp
        src/screen.cpp
        src/post.cpp
        src/probes.cpp
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
