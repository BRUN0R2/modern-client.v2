# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# A frame of a map, as the client draws it, into a BMP file.
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
        cs::shared
        cs::client)

# The world, studio, sprite and screen shaders, in bin/shaders/.
add_dependencies(nxview nxShaders)
