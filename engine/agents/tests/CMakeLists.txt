# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(nxAgentsTests SOURCES brainTests.cpp stuckTests.cpp decisionsTests.cpp DEPENDS nx::agents nx::navigation nx::ecs nx::jobs)
target_include_directories(nxAgentsTests PRIVATE "${PROJECT_SOURCE_DIR}/engine/navigation/tests")
