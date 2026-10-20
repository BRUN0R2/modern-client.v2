# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

if(NX_BUILD_TESTS)
    add_subdirectory(testSupport)
endif()

# Next Engine modules, in dependency order (docs/01-arquitetura.md, section 1.1).
add_subdirectory(core)
add_subdirectory(math)
add_subdirectory(jobs)
add_subdirectory(ecs)
