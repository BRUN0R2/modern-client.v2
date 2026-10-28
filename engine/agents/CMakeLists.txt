# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(nxAgents
    ALIAS nx::agents
    LAYER 4
    MODULES src/nx.agents.cppm
    SOURCES src/brain.cpp src/decisions.cpp
    DEPENDS nx::core nx::math nx::ecs nx::jobs nx::navigation)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
