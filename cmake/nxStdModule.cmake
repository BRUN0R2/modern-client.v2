# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# The standard library module as the project target nx::std. How it is built is rook/cmake's
# (rookStdModule.cmake): the toolchain builds on its own, and the engine's build may depend on it, not
# the reverse (docs/adr/0033-rook-compiler-and-types.md).

include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/../rook/cmake/rookStdModule.cmake")

# Creates nx::std. Must run after the project-wide compiler flags are set (cmake/nxCompilerSettings.cmake).
function(nxAddStdModuleLibrary)
    rookAddStdModule(nxStd)
    add_library(nx::std ALIAS nxStd)
endfunction()
