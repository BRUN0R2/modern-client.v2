# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(toolNxcheckTests
    SOURCES
        checkTests.cpp
    DEPENDS
        tool::nxcheck
        nx::assets
        nx::jobs
        nx::platform)

# The seeds of the fuzz targets are small valid (and broken) files of every format.
target_compile_definitions(toolNxcheckTests PRIVATE NX_FUZZ_CORPUS_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/fuzz/corpus")
