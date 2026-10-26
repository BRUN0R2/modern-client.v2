# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddTest(csClientTests
    SOURCES
        inputTests.cpp
        mapTests.cpp
        clientGameTests.cpp
        hudTests.cpp
        muzzleFlashTests.cpp
        modelMaterialsTests.cpp
        viewModelTests.cpp
        decalsTests.cpp
        weaponCommandsTests.cpp
        navigationToolsTests.cpp
        hearingTests.cpp
        audioTests.cpp
        windowFontsTests.cpp
    DEPENDS
        cs::client
        cs::shared
        nx::game
        nx::navigation
        nx::ecs
        nx::console
        nx::assets
        nx::render
        nx::ui
        nx::platform
    # Sound played to no device, in CI and on a developer's desktop alike.
    ENVIRONMENT
        SDL_AUDIO_DRIVER=dummy)

# The maps of the tests are the BSP seeds of the fuzz corpus, given other entities; the light styles, the
# navigation profiles and the brush classes are the repository's data/cstrike/lightStyles.toml,
# navigation.toml and entities.toml.
target_compile_definitions(csClientTests PRIVATE NX_FUZZ_CORPUS_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/fuzz/corpus"
                                                 NX_DATA_DIRECTORY="${PROJECT_SOURCE_DIR}/data")
