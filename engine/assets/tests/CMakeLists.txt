# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(nxAssetsTests
    SOURCES
        vfsTests.cpp
        contentTests.cpp
        steamTests.cpp
        wadTests.cpp
        wavTests.cpp
        bspTests.cpp
        sprTests.cpp
        spriteListTests.cpp
        keyValuesTests.cpp
        schemeTests.cpp
        localizationTests.cpp
        mdlTests.cpp
        tgaTests.cpp
    DEPENDS
        nx::assets)

# The shipped data files are tested as they are.
target_compile_definitions(nxAssetsTests PRIVATE NX_DATA_DIRECTORY="${PROJECT_SOURCE_DIR}/data")
