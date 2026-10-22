# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(nxSettings
    ALIAS nx::settings
    LAYER 3
    MODULES
        src/nx.settings.cppm
        src/keys.cppm
        src/binds.cppm
        src/files.cppm
        src/clientCvars.cppm
    SOURCES
        src/keys.cpp
        src/binds.cpp
        src/files.cpp
        src/clientCvars.cpp
    DEPENDS
        nx::core
        nx::console
        nx::platform)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
