# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# Helpers shared by every test executable (docs/18-testes-e-qualidade.md). nxAddTest links them.
add_library(nxTestSupport INTERFACE)
target_include_directories(nxTestSupport INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(nx::testSupport ALIAS nxTestSupport)
