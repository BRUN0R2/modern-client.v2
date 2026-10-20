# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(nxPlatform
    ALIAS nx::platform
    MODULES
        src/nx.platform.cppm
        src/clock.cppm
    SOURCES
        src/clock.cpp
    DEPENDS
        nx::core)

# <windows.h> stays in the implementation units' global module fragments; keep it lean and
# without the min/max macros.
if(WIN32)
    target_compile_definitions(nxPlatform PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
