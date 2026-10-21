# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddProgram(nxview
    SOURCES
        src/main.cpp
    DEPENDS
        nx::core
        nx::platform
        nx::rhi
        nx::rhiVulkan)
