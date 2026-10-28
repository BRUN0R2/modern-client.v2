# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(csClientTests
    SOURCES
        inputTests.cpp
        mapTests.cpp
        mapLightTests.cpp
        clientGameTests.cpp
        hudTests.cpp
        muzzleFlashTests.cpp
        tempEntitiesTests.cpp
        particlesTests.cpp
        modelMaterialsTests.cpp
        viewModelTests.cpp
        decalsTests.cpp
        weaponCommandsTests.cpp
        hearingTests.cpp
        audioTests.cpp
        windowFontsTests.cpp
    DEPENDS
        cs::client
        cs::shared
        nx::game
        nx::ecs
        nx::collision
        nx::console
        nx::assets
        nx::render
        nx::ui
        nx::platform
    # Sound played to no device, in CI and on a developer's desktop alike.
    ENVIRONMENT
        SDL_AUDIO_DRIVER=dummy)

# The maps of the tests are the BSP seed of the fuzz corpus, given other entities; content uses
# the repository's data/cstrike/.
target_compile_definitions(csClientTests PRIVATE NX_FUZZ_CORPUS_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/fuzz/corpus"
                                                 NX_DATA_DIRECTORY="${PROJECT_SOURCE_DIR}/data")
