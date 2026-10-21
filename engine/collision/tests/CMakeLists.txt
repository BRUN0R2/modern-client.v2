# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(nxCollisionTests
    SOURCES
        hullsTests.cpp
    DEPENDS
        nx::collision
        nx::platform)

# A test reads the hulls of the BSP seed of the fuzz corpus.
target_compile_definitions(nxCollisionTests PRIVATE NX_FUZZ_CORPUS_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/fuzz/corpus")
