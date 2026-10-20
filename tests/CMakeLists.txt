# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# Tests that span several modules (docs/18-testing-and-quality.md). Module unit tests live next to
# their module, in engine/<module>/tests.
add_subdirectory(integration)

# The layer check of the build itself (cmake/nxLayers.cmake), configured on a fixture of empty
# modules: each case must pass, or fail naming the offending dependency.
set(layerCases lowerLayers higherLayerDefinedLater sameLayer layerZero)
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
set_tests_properties(LayerCheck.layerZero PROPERTIES
    PASS_REGULAR_EXPRESSION "nx: other \\(layer 0\\) depends on base \\(layer 0\\)")
