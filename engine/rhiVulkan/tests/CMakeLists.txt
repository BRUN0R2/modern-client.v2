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

# An explicit layer path overrides installed layers in an unelevated process. The negative test
# checks discovery first and skips where the operating system ignores this path (elevated Windows).
set(noValidationLayers "${CMAKE_CURRENT_BINARY_DIR}/noValidationLayers")
file(MAKE_DIRECTORY "${noValidationLayers}")
nxAddTest(nxVulkanValidationTests
    SOURCES
        validationTests.cpp
    DEPENDS
        nx::rhiVulkan
        nx::rhi
        volk::volk
    ENVIRONMENT
        "VK_LAYER_PATH=${noValidationLayers}")
