# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# The layer rule of docs/01-architecture.md, section 1.1: a module depends only on modules of lower
# layers, and on the modules of its own layer that the catalog declares (a backend on the interface
# it implements), and those it declares are those its sources import. Every module declares its
# layer, dependencies and sources; the checks run once, when the top-level directory finishes
# configuring, so they see every module whatever order the directories are added in (a dependency on
# a higher layer names a module not defined yet when the lower one is).

include_guard(GLOBAL)

# nxDeclareModuleLayer(<target> <layer> [DEPENDS <targets>...] [LATERAL <targets>...]
#     [SOURCES <files>...])
#
# Records that the project module `target` sits in `layer` and links `DEPENDS`. LATERAL names the
# dependencies of the same layer that the catalog of docs/01 declares; each must be of that layer.
# SOURCES are its module units, whose imports are checked against DEPENDS: a module of none is not.
function(nxDeclareModuleLayer target layer)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "" "DEPENDS;LATERAL;SOURCES")
    if(NOT layer MATCHES "^[0-9]+$")
        message(FATAL_ERROR "nx: ${target}: LAYER must be a layer number, not '${layer}'")
    endif()
    set_property(TARGET ${target} PROPERTY NX_LAYER ${layer})
    set_property(TARGET ${target} PROPERTY NX_DEPENDS ${arg_DEPENDS})
    set_property(TARGET ${target} PROPERTY NX_LATERAL ${arg_LATERAL})
    set(sources "")
    foreach(source IN LISTS arg_SOURCES)
        cmake_path(ABSOLUTE_PATH source NORMALIZE)
        list(APPEND sources "${source}")
    endforeach()
    set_property(TARGET ${target} PROPERTY NX_SOURCES ${sources})

    get_property(modules GLOBAL PROPERTY NX_MODULES)
    if(NOT modules)
        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL nxCheckModuleLayers)
        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL nxCheckModuleImports)
    endif()
    set_property(GLOBAL APPEND PROPERTY NX_MODULES ${target})
endfunction()

# nxDeclareIndependentTree(<directory> [ALLOW <targets>...])
#
# Records that the targets under `directory`, a subdirectory already added, stand apart from the rest of
# the project: they depend on nothing it defines but ALLOW, and their sources import no module of it. The
# other way round is free: a module of the project may depend on them. So rook/, which is MIT and must stay
# so (docs/adr/0033-rook-compiler-and-types.md), can never link what is GPL, whatever it declares about
# itself: the check walks its directories for its targets.
function(nxDeclareIndependentTree directory)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "ALLOW")
    cmake_path(ABSOLUTE_PATH directory NORMALIZE)
    set(allowed "")
    foreach(target IN LISTS arg_ALLOW)
        nxResolveTarget("${target}" resolved)
        list(APPEND allowed "${resolved}")
    endforeach()
    # Through EVAL because a deferred call reads its arguments when it runs, where these names are gone.
    cmake_language(EVAL CODE
        "cmake_language(DEFER DIRECTORY [[${CMAKE_SOURCE_DIR}]] CALL nxCheckIndependentTree [[${directory}]] [[${allowed}]])")
endfunction()

# The targets defined in `directory` and in the directories under it, in `out`.
function(nxTargetsUnder directory out)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        nxTargetsUnder("${subdirectory}" under)
        list(APPEND targets ${under})
    endforeach()
    set(${out} ${targets} PARENT_SCOPE)
endfunction()

