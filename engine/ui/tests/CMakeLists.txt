# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(nxUiTests
    SOURCES
        panelTests.cpp
        fontTests.cpp
        fontCatalogTests.cpp
        textTests.cpp
    DEPENDS
        nx::ui
        nx::assets
        nx::render
        nx::rhiVulkan
        nx::platform)

# The text test draws with the build's screen shader.
add_dependencies(nxUiTests nxShaders)
