# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(nxConsole
    ALIAS nx::console
    MODULES
        src/nx.console.cppm
        src/text.cppm
        src/parser.cppm
        src/cvar.cppm
        src/button.cppm
        src/console.cppm
    SOURCES
        src/text.cpp
        src/parser.cpp
        src/cvar.cpp
        src/console.cpp
    DEPENDS
        nx::core)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
