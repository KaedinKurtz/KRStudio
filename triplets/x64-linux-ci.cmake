# FALLBACK CI triplet for Linux: identical to the built-in x64-linux except release-only
# (halves dependency build time + disk). NOT wired by default — ci-linux deliberately keeps
# the stock x64-linux triplet because its GitHub Actions binary cache is already primed with
# most of the ~200 built ports, and any triplet change invalidates every cached package
# (the triplet file content is part of vcpkg's package ABI hash). Switch the
# linux-ninja-release preset to this triplet only if the runner still exhausts its disk
# after the free-space + --clean-after-build measures; the first run after switching pays
# one full cold dependency rebuild. See docs/CI.md.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

set(VCPKG_BUILD_TYPE release)
