# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(nxSettingsTests
    SOURCES
        filesTests.cpp
        clientCvarsTests.cpp
    DEPENDS
        nx::settings
        nx::console
        nx::platform)
