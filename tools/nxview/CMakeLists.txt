# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddProgram(nxview
    SOURCES
        src/main.cpp
    DEPENDS
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

# The world shader, in bin/shaders/.
add_dependencies(nxview nxShaders)
