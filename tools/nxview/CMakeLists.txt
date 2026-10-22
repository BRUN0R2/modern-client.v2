# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(toolNxview
    ALIAS tool::nxview
    LAYER 6
    MODULES
        src/tool.nxview.cppm
        src/entityKeys.cppm
        src/modelFiles.cppm
        src/mapLight.cppm
        src/mapSprites.cppm
        src/mapModels.cppm
        src/viewModel.cppm
    SOURCES
        src/entityKeys.cpp
        src/modelFiles.cpp
        src/mapLight.cpp
        src/mapSprites.cpp
        src/mapModels.cpp
        src/viewModel.cpp
    DEPENDS
        nx::core
        nx::math
        nx::assets
        nx::rhi
        nx::render
        nx::ecs
        nx::collision
        nx::game
        cs::shared)

nxAddProgram(nxview
    SOURCES
        src/main.cpp
    DEPENDS
        tool::nxview
        nx::core
        nx::math
        nx::platform
        nx::console
        nx::settings
        nx::assets
        nx::rhi
        nx::rhiVulkan
        nx::render
        nx::ecs
        nx::collision
        nx::game
        cs::shared
        cs::client)

# The world, studio and sprite shaders, in bin/shaders/.
add_dependencies(nxview nxShaders)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
