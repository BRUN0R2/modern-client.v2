# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(nxUiTests
    SOURCES
        panelTests.cpp
    DEPENDS
        nx::ui
        nx::assets)
