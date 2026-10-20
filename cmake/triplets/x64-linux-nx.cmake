# Linux x64 triplet matching the engine ABI policy (ADR-0005): static libraries,
# no exceptions and no RTTI. Every dependency is built with the same switches as our own code.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

set(VCPKG_CXX_FLAGS "-fno-exceptions -fno-rtti")
set(VCPKG_C_FLAGS "")
