# CI triplet for Linux: identical to the built-in x64-linux except release-only
# (halves dependency build time + disk + binary-cache size). WIRED into ci-linux
# since 2026-09-25: the debug+release cache repeatedly lost the repository's 10 GB
# Actions-cache quota to the other platforms' caches (run 41 restored nothing and
# rebuilt cold regardless), so the one-time cold rebuild that switching triplets
# costs had already been paid. Local developer builds keep the stock x64-linux
# triplet via the unchanged linux-ninja-release preset defaults.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

set(VCPKG_BUILD_TYPE release)
