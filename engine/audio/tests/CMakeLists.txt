# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(nxAudioTests
    SOURCES
        soundTests.cpp
        libraryTests.cpp
        mixerTests.cpp
        deviceTests.cpp
    DEPENDS
        nx::audio
        nx::assets
        nx::platform
    # Sound played to no device, in CI and on a developer's desktop alike: SDL's dummy output takes
    # blocks at the pace of a real one.
    ENVIRONMENT
        SDL_AUDIO_DRIVER=dummy)
