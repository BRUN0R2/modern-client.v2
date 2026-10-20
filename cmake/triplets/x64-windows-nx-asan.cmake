# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# x64-windows-nx for AddressSanitizer builds (the fuzz preset). Under ASan, MSVC's standard library
# annotates std::vector, std::string and std::optional, and the linker refuses to mix objects with
# and without those annotations. _ANNOTATE_STL compiles the annotations into the dependencies
# without instrumenting them (the standard library's switch for static libraries that ASan
# binaries link), so nothing turns the annotations off and container overflows stay detected in
# the project's code.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CXX_FLAGS "/EHs-c- /D_HAS_EXCEPTIONS=0 /GR- /D_ANNOTATE_STL")
set(VCPKG_C_FLAGS "")
