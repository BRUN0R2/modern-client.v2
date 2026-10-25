# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(toolNxcheck
    ALIAS tool::nxcheck
    LAYER 6
    MODULES
        src/tool.nxcheck.cppm
    SOURCES
        src/check.cpp
    DEPENDS
        nx::core
        nx::math
        nx::jobs
        nx::assets
        nx::collision
        cs::shared)

nxAddProgram(nxcheck
    SOURCES
        src/main.cpp
    DEPENDS
        tool::nxcheck
        nx::platform)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
