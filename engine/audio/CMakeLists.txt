# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# Sounds come from the game's WAV files: the same-layer dependency on nx.assets the catalog declares
# (docs/01-architecture.md, section 1.1).
nxAddModuleLibrary(nxAudio
    ALIAS nx::audio
    LAYER 3
    MODULES
        src/nx.audio.cppm
        src/sound.cppm
        src/mixer.cppm
    SOURCES
        src/sound.cpp
        src/mixer.cpp
    DEPENDS
        nx::core
        nx::jobs
        nx::assets
    LATERAL
        nx::assets)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
