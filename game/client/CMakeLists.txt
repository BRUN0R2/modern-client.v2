# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# What only the client runs, over cs.shared (docs/01-architecture.md, section 1.1).
nxAddModuleLibrary(csClient
    ALIAS cs::client
    LAYER 5
    MODULES
        src/cs.client.cppm
        src/input.cppm
    SOURCES
        src/input.cpp
    DEPENDS
        nx::core
        nx::math
        nx::console
        cs::shared
    LATERAL
        cs::shared)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
