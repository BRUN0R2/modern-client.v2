# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(nxPlatform
    ALIAS nx::platform
    LAYER 2
    MODULES
        src/nx.platform.cppm
        src/audio.cppm
        src/clock.cppm
        src/fileSystem.cppm
        src/steam.cppm
        src/window.cppm
    SOURCES
        src/audio.cpp
        src/clock.cpp
        src/fileSystem.cpp
        src/steam.cpp
        src/window.cpp
    DEPENDS
        nx::core)

# <windows.h> stays in the implementation units' global module fragments; keep it lean and
# without the min/max macros.
if(WIN32)
    target_compile_definitions(nxPlatform PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# SDL3 stays in window.cpp and audio.cpp: no interface of nx.platform names it.
target_link_libraries(nxPlatform PRIVATE SDL3::SDL3)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
