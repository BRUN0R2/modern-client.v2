# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(nxRhi
    ALIAS nx::rhi
    LAYER 2
    MODULES
        src/nx.rhi.cppm
        src/types.cppm
        src/handles.cppm
        src/device.cppm
    DEPENDS
        nx::core)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
