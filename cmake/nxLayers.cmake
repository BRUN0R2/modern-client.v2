# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# The layer rule of docs/01-architecture.md, section 1.1: a module depends only on modules of lower
# layers, and on the modules of its own layer that the catalog declares (a backend on the interface
# it implements). Every module declares its layer and dependencies; the check runs once, when the
# top-level directory finishes configuring, so it sees every module whatever order the directories
# are added in (a dependency on a higher layer names a module not defined yet when the lower one is).

include_guard(GLOBAL)

# nxDeclareModuleLayer(<target> <layer> [DEPENDS <targets>...] [LATERAL <targets>...])
#
# Records that the project module `target` sits in `layer` and links `DEPENDS`. LATERAL names the
# dependencies of the same layer that the catalog of docs/01 declares; each must be of that layer.
function(nxDeclareModuleLayer target layer)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "" "DEPENDS;LATERAL")
    if(NOT layer MATCHES "^[0-9]+$")
        message(FATAL_ERROR "nx: ${target}: LAYER must be a layer number, not '${layer}'")
    endif()
    set_property(TARGET ${target} PROPERTY NX_LAYER ${layer})
    set_property(TARGET ${target} PROPERTY NX_DEPENDS ${arg_DEPENDS})
    set_property(TARGET ${target} PROPERTY NX_LATERAL ${arg_LATERAL})

    get_property(modules GLOBAL PROPERTY NX_MODULES)
    if(NOT modules)
        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL nxCheckModuleLayers)
    endif()
    set_property(GLOBAL APPEND PROPERTY NX_MODULES ${target})
endfunction()

# The target `name` names, through an alias; `name` itself when it is no target.
function(nxResolveTarget name out)
    set(resolved "${name}")
    if(TARGET "${name}")
        get_target_property(aliased "${name}" ALIASED_TARGET)
        if(aliased)
            set(resolved "${aliased}")
        endif()
    endif()
    set(${out} "${resolved}" PARENT_SCOPE)
endfunction()

# Fails the configuration when a module depends on a project module of a higher layer, or of its
# own layer without declaring it LATERAL, and when a LATERAL module is not of the same layer.
# Dependencies that are not project modules (third-party targets, generator expressions, system
# libraries) have no layer and are not checked.
function(nxCheckModuleLayers)
    get_property(modules GLOBAL PROPERTY NX_MODULES)
    foreach(module IN LISTS modules)
        get_target_property(layer ${module} NX_LAYER)
        get_target_property(dependencies ${module} NX_DEPENDS)
        get_target_property(declaredLaterals ${module} NX_LATERAL)
        if(NOT declaredLaterals) # unset, or the -NOTFOUND of a module that declares none
            set(declaredLaterals "")
        endif()
        set(laterals "")
        foreach(lateral IN LISTS declaredLaterals)
            nxResolveTarget("${lateral}" resolved)
            set(lateralLayer "")
            if(TARGET "${resolved}")
                get_target_property(lateralLayer "${resolved}" NX_LAYER)
            endif()
            if(NOT lateralLayer STREQUAL layer)
                message(FATAL_ERROR
                    "nx: ${module} (layer ${layer}) declares ${resolved} LATERAL, but it is not a module of "
                    "layer ${layer}; LATERAL lists only the same-layer dependencies of docs/01-architecture.md")
            endif()
            list(APPEND laterals "${resolved}")
        endforeach()
        foreach(dependency IN LISTS dependencies)
            if(NOT TARGET "${dependency}")
                continue()
            endif()
            nxResolveTarget("${dependency}" dependency)
            get_target_property(dependencyLayer "${dependency}" NX_LAYER)
            if(NOT dependencyLayer MATCHES "^[0-9]+$")
                continue()
            endif()
            if(dependencyLayer EQUAL layer AND dependency IN_LIST laterals)
                continue()
            endif()
            if(NOT dependencyLayer LESS layer)
                message(FATAL_ERROR
                    "nx: ${module} (layer ${layer}) depends on ${dependency} (layer ${dependencyLayer}); "
                    "a module depends only on lower layers and on the same-layer modules it declares "
                    "LATERAL (docs/01-architecture.md, section 1.1)")
            endif()
        endforeach()
    endforeach()
endfunction()
