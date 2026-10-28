# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(modZombies
    ALIAS mod::zombies
    LAYER 5
    MODULES src/mod.zombies.cppm
    SOURCES src/horde.cpp
    DEPENDS nx::core nx::math nx::meta nx::data nx::jobs nx::ecs nx::assets nx::game
            nx::navigation nx::agents cs::shared cs::server
    LATERAL cs::shared cs::server)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
