# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(nxAssets
    ALIAS nx::assets
    LAYER 3
    MODULES
        src/nx.assets.cppm
        src/vfs.cppm
        src/content.cppm
        src/steam.cppm
    SOURCES
        src/vfs.cpp
        src/content.cpp
        src/steam.cpp
    DEPENDS
        nx::core
        nx::meta
        nx::data
        nx::platform)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
