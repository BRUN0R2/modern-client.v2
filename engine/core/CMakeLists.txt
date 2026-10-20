# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

nxAddModuleLibrary(nxCore
    ALIAS nx::core
    LAYER 0
    MODULES
        src/nx.core.cppm
        src/introspection.cppm
        src/text.cppm
        src/source.cppm
        src/debugging.cppm
        src/contracts.cppm
        src/failure.cppm
        src/conversion.cppm
        src/byteOrder.cppm
        src/quantity.cppm
        src/units.cppm
        src/enumArray.cppm
        src/flags.cppm
        src/hash.cppm
        src/memory.cppm
        src/log.cppm
    SOURCES
        src/debugging.cpp
        src/contracts.cpp
        src/failure.cpp
        src/memory.cpp
        src/log.cpp
    DEPENDS
        # libstdc++ keeps std::stacktrace in libstdc++exp (nx.core:debugging).
        $<$<CXX_COMPILER_ID:GNU>:stdc++exp>)

if(NX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
