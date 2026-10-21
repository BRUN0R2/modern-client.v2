# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

if(NX_BUILD_TESTS)
    add_subdirectory(testSupport)
endif()

# Next Engine modules, in dependency order (docs/01-architecture.md, section 1.1).
add_subdirectory(core)
add_subdirectory(meta)
add_subdirectory(math)
add_subdirectory(console)
add_subdirectory(jobs)
add_subdirectory(platform)
add_subdirectory(rhi)
add_subdirectory(rhiVulkan)
add_subdirectory(ecs)
add_subdirectory(data)
add_subdirectory(assets)
add_subdirectory(collision)
add_subdirectory(render)
add_subdirectory(game)
