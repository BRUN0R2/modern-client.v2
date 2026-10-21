# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(nxRenderTests
    SOURCES
        cameraTests.cpp
        gammaTests.cpp
        worldTests.cpp
    DEPENDS
        nx::render
        nx::rhiVulkan
        nx::platform)

# The world test draws the BSP seed of the fuzz corpus with the build's world shader.
target_compile_definitions(nxRenderTests PRIVATE NX_FUZZ_CORPUS_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/fuzz/corpus")
add_dependencies(nxRenderTests nxShaders)
