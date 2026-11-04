# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# Tests that span several modules (docs/18-testing-and-quality.md). Module unit tests live next to
# their module, in engine/<module>/tests.
add_subdirectory(integration)

# The layer check of the build itself (cmake/nxLayers.cmake), configured on a fixture of empty
# modules: each case must pass, or fail naming the offending dependency.
set(layerCases lowerLayers higherLayerDefinedLater sameLayer declaredLateral lateralOfAnotherLayer layerZero
    declaredImports undeclaredImport dependencyNotImported
    independentTree independentTreeLinksOutside independentTreeLinksModule independentTreeLinksInExpression
    independentTreeImportsModule)
foreach(layerCase IN LISTS layerCases)
    add_test(NAME LayerCheck.${layerCase}
        COMMAND "${CMAKE_COMMAND}"
            -S "${CMAKE_CURRENT_SOURCE_DIR}/cmake/layerCheck"
            -B "${CMAKE_CURRENT_BINARY_DIR}/layerCheck/${layerCase}"
            -G "${CMAKE_GENERATOR}"
            "-DNX_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DNX_LAYER_CASE=${layerCase}")
endforeach()
set_tests_properties(LayerCheck.higherLayerDefinedLater PROPERTIES
    PASS_REGULAR_EXPRESSION "nx: base \\(layer 0\\) depends on top \\(layer 2\\)")
set_tests_properties(LayerCheck.sameLayer PROPERTIES
    PASS_REGULAR_EXPRESSION "nx: right \\(layer 1\\) depends on left \\(layer 1\\)")
set_tests_properties(LayerCheck.lateralOfAnotherLayer PROPERTIES
    PASS_REGULAR_EXPRESSION "nx: top \\(layer 1\\) declares base LATERAL, but it is not a module of layer 1")
set_tests_properties(LayerCheck.layerZero PROPERTIES
    PASS_REGULAR_EXPRESSION "nx: other \\(layer 0\\) depends on base \\(layer 0\\)")
set_tests_properties(LayerCheck.undeclaredImport PROPERTIES
    PASS_REGULAR_EXPRESSION "nx: top imports fixture.base \\(base\\)")
set_tests_properties(LayerCheck.dependencyNotImported PROPERTIES
    PASS_REGULAR_EXPRESSION "nx: top declares base in DEPENDS, and none of its sources imports it")
set_tests_properties(LayerCheck.independentTreeLinksOutside PROPERTIES
    PASS_REGULAR_EXPRESSION "nx: tree depends on helper, outside")
set_tests_properties(LayerCheck.independentTreeLinksModule LayerCheck.independentTreeLinksInExpression PROPERTIES
    PASS_REGULAR_EXPRESSION "nx: tree depends on engine, outside")
set_tests_properties(LayerCheck.independentTreeImportsModule PROPERTIES
    PASS_REGULAR_EXPRESSION "nx: tree imports fixture.engine \\(engine\\)")

# rook/ configures and builds on its own, with a compiler and nothing else (docs/adr/0033), and its
# conformance suite passes there. The build of its own proves what the check above cannot: that nothing
# outside rook/ is needed, the CMake files included.
if(NX_BUILD_ROOK)
    add_test(NAME Rook.BuildsOnItsOwn
        COMMAND "${CMAKE_CTEST_COMMAND}"
            --build-and-test "${PROJECT_SOURCE_DIR}/rook" "${CMAKE_CURRENT_BINARY_DIR}/rookOnItsOwn"
            --build-generator "${CMAKE_GENERATOR}"
            --build-options "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}" "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            --test-command "${CMAKE_CTEST_COMMAND}" --output-on-failure)
endif()