# Fails the configuration when a target under `directory` links a target of the project defined outside
# it, other than `allowed`, or when one of its sources imports a module of the project. Imported targets
# are third parties, and names that are no target are system libraries.
function(nxCheckIndependentTree directory allowed)
    get_property(modules GLOBAL PROPERTY NX_MODULES)
    foreach(module IN LISTS modules)
        get_target_property(sources ${module} NX_SOURCES)
        foreach(source IN LISTS sources)
            if(EXISTS "${source}")
                file(READ "${source}" text)
                if(text MATCHES "(^|\n)[ \t]*export[ \t]+module[ \t]+([A-Za-z0-9.]+)[ \t]*;")
                    set(providerOf.${CMAKE_MATCH_2} ${module})
                endif()
            endif()
        endforeach()
    endforeach()

    nxTargetsUnder("${directory}" targets)
    foreach(target IN LISTS targets)
        foreach(property IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(items ${target} ${property})
            if(NOT items)
                continue()
            endif()
            foreach(item IN LISTS items)
                # Generator expressions hold names too: `$<LINK_ONLY:nx::core>`.
                string(REGEX MATCHALL "[A-Za-z0-9_.+-]+(::[A-Za-z0-9_.+-]+)*" names "${item}")
                foreach(name IN LISTS names)
                    if(NOT TARGET "${name}")
                        continue()
                    endif()
                    nxResolveTarget("${name}" resolved)
                    get_target_property(isImported "${resolved}" IMPORTED)
                    if(isImported OR resolved IN_LIST allowed)
                        continue()
                    endif()
                    get_target_property(definedIn "${resolved}" SOURCE_DIR)
                    cmake_path(IS_PREFIX directory "${definedIn}" NORMALIZE isInside)
                    if(NOT isInside)
                        message(FATAL_ERROR
                            "nx: ${target} depends on ${resolved}, outside ${directory}: that directory imports "
                            "nothing of the rest of the project (docs/adr/0033-rook-compiler-and-types.md)")
                    endif()
                endforeach()
            endforeach()
        endforeach()

        get_target_property(targetDirectory ${target} SOURCE_DIR)
        set(sources "")
        foreach(property IN ITEMS SOURCES INTERFACE_SOURCES CXX_MODULE_SET)
            get_target_property(items ${target} ${property})
            if(items)
                list(APPEND sources ${items})
            endif()
        endforeach()
        foreach(source IN LISTS sources)
            if(source MATCHES "^\\$<")
                continue()
            endif()
            cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${targetDirectory}" NORMALIZE)
            if(NOT source MATCHES "\\.(cppm|cpp|hpp)$" OR NOT EXISTS "${source}")
                continue()
            endif()
            nxImportsOf("${source}" names)
            foreach(name IN LISTS names)
                if(providerOf.${name})
                    message(FATAL_ERROR
                        "nx: ${target} imports ${name} (${providerOf.${name}}), outside ${directory}: that directory "
                        "imports nothing of the rest of the project (docs/adr/0033-rook-compiler-and-types.md)")
                endif()
            endforeach()
        endforeach()
    endforeach()
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

# The names of the modules `file` imports, in `out`: neither partitions of its own (`import :name`)
# nor comments, as a declaration starts its line.
function(nxImportsOf file out)
    file(READ "${file}" text)
    string(REGEX MATCHALL "(^|\n)[ \t]*(export[ \t]+)?import[ \t]+[A-Za-z0-9.]+" declarations "${text}")
    list(TRANSFORM declarations REPLACE ".*[ \t]" "")
    set(${out} ${declarations} PARENT_SCOPE)
endfunction()

# Fails the configuration when a module imports a project module it does not declare in DEPENDS, which
# builds as long as another of its dependencies links that one, or declares one it imports nowhere: an
# edge of the graph is one the code has. Imports of what no project module is (std) are not checked.
function(nxCheckModuleImports)
    get_property(modules GLOBAL PROPERTY NX_MODULES)
    foreach(module IN LISTS modules)
        get_target_property(sources ${module} NX_SOURCES)
        if(NOT sources)
            continue()
        endif()
        foreach(source IN LISTS sources)
            file(READ "${source}" text)
            if(text MATCHES "(^|\n)[ \t]*export[ \t]+module[ \t]+([A-Za-z0-9.]+)[ \t]*;")
                set(providerOf.${CMAKE_MATCH_2} ${module})
            endif()
        endforeach()
    endforeach()
    foreach(module IN LISTS modules)
        get_target_property(sources ${module} NX_SOURCES)
        if(NOT sources)
            continue()
        endif()
        get_target_property(dependencies ${module} NX_DEPENDS)
        set(declared "")
        foreach(dependency IN LISTS dependencies)
            nxResolveTarget("${dependency}" resolved)
            list(APPEND declared "${resolved}")
        endforeach()
        set(imported "")
        foreach(source IN LISTS sources)
            nxImportsOf("${source}" names)
            foreach(name IN LISTS names)
                set(provider "${providerOf.${name}}")
                if(NOT provider OR provider STREQUAL module)
                    continue()
                endif()
                list(APPEND imported "${provider}")
                if(NOT provider IN_LIST declared)
                    message(SEND_ERROR
                        "nx: ${module} imports ${name} (${provider}) in ${source}, not among its DEPENDS "
                        "(docs/01-architecture.md, section 1.1)")
                endif()
            endforeach()
        endforeach()
        foreach(dependency IN LISTS declared)
            if(TARGET "${dependency}")
                get_target_property(dependencySources "${dependency}" NX_SOURCES)
                if(dependencySources AND NOT dependency IN_LIST imported)
                    message(SEND_ERROR
                        "nx: ${module} declares ${dependency} in DEPENDS, and none of its sources imports it "
                        "(docs/01-architecture.md, section 1.1)")
                endif()
            endif()
        endforeach()
    endforeach()
endfunction()
