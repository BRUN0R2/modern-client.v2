# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(modZombiesTests SOURCES settingsTests.cpp DEPENDS mod::zombies nx::platform)
target_compile_definitions(modZombiesTests PRIVATE NX_DATA_DIRECTORY="${PROJECT_SOURCE_DIR}/data")
