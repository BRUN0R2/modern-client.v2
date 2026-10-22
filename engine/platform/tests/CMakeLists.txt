# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(nxPlatformTests
    SOURCES
        audioTests.cpp
        clockTests.cpp
        fileSystemTests.cpp
        windowTests.cpp
    DEPENDS
        nx::platform
    # Windows without a display and sound played to no device, in CI and on a developer's desktop
    # alike: SDL's dummy output takes blocks at the pace of a real one.
    ENVIRONMENT
        SDL_VIDEODRIVER=offscreen
        SDL_AUDIO_DRIVER=dummy)
