# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(nxRhiVulkanTests
    SOURCES
        deviceTests.cpp
        drawTests.cpp
    DEPENDS
        nx::rhiVulkan
        nx::rhi
        nx::platform)

# The draw tests load the test shaders from bin/shaders/.
add_dependencies(nxRhiVulkanTests nxShaders)
