# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# What only the client runs, over cs.shared (docs/01-architecture.md, section 1.1).
nxAddModuleLibrary(csClient
    ALIAS cs::client
    LAYER 5
    MODULES
        src/cs.client.cppm
        src/input.cppm
        src/entityKeys.cppm
        src/mapLight.cppm
        src/mapSprites.cppm
        src/posedModels.cppm
        src/mapModels.cppm
        src/playerModels.cppm
        src/entityModels.cppm
        src/modelMaterials.cppm
        src/viewModel.cppm
        src/muzzleFlash.cppm
        src/levelDecals.cppm
        src/bulletMarks.cppm
        src/level.cppm
        src/scene.cppm
        src/frame.cppm
        src/clientGame.cppm
        src/hud.cppm
        src/windowFonts.cppm
        src/console.cppm
        src/fpsLabel.cppm
        src/audio.cppm
        src/weaponCommands.cppm
    SOURCES
        src/input.cpp
        src/entityKeys.cpp
        src/mapLight.cpp
        src/mapSprites.cpp
        src/mapModels.cpp
        src/playerModels.cpp
        src/entityModels.cpp
        src/modelMaterials.cpp
        src/viewModel.cpp
        src/muzzleFlash.cpp
        src/levelDecals.cpp
        src/bulletMarks.cpp
        src/level.cpp
        src/scene.cpp
        src/frame.cpp
        src/clientGame.cpp
        src/hud.cpp
        src/windowFonts.cpp
        src/console.cpp
        src/fpsLabel.cpp
        src/audio.cpp
        src/weaponCommands.cpp
    DEPENDS
        nx::core
        nx::math
        nx::meta
        nx::platform
        nx::console
        nx::data
        nx::assets
        nx::rhi
        nx::render
        nx::ui
        nx::audio
        nx::ecs
        nx::collision
        nx::game
        cs::shared
    LATERAL
        cs::shared)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
