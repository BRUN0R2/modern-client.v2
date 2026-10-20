# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(nxMath
    ALIAS nx::math
    LAYER 1
    MODULES
        src/nx.math.cppm
        src/vector.cppm
        src/matrix.cppm
        src/angles.cppm
        src/quaternion.cppm
        src/bounds.cppm
        src/plane.cppm
    SOURCES
        src/angles.cpp
        src/quaternion.cpp
    DEPENDS
        nx::core)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
