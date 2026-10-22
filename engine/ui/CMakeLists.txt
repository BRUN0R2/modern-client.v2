# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# The interface's toolkit, in VGUI2's model (ADR-0020).
nxAddModuleLibrary(nxUi
    ALIAS nx::ui
    LAYER 3
    MODULES
        src/nx.ui.cppm
        src/panel.cppm
        src/painter.cppm
        src/label.cppm
        src/textEntry.cppm
        src/font.cppm
        src/fontCatalog.cppm
        src/schemeFont.cppm
        src/text.cppm
    SOURCES
        src/panel.cpp
        src/painter.cpp
        src/label.cpp
        src/textEntry.cpp
        src/font.cpp
        src/fontCatalog.cpp
        src/schemeFont.cpp
        src/text.cpp
    DEPENDS
        nx::core
        nx::platform
        nx::rhi
        nx::assets
        nx::render
    LATERAL
        nx::assets
        nx::render)

# FreeType draws the glyphs; it is an implementation detail of src/font.cpp, never part of the
# interface.
target_link_libraries(nxUi PRIVATE Freetype::Freetype)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
