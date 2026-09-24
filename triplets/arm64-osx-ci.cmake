# CI triplet for macOS (Apple Silicon): identical to the built-in arm64-osx except
# release-only. CI never debugs dependency internals, and skipping every port's debug
# variant roughly halves both the multi-hour cold dependency build and its disk footprint
# on the runner. Wired via VCPKG_OVERLAY_TRIPLETS in the macos-ci-release CMake preset.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)

set(VCPKG_BUILD_TYPE release)
