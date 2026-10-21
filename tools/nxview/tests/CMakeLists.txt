# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(toolNxviewTests
    SOURCES
        mapTests.cpp
    DEPENDS
        tool::nxview
        nx::assets
        nx::render
        nx::platform)

# The maps of the tests are the BSP seed of the fuzz corpus, given other entities.
target_compile_definitions(toolNxviewTests PRIVATE NX_FUZZ_CORPUS_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/fuzz/corpus")
