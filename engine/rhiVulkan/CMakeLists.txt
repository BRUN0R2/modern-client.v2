# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# The Vulkan Memory Allocator's implementation: third-party code, built without the project's
# warnings (docs/02-coding-standards.md, section 12).
add_library(nxVulkanMemoryAllocator STATIC thirdParty/vulkanMemoryAllocator.cpp)
target_link_libraries(nxVulkanMemoryAllocator PRIVATE volk::volk GPUOpen::VulkanMemoryAllocator Vulkan::Headers)

# The backend implements nx.rhi and draws to the windows of nx.platform: the two same-layer
# dependencies the catalog declares (docs/01-architecture.md, section 1.1).
nxAddModuleLibrary(nxRhiVulkan
    ALIAS nx::rhiVulkan
    LAYER 2
    MODULES
        src/nx.rhi.vulkan.cppm
    SOURCES
        src/device.cpp
    DEPENDS
        nx::core
        nx::platform
        nx::rhi
    LATERAL
        nx::platform
        nx::rhi)

# Vulkan stays in the implementation units: volk loads it, and no interface names it.
target_link_libraries(nxRhiVulkan PRIVATE volk::volk Vulkan::Headers GPUOpen::VulkanMemoryAllocator
                                          nxVulkanMemoryAllocator)
if(WIN32)
    target_compile_definitions(nxRhiVulkan PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
