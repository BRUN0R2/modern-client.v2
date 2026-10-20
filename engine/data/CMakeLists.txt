# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(nxData
    ALIAS nx::data
    LAYER 2
    MODULES
        src/nx.data.cppm
        src/document.cppm
        src/schema.cppm
    SOURCES
        src/document.cpp
        src/schema.cpp
    DEPENDS
        nx::core
        nx::meta)

# The TOML library is an implementation detail of src/document.cpp: never part of the interface.
target_link_libraries(nxData PRIVATE tomlplusplus::tomlplusplus)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
