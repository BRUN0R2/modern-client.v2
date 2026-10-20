# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(nxCoreTests
    SOURCES
        introspectionTests.cpp
        sourceTests.cpp
        contractsTests.cpp
        failureTests.cpp
        conversionTests.cpp
        quantityTests.cpp
        unitsTests.cpp
        enumArrayTests.cpp
        flagsTests.cpp
        hashTests.cpp
        memoryTests.cpp
        logTests.cpp
    DEPENDS
        nx::core
        xxHash::xxhash)
