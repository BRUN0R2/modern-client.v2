# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# The interface's toolkit, in VGUI2's model (ADR-0020).
nxAddModuleLibrary(nxUi
    ALIAS nx::ui
    LAYER 3
    MODULES
        src/nx.ui.cppm
        src/panel.cppm
    SOURCES
        src/panel.cpp
    DEPENDS
        nx::core
        nx::assets
    LATERAL
        nx::assets)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
