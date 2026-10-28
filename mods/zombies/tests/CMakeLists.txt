# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(modZombiesTests
    SOURCES settingsTests.cpp spawnTests.cpp
    DEPENDS mod::zombies cs::server cs::shared nx::core nx::math nx::platform nx::console
            nx::assets nx::jobs nx::game nx::navigation)
target_compile_definitions(modZombiesTests PRIVATE NX_DATA_DIRECTORY="${PROJECT_SOURCE_DIR}/data")
target_compile_definitions(modZombiesTests PRIVATE NX_FUZZ_CORPUS_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/fuzz/corpus")
