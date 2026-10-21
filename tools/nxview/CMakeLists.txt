# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(toolNxview
    ALIAS tool::nxview
    LAYER 6
    MODULES
        src/tool.nxview.cppm
        src/mapLight.cppm
        src/viewModel.cppm
    SOURCES
        src/mapLight.cpp
        src/viewModel.cpp
    DEPENDS
        nx::core
        nx::math
        nx::assets
        nx::rhi
        nx::render
        cs::shared)

nxAddProgram(nxview
    SOURCES
        src/main.cpp
    DEPENDS
        tool::nxview
        nx::core
        nx::math
        nx::platform
        nx::assets
        nx::rhi
        nx::rhiVulkan
        nx::render
        nx::ecs
        nx::collision
        nx::game
        cs::shared)

# The world and studio shaders, in bin/shaders/.
add_dependencies(nxview nxShaders)
