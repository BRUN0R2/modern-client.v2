# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(nxConsoleTests
    SOURCES
        parserTests.cpp
        cvarTests.cpp
        consoleTests.cpp
        buttonTests.cpp
        listingTests.cpp
        logLevelsTests.cpp
    DEPENDS
        nx::console)
