# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(nxMathTests
    SOURCES
        vectorTests.cpp
        matrixTests.cpp
        anglesTests.cpp
        quaternionTests.cpp
        boundsTests.cpp
        planeTests.cpp
        floatingPointTests.cpp
    DEPENDS
        nx::math)
