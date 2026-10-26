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
    SOURCES
        src/commandLine.cpp
        src/client.cpp
    DEPENDS
        nx::core
        nx::math
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
        cs::client)

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
