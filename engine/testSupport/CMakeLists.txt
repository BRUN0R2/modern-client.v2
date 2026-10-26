# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# Helpers shared by tests and synthetic benchmarks (docs/18-testing-and-quality.md).
add_library(nxTestSupport INTERFACE)
target_include_directories(nxTestSupport INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(nx::testSupport ALIAS nxTestSupport)
