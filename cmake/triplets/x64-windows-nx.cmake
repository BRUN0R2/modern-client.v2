# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Windows x64 triplet matching the engine ABI policy (ADR-0005): static libraries, dynamic CRT,
# no exceptions and no RTTI. Every dependency is built with the same switches as our own code.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CXX_FLAGS "/EHs-c- /D_HAS_EXCEPTIONS=0 /GR-")
set(VCPKG_C_FLAGS "")
