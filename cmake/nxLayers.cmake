# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# The layer rule of docs/01-architecture.md, section 1.1: a module depends only on modules of lower
# layers. Every module declares its layer and dependencies; the check runs once, when the top-level
# directory finishes configuring, so it sees every module whatever order the directories are added
# in (a dependency on a higher layer names a module not defined yet when the lower one is).

include_guard(GLOBAL)

# nxDeclareModuleLayer(<target> <layer> [DEPENDS <targets>...])
#
# Records that the project module `target` sits in `layer` and links `DEPENDS`.
function(nxDeclareModuleLayer target layer)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "" "DEPENDS")
    if(NOT layer MATCHES "^[0-9]+$")
        message(FATAL_ERROR "nx: ${target}: LAYER must be a layer number, not '${layer}'")
    endif()
    set_property(TARGET ${target} PROPERTY NX_LAYER ${layer})
    set_property(TARGET ${target} PROPERTY NX_DEPENDS ${arg_DEPENDS})

    get_property(modules GLOBAL PROPERTY NX_MODULES)
    if(NOT modules)
        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL nxCheckModuleLayers)
    endif()
    set_property(GLOBAL APPEND PROPERTY NX_MODULES ${target})
endfunction()

# Fails the configuration when a module depends on a project module of the same or a higher
# layer. Dependencies that are not project modules (third-party targets, generator expressions,
# system libraries) have no layer and are not checked.
function(nxCheckModuleLayers)
    get_property(modules GLOBAL PROPERTY NX_MODULES)
    foreach(module IN LISTS modules)
        get_target_property(layer ${module} NX_LAYER)
        get_target_property(dependencies ${module} NX_DEPENDS)
        foreach(dependency IN LISTS dependencies)
            if(NOT TARGET "${dependency}")
                continue()
            endif()
            get_target_property(aliased "${dependency}" ALIASED_TARGET)
            if(aliased)
                set(dependency "${aliased}")
            endif()
            get_target_property(dependencyLayer "${dependency}" NX_LAYER)
            if(NOT dependencyLayer MATCHES "^[0-9]+$")
                continue()
            endif()
            if(NOT dependencyLayer LESS layer)
                message(FATAL_ERROR
                    "nx: ${module} (layer ${layer}) depends on ${dependency} (layer ${dependencyLayer}); "
                    "a module depends only on lower layers (docs/01-architecture.md, section 1.1)")
            endif()
        endforeach()
    endforeach()
endfunction()
