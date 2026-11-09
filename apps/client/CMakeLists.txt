# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# The client, next (docs/01-architecture.md, section 3).
nxAddModuleLibrary(appClient
    ALIAS app::client
    LAYER 6
    MODULES
        src/app.client.cppm
        src/commandLine.cppm
        src/client.cppm
        src/clientSettings.cppm
        src/clientBuyMenu.cppm
        src/clientBots.cppm
        src/navigationView.cppm
        src/video.cppm
        src/agentMeshes.cppm
        src/session.cppm
        src/zombieCommands.cppm
        src/sessionFrame.cppm
    SOURCES
        src/commandLine.cpp
        src/video.cpp
        src/agentMeshes.cpp
        src/session.cpp
        src/zombieCommands.cpp
        src/sessionFrame.cpp
        src/clientSettings.cpp
        src/clientBuyMenu.cpp
        src/clientBots.cpp
        src/navigationView.cpp
        src/client.cpp
    DEPENDS
        nx::core
        nx::math
        nx::jobs
        nx::game
        nx::navigation
        nx::ecs
        nx::agents
        nx::platform
        nx::console
        nx::settings
        nx::assets
        nx::rhi
        nx::rhiVulkan
        nx::render
        nx::ui
        cs::shared
        cs::server
        cs::client
        cs::bots
        mod::zombies)

nxAddProgram(next
    SOURCES
        src/main.cpp
    DEPENDS
        app::client
        nx::core)

# The world, studio, sprite and screen shaders, in bin/shaders/.
add_dependencies(next nxShaders)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
