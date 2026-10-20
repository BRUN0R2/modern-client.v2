# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# Builds the C++ standard library module (`import std;`) as the project target nx::std.
#
# The standard library ships a manifest of its module sources (modules.json): Microsoft STL
# next to its headers, libstdc++ and libc++ next to their libraries. This file reads that
# manifest and compiles the `std` module like any other module library, using only stable
# CMake features. CMake's own `import std` support is not used: it is still experimental,
# needs an opt-in gate whose UUID changes with every CMake release, and with MSVC it names
# its library "@cmake_cxx_std.lib", which link.exe reads as a response file.
#
# Only `std` is built. `std.compat` is left out on purpose: it re-exports the C library into
# the global namespace, and project code names everything through `std::`.

include_guard(GLOBAL)

# Sets <outManifest> to the absolute path of the standard library module manifest.
function(nxFindStdModulesManifest outManifest)
    if(MSVC)
        # cl.exe lives in <toolset>/bin/Host<arch>/<arch>/; the manifest in <toolset>/modules/.
        cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH toolset)
        cmake_path(GET toolset PARENT_PATH toolset)
        cmake_path(GET toolset PARENT_PATH toolset)
        cmake_path(GET toolset PARENT_PATH toolset)
        set(candidates "${toolset}/modules/modules.json")
        if(DEFINED ENV{VCToolsInstallDir})
            list(PREPEND candidates "$ENV{VCToolsInstallDir}/modules/modules.json")
        endif()

        foreach(candidate IN LISTS candidates)
            if(EXISTS "${candidate}")
                set(${outManifest} "${candidate}" PARENT_SCOPE)
                return()
            endif()
        endforeach()
        message(FATAL_ERROR "nx: MSVC module manifest not found; looked for: ${candidates}")
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_STANDARD_LIBRARY STREQUAL "libc++")
        set(library "libc++")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
        set(library "libstdc++")
    else()
        message(FATAL_ERROR "nx: no standard library module manifest lookup for ${CMAKE_CXX_COMPILER_ID}")
    endif()

    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" "-print-file-name=${library}.modules.json"
        OUTPUT_VARIABLE manifest
        RESULT_VARIABLE status
        OUTPUT_STRIP_TRAILING_WHITESPACE)

    # The driver echoes the bare file name back when the file does not exist.
    if(NOT status EQUAL 0 OR NOT IS_ABSOLUTE "${manifest}" OR NOT EXISTS "${manifest}")
        message(FATAL_ERROR
            "nx: ${library}.modules.json not found by ${CMAKE_CXX_COMPILER} (got \"${manifest}\"). "
            "The standard library must be installed with its module sources.")
    endif()
    set(${outManifest} "${manifest}" PARENT_SCOPE)
endfunction()

# Reads <manifest> and sets <outSource> to the `std` module source and <outIncludeDirectories>
# to the include directories the manifest requires for compiling it.
function(nxReadStdModuleManifest manifest outSource outIncludeDirectories)
    file(READ "${manifest}" json)
    cmake_path(GET manifest PARENT_PATH base)
    set(source "")
    set(includeDirectories "")

    string(JSON library ERROR_VARIABLE libraryMissing GET "${json}" "library")
    if(library STREQUAL "microsoft/STL")
        # Microsoft STL: {"library": "microsoft/STL", "module-sources": ["std.ixx", ...]}
        string(JSON count LENGTH "${json}" "module-sources")
        math(EXPR last "${count} - 1")
        foreach(index RANGE ${last})
            string(JSON candidate GET "${json}" "module-sources" ${index})
            if(candidate STREQUAL "std.ixx")
                set(source "${candidate}")
            endif()
        endforeach()
    else()
        # Standard format: {"modules": [{"logical-name": "std", "source-path": ..., "local-arguments": {...}}]}
        string(JSON count LENGTH "${json}" "modules")
        math(EXPR last "${count} - 1")
        foreach(index RANGE ${last})
            string(JSON logicalName GET "${json}" "modules" ${index} "logical-name")
            if(NOT logicalName STREQUAL "std")
                continue()
            endif()
            string(JSON source GET "${json}" "modules" ${index} "source-path")

            string(JSON arguments ERROR_VARIABLE argumentsMissing GET "${json}" "modules" ${index} "local-arguments")
            if(argumentsMissing STREQUAL "NOTFOUND")
                string(JSON argumentCount LENGTH "${arguments}")
                math(EXPR lastArgument "${argumentCount} - 1")
                foreach(argumentIndex RANGE ${lastArgument})
                    string(JSON key MEMBER "${arguments}" ${argumentIndex})
                    if(NOT key STREQUAL "system-include-directories")
                        message(FATAL_ERROR "nx: unsupported local argument \"${key}\" in ${manifest}")
                    endif()
                    string(JSON directoryCount LENGTH "${arguments}" "${key}")
                    math(EXPR lastDirectory "${directoryCount} - 1")
                    foreach(directoryIndex RANGE ${lastDirectory})
                        string(JSON directory GET "${arguments}" "${key}" ${directoryIndex})
                        cmake_path(ABSOLUTE_PATH directory BASE_DIRECTORY "${base}" NORMALIZE)
                        list(APPEND includeDirectories "${directory}")
                    endforeach()
                endforeach()
            endif()
        endforeach()
    endif()

    if(source STREQUAL "")
        message(FATAL_ERROR "nx: ${manifest} does not list the `std` module")
    endif()
    cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${base}" NORMALIZE)
    if(NOT EXISTS "${source}")
        message(FATAL_ERROR "nx: ${manifest} lists ${source}, which does not exist")
    endif()

    set(${outSource} "${source}" PARENT_SCOPE)
    set(${outIncludeDirectories} "${includeDirectories}" PARENT_SCOPE)
endfunction()

# Creates nx::std. Must run after the project-wide compiler flags are set, because the std
# module has to be compiled with exactly the ABI flags of every module that imports it.
function(nxAddStdModuleLibrary)
    nxFindStdModulesManifest(manifest)
    nxReadStdModuleManifest("${manifest}" source includeDirectories)

    # Compile a copy inside the build tree: object paths stay short (no hashed directories for
    # out-of-tree sources) and configure_file re-copies whenever the toolchain updates it.
    cmake_path(GET source FILENAME fileName)
    cmake_path(GET source PARENT_PATH sourceDirectory)
    set(copyDirectory "${CMAKE_BINARY_DIR}/nxStd")
    configure_file("${source}" "${copyDirectory}/${fileName}" COPYONLY)

    # Quoted includes in the module source resolve against its original directory.
    list(APPEND includeDirectories "${sourceDirectory}")

    add_library(nxStd STATIC)
    target_sources(nxStd
        PUBLIC
            FILE_SET CXX_MODULES
            BASE_DIRS "${copyDirectory}"
            FILES "${copyDirectory}/${fileName}")
    target_include_directories(nxStd SYSTEM PRIVATE ${includeDirectories})
    target_compile_features(nxStd PUBLIC cxx_std_${NX_CXX_STANDARD})
    add_library(nx::std ALIAS nxStd)

    message(STATUS "nx: std module from ${source}")
endfunction()
