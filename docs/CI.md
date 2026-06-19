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

---

## Phase 2/3 — the fixes (Linux + Windows)

Applied, in order, each verified against the live Actions API:

1. **Checkout (the universal blocker) — FIXED.** Both workflows now check out with `submodules: false`. The
   orphaned `external/SPlisHSPlasH` gitlink is no longer fetched; CMake's `EXISTS` guard auto-skips it.
   *Observed:* `Run actions/checkout@v4` went green on both win and (new) linux — previously it was the failing step.

2. **`lukka/run-vcpkg@v11` — REMOVED.** After checkout passed, the next run failed fast inside `run-vcpkg`.
   Replaced it with the runner's **pre-installed vcpkg** (`VCPKG_ROOT=$VCPKG_INSTALLATION_ROOT`) — fewer moving
   parts, no third-party action. *Observed:* the run then advanced past setup into the Configure step.

3. **Dependency build caching.** ~22 heavy libs (Qt, OpenCV, OpenVDB, PhysX, OCCT, OMPL, …) build from source in
   manifest mode at configure time. Added a transparent `files` binary cache
   (`VCPKG_BINARY_SOURCES=clear;files,<ws>/vcpkg-bincache,readwrite`) persisted by `actions/cache` keyed on
   `vcpkg.json`. The first run pays the full multi-hour build and primes the cache; later runs restore prebuilt
   binaries. (Note: a partial cache still persists when a job FAILS — only a *cancellation* skips the cache upload,
   so `concurrency.cancel-in-progress` must not fire mid-prime.)

4. **Linux workflow — CREATED.** `.github/workflows/ci-linux` (`ubuntu-22.04`): apt-installs the GL/X11/Wayland/
   xcb + build-tool system packages vcpkg's qtbase and the windowing/GL link need, then the same vcpkg + preset
   path via a new `linux-ninja-release` CMake preset (`x64-linux`). Includes a best-effort headless
   `ldd` + offscreen-start smoke (non-fatal — the runner has no GPU).

5. **macOS — DISABLED.** `ci-mac` now runs only on manual `workflow_dispatch` (was push/PR), so it stops failing
   every commit. See the Mac section below for why it is out of scope.

**Status as of this writing (commit 5889a66):** both `ci-win` and `ci-linux` reached the **Configure** step and are
building the vcpkg dependency set — the first time the build phase has ever run in CI. Whether each goes fully
green (Build → Archive → artifact) depends on that long cold build completing under the 6 h job limit and on each
dependency building on its platform; failures from here are per-dependency and are fixed by iterating on the
manifest/apt list (the cache makes each retry resume rather than restart). **This is the honest boundary: getting
to a green compile+link may take one or more cache-priming runs.**

---

## Phase 4 — packaging + install (for a non-developer colleague)

> HONEST BOUNDARY (repeated because it matters): a green CI run means KRStudio **compiled and linked** on a
> headless runner. It does NOT prove the GUI launches or renders. **The real acceptance test is a colleague
> running the downloaded artifact on a real machine with a GL-4.3 GPU — OPERATOR/COLLEAGUE VERIFICATION REQUIRED.**

### Hardware / driver requirements (BOTH platforms)
- A GPU + drivers supporting **OpenGL 4.3 or newer with compute shaders** (KRStudio's renderer is GL-4.3-compute).
  This rules out very old GPUs, most integrated GPUs older than ~2014, headless servers without a GPU, and
  **all Macs** (see below).
- 64-bit Windows 10/11, or a reasonably current 64-bit Linux (glibc ≥ the runner's — built on Ubuntu 22.04).

### Where to get the artifact
GitHub → the repo → **Actions** → pick the latest green **ci-win** or **ci-linux** run → **Artifacts** →
download `KRStudio-Windows` / `KRStudio-Linux`. (Artifacts are downloadable zips; no build-from-source needed.)

### Windows
1. Download `KRStudio-Windows` and unzip it.
2. Run `RoboticsSoftware.exe`.
3. If Windows prompts about the **Visual C++ runtime**, install the latest *Microsoft Visual C++ 2015-2022
   Redistributable (x64)*. (CI currently zips the build tree; the Phase-4 follow-up is a `windeployqt` dist that
   bundles the Qt DLLs + plugins so no separate Qt install is needed — noted as a refinement.)

### Linux
1. Download `KRStudio-Linux` and unzip it.
2. `chmod +x RoboticsSoftware && ./RoboticsSoftware`.
3. You need a working GL 4.3 driver (Mesa ≥ recent for AMD/Intel, or NVIDIA's proprietary driver). The bundled
   zip is the binary + assets; some system libs (glibc, libstdc++, the GL driver) come from your distro.

### Why macOS is deferred (NOT shipped this sprint)
macOS caps OpenGL at 4.1 and has deprecated GL; KRStudio needs **GL 4.3 compute shaders**, which never existed on
macOS. A Mac build cannot run the compute pipeline — supporting it means **porting the renderer to Vulkan via
MoltenVK**, a large separate project, not a CI fix. `ci-mac` is parked on manual dispatch until then.

### GATE PACKAGING
The per-platform download → run steps and the GL-4.3 hardware requirement are documented above. The clean-bundle
refinement (windeployqt on Windows; an AppImage/`linuxdeployqt` on Linux) is the remaining polish once the builds
are reliably green.
