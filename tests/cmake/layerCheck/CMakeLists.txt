# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# Empty modules that run the layer check (cmake/nxLayers.cmake) on one case, chosen with
# -DNX_LAYER_CASE=<case>: the regression tests of the rule in docs/01-architecture.md, section 1.1.
cmake_minimum_required(VERSION 4.3)
project(nxLayerCheckFixture LANGUAGES NONE)

list(APPEND CMAKE_MODULE_PATH "${NX_SOURCE_DIR}/cmake")
include(nxLayers)

add_library(thirdParty INTERFACE) # declares no layer

if(NX_LAYER_CASE STREQUAL "lowerLayers")
    add_library(base INTERFACE)
    nxDeclareModuleLayer(base 0 DEPENDS thirdParty)
    add_library(middle INTERFACE)
    add_library(fixture::middle ALIAS middle)
    nxDeclareModuleLayer(middle 1 DEPENDS base)
    add_library(top INTERFACE)
    nxDeclareModuleLayer(top 2 DEPENDS fixture::middle base "$<$<BOOL:ON>:thirdParty>")
elseif(NX_LAYER_CASE STREQUAL "higherLayerDefinedLater")
    # The dependency does not exist yet when `base` is declared.
    add_library(base INTERFACE)
    nxDeclareModuleLayer(base 0 DEPENDS top)
    add_library(top INTERFACE)
    nxDeclareModuleLayer(top 2)
elseif(NX_LAYER_CASE STREQUAL "sameLayer")
    add_library(left INTERFACE)
    nxDeclareModuleLayer(left 1)
    add_library(right INTERFACE)
    nxDeclareModuleLayer(right 1 DEPENDS left)
elseif(NX_LAYER_CASE STREQUAL "declaredLateral")
    # A backend on the interface it implements, declared in the catalog.
    add_library(interface INTERFACE)
    add_library(fixture::interface ALIAS interface)
    nxDeclareModuleLayer(interface 1)
    add_library(backend INTERFACE)
    nxDeclareModuleLayer(backend 1 DEPENDS fixture::interface LATERAL fixture::interface)
elseif(NX_LAYER_CASE STREQUAL "lateralOfAnotherLayer")
    add_library(base INTERFACE)
    nxDeclareModuleLayer(base 0)
    add_library(top INTERFACE)
    nxDeclareModuleLayer(top 1 DEPENDS base LATERAL base)
elseif(NX_LAYER_CASE STREQUAL "layerZero")
    add_library(base INTERFACE)
    nxDeclareModuleLayer(base 0)
    add_library(other INTERFACE)
    nxDeclareModuleLayer(other 0 DEPENDS base)
else()
    message(FATAL_ERROR "layerCheck: unknown case '${NX_LAYER_CASE}'")
endif()
