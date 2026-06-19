# KRStudio CI / Build Artifacts

Honest boundary up front: **"CI green" means the code compiled and linked on a GitHub runner — it does NOT
mean the app runs.** The CI runners have no display and no GPU, and KRStudio is a GL 4.3 + compute-shader
desktop app. The real acceptance test is a **colleague launching the downloaded artifact on real hardware with a
GL-4.3-capable GPU** (marked OPERATOR/COLLEAGUE VERIFICATION REQUIRED below).

---

## Phase 1 — CI RECON (state as of 2026-06-18, commit 66ea5c6)

Inspected: `.github/workflows/`, `vcpkg.json`, `CMakePresets.json`, root `CMakeLists.txt`, and the live GitHub
Actions REST API (anonymous; `gh` CLI is not installed locally).

### What workflows exist
- `.github/workflows/win.yml` (`ci-win`, `windows-latest`, MSVC + Ninja + vcpkg).
- `.github/workflows/mac.yml` (`ci-mac`, `macos-14`, Ninja + vcpkg).
- **No Linux workflow exists.** (Answering the open question: a Linux build does NOT exist.)

### The actual current state: EVERYTHING fails, at the FIRST step
The last 12+ runs on every commit (275 runs total in history) are **failure**, on BOTH `ci-win` and `ci-mac`.
The failing step is **`Run actions/checkout@v4`** — the very first step. Every subsequent step (vcpkg, configure,
build, archive, upload) is **skipped**. So the dependency resolution and the build have **NEVER been exercised in
CI** — checkout blocks first.

```
JOB: build [failure]
   success   Set up job
   failure   Run actions/checkout@v4      <-- dies here
   skipped   Run lukka/run-vcpkg@v11
   skipped   Configure (Release)
   skipped   Build
   skipped   Archive
   skipped   Run actions/upload-artifact@v4
```

### Root cause of the checkout failure
Both workflows check out with `submodules: recursive`. The repo contains an **orphaned submodule gitlink**:

```
git ls-files --stage | grep ^160000
160000 8dc80c0f5bab9fbb60b4284fbd7ac2aad713c874 0  external/SPlisHSPlasH
```

…but there is **no `.gitmodules` file** and **no `submodule.*` section in `.git/config`**. So the gitlink has no
URL mapping. `actions/checkout@v4 submodules: recursive` runs `git submodule update --init --recursive`, finds the
`external/SPlisHSPlasH` gitlink, cannot resolve a URL for it, and fails the whole checkout. (It is the ONLY gitlink
in the repo — `external/entt`, `external/pugixml`, `external/CoACD` are vendored regular trees, not submodules.)

### Is SPlisHSPlasH actually required?
No — it is **optional and Windows-only as wired**. `CMakeLists.txt`:
- line 192: `if(ENABLE_SPLISH_SPLASH AND EXISTS "${CMAKE_SOURCE_DIR}/external/SPlisHSPlasH/CMakeLists.txt")` — it
  is built (as an isolated ExternalProject superbuild) only when the submodule directory is present.
- The link step references `.lib` files (`SPlisHSPlasH.lib`, `Discregrid.lib`, …) — Windows/MSVC naming only.
- It is fully guarded: `KR_HAS_SPLISH` → the compile define `KR_WITH_SPLISHSPLASH=1`. The sole consumer,
  `src/Rendering/DfsphBackend.cpp`, wraps its entire body in `#if defined(KR_WITH_SPLISHSPLASH)` and stubs out
  otherwise. So a build with the submodule absent compiles cleanly (just without the DFSPH fluid backend).

Conclusion: the fix is to stop the recursive-submodule clone (so checkout succeeds); the `EXISTS` guard then
auto-skips SPlisHSPlasH and the rest of the app still builds. (Local developer builds, where the submodule
content is present, continue to build WITH it.)

### Dependency situation (untested in CI, but identified)
`vcpkg.json` is a manifest of ~22 heavy libraries built **from source** by the vcpkg toolchain at configure time,
with **no binary caching** in either workflow:
`qtbase` (opengl/gui/widgets/sql/network), `imgui`, `qttools`, `qtimageformats`, `opencv4`, `assimp`, `glm`,
`realsense2`, `openvdb`, `tbb`, `zlib`, `blosc`, `physx`, `fcl`, `ccd`, `v-hacd`, `libigl`, `urdfdom`,
`urdfdom-headers`, `opencascade` (OCCT), `mosquitto`, `ompl`.

Anticipated problems once checkout is fixed (NOT yet observed, because checkout has never passed):
- **Build time**: Qt + OCCT + OpenVDB + OpenCV + PhysX from source on one runner is plausibly several hours; the
  GitHub per-job limit is 6 h. With no binary cache, every run rebuilds everything. This is the next real risk.
- **`realsense2`**: needs ATL on Windows (a "Desktop development with C++" MSVC component — usually present on the
  `windows-latest` image, but a risk); on Linux it needs `libusb`/udev. A candidate to drop for CI.
- **Linux**: needs system X11/GL/Wayland dev packages (apt) for the windowing/GL link, plus a vcpkg triplet
  (`x64-linux`). Not yet configured at all.

### Why macOS is OUT OF SCOPE (deferred, by design)
macOS caps OpenGL at **4.1** and deprecated GL entirely; KRStudio's renderer requires **GL 4.3 compute shaders**
(introduced in 4.3, never available on macOS). A Mac build cannot run the compute pipeline as written — making it
work means **porting the renderer from OpenGL to Vulkan via MoltenVK**, a large separate project, not a build-config
fix. `ci-mac` is therefore being disabled this sprint; it is not a CI problem to "fix."

### GATE CI-RECON: PASS (this report)
Accurate current state established with real evidence: platforms (win + mac, no linux), the exact failure
(`checkout` on the orphaned `external/SPlisHSPlasH` gitlink, build never runs), and the dependency breakage risk
(uncached ~22-lib source build vs the 6 h limit). Fixes follow in the sections below.

<!-- Phases 2-4 (Linux build, Windows build, packaging + install guide) appended as they land. -->
