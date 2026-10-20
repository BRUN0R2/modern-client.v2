# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(nxIntegrationTests
    SOURCES
        determinismTests.cpp
    DEPENDS
        nx::math
        nx::jobs
        nx::ecs)
