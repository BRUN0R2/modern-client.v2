# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(nxEcsTests
    SOURCES
        entityTests.cpp
        componentTests.cpp
        worldTests.cpp
        queryTests.cpp
    DEPENDS
        nx::ecs)
