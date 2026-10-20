# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(nxAssets
    ALIAS nx::assets
    LAYER 3
    MODULES
        src/nx.assets.cppm
        src/binary.cppm
        src/vfs.cppm
        src/content.cppm
        src/steam.cppm
        src/wad.cppm
        src/wav.cppm
        src/bsp.cppm
        src/spr.cppm
        src/mdl.cppm
    SOURCES
        src/binary.cpp
        src/vfs.cpp
        src/content.cpp
        src/steam.cpp
        src/wad.cpp
        src/wav.cpp
        src/bsp.cpp
        src/spr.cpp
        src/mdl.cpp
    DEPENDS
        nx::core
        nx::math
        nx::meta
        nx::data
        nx::platform)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
