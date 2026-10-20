# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(nxMeta
    ALIAS nx::meta
    LAYER 1
    MODULES
        src/nx.meta.cppm
        src/description.cppm
        src/native.cppm
        src/reflection.cppm
    DEPENDS
        nx::core)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
