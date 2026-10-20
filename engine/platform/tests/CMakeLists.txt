# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(nxPlatformTests
    SOURCES
        clockTests.cpp
        fileSystemTests.cpp
        windowTests.cpp
    DEPENDS
        nx::platform
    # Windows without a display, in CI and on a developer's desktop alike.
    ENVIRONMENT
        SDL_VIDEODRIVER=offscreen)
