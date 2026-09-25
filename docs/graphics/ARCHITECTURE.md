# KRS Graphics — Engine/Application Split & Enterprise Architecture

Plan of record for the standalone graphics library (decision record: `docs/VULKAN_PORT_PLAN.md`
header). Companion documents: `IMPLEMENTATION_PLAN.md` (the work packages), `CI_PIPELINE.md` (the
verification harness that gates every work package).

Honest boundary up front: this document defines structure and contracts. Nothing here is "done"
until its CI gate in `CI_PIPELINE.md` is green — the scaffolding under `engine/` is the v0 skeleton
of this architecture, not the port.

---

## 1. The two codebases

Today there is **no boundary**: `src/Rendering` reaches into ECS components (`components.hpp`),
UI widgets own `RenderingSystem` instances, gates live inside renderer init, and 24 public headers
leak `GLuint`. The target state is two products in one repository (monorepo now, extraction later —
owner decision 2026-09-25):

```
KRStudio.git
├── engine/                  KRS GRAPHICS — the library.  Standalone CMake project.
│                            Zero knowledge of robots, ECS, Qt, or KRStudio.
│                            Compiles, installs, and tests WITHOUT the app.
└── (repo root)              KRSTUDIO — the robotics application.
                             Links the INSTALLED engine package. Owns ECS, physics,
                             CAD/URDF, UI, and the legacy GL renderer until parity.
```

**The dependency arrow points one way, forever: app → engine.** The engine never includes an app
header; the app never includes an engine header outside `engine/include/krsg/`. Both rules are
machine-enforced from day one (`scripts/check_engine_boundary.py`, CI stage S0 — not code review).

**Extraction criterion**: when the public API survives two consecutive work packages without a
breaking change (see §6 versioning), `engine/` moves to its own repository via `git filter-repo`
(history preserved), and the app consumes it as a versioned binary through the in-repo vcpkg
overlay port. Until then, atomic cross-boundary commits are worth more than repo purity.

## 2. Library identity

- **Name**: KRS Graphics. CMake package `KRSGraphics`, targets `krsg::krsg` (core) and
  `krsg::qt` (Qt adapter). C++ namespace `krsg`. Export macro `KRSG_API`.
- **Language/toolchain**: C++17 (matches the app; C++20 is a later, versioned decision), CMake
  ≥ 3.24, warnings-as-errors *inside the engine only* (`-Wall -Wextra -Werror` / `/W4 /WX`).
- **Graphics API**: Vulkan 1.2 core + selected extensions (dynamic rendering via
  `VK_KHR_dynamic_rendering` when present, fallback render passes — lavapipe on ubuntu-22.04 and
  MoltenVK both gate what we can assume; the capability table lives in `IMPLEMENTATION_PLAN.md` WP2).
  macOS runs Vulkan through **MoltenVK** (`VK_KHR_portability_subset` handled explicitly).
- **Third-party inside the engine** (vendored via FetchContent, pinned SHAs, no vcpkg dependency —
  this is what keeps graphics-ci under 10 minutes): `volk` (loader), `VulkanMemoryAllocator`,
  `Vulkan-Headers`. Shader toolchain at build time only: `glslangValidator` + `spirv-tools`
  (host tools, not linked). Nothing else. Every addition is an ADR.

## 3. Layering (inside `engine/`)

```
┌────────────────────────────────────────────────────────────────────┐
│ L3  ADAPTERS (optional, separately-built targets)                  │
│     krsg-qt: QWindow→VkSurfaceKHR glue + a presenter widget.       │
│     The ONLY place Qt may appear. Future: krsg-glfw, krsg-sdl.     │
├────────────────────────────────────────────────────────────────────┤
│ L2  RENDERER TOOLKIT  (krsg/render/*)                              │
│     Mesh/material/texture registries, the deferred pipeline blocks │
│     (G-buffer, lighting, tonemap), compute-effect framework (the   │
│     fluid/MPM/smoke pipelines port INTO this layer), debug labels, │
│     GPU timers, screenshot/readback service.                       │
├────────────────────────────────────────────────────────────────────┤
│ L1  RENDER GRAPH  (krsg/graph/*)                                   │
│     Passes declare reads/writes; the graph derives image layout    │
│     transitions and pipeline barriers, aliases transient           │
│     attachments, orders submission. Replaces the app's implicit GL │
│     state machine and its 37 hand-placed glMemoryBarrier sites.    │
├────────────────────────────────────────────────────────────────────┤
│ L0  RHI  (krsg/rhi/*)                                              │
│     Instance/device/queues, VMA memory, swapchain, buffers/images/ │
│     samplers, SPIR-V pipelines + descriptor allocation, command    │
│     recording, timeline-semaphore frame pacing, readback rings.    │
│     Backends: vulkan/ (the product), null/ (unit tests, CI logic   │
│     tests, and the API-contract reference implementation).         │
└────────────────────────────────────────────────────────────────────┘
```

Layer rule (S0-enforced by include scanning): L(n) may include L(n-1)…L0; never upward, never
sideways into an adapter. Consumers may use any layer — a future non-KRStudio 3D program can sit
directly on L0/L1 and ignore the toolkit.

## 4. Public API rules (`engine/include/krsg/` — the contract)

1. **No foreign types.** No Vulkan, Qt, OpenGL, GLM, or STL-ABI-fragile types (`std::string` in/out
   is allowed C++17-pragmatically; document any exception in an ADR). Math crosses the boundary as
   POD (`float[16]` column-major, `krsg::Float3` etc.) — GLM stays app-side and converts at the seam.
2. **Opaque handles, not pointers**: `struct BufferHandle { uint64_t v; }` etc., backed by
   generational handle tables inside. Use-after-destroy is a validation error, not a crash.
3. **POD descriptor structs** with defaulted members for all creation
   (`BufferDesc{.size, .usage, .memory}`) — additive evolution without overload explosion.
4. **Errors**: recoverable failures return `krsg::Result<T>`; programmer errors hit the installed
   `krsg::SetDebugCallback` sink and (debug builds) assert. No exceptions across the ABI.
5. **Threading contract v1**: one `Frame` recorded per device at a time; resource creation is
   thread-safe; contract widens by ADR only.
6. **WSI inversion**: the library never creates windows and never includes a windowing header.
   Consumers hand it a `SurfaceSource` (platform window handles in a tagged union — HWND, xcb,
   wayland, CAMetalLayer) or run fully **headless** (render-to-texture + readback), which is a
   first-class path because CI and the robotics gates live there. `krsg-qt` is sugar over
   `SurfaceSource`, nothing more.
7. Every public entity carries a doc comment; `krsg.h` is the umbrella; headers are IWYU-clean
   (S0 compiles each public header in isolation).

Why this shape: the existing renderer already renders offscreen and lets widgets blit
(`RenderingSystem.cpp:262-275, 2639-2690`) — the library boundary formalizes the seam the code
already has, which is the single strongest reason this split is tractable.

## 5. What lives where after the split

| Concern | Today | Target |
|---|---|---|
| Device/context, swapchain | `RenderingSystem` + Qt GL contexts | engine L0 (+ `krsg-qt` presenter) |
| Pass ordering, barriers, FBOs | implicit GL state, hand sync | engine L1 render graph |
| G-buffer/lighting/tonemap, LTC, IBL | `OpaquePass`/`LightingPass`/… | engine L2 pipeline blocks |
| Fluid/MPM/smoke/SDF GPU compute | `FluidSystem`/`MpmSystem`/… (GL 4.3) | engine L2 compute framework; **solver logic stays app-side**, dispatch/resources engine-side via the sim→render buffer contract (`SimStream`s) |
| Shaders | `shaders/*.glsl` GL dialect | `engine/shaders/` Vulkan GLSL → SPIR-V at build; manifest-enforced |
| ECS, robotics, CAD, URDF, physics | app | app (never moves) |
| Viewport widgets, docking, UI | app (QOpenGLWidget) | app (`krsg-qt` presenter widget) |
| Headless gates/selftests | inside renderer init, GL-only | engine tests (engine concerns) + app gates calling the engine headless API (robotics concerns) |
| Legacy GL renderer | the renderer | app-side, untouched, shipped default until the parity dashboard flips it (parallel-port requirement) |

## 6. Versioning, ABI, packaging

- **SemVer** from `0.1.0`; pre-1.0 minor bumps may break API but each break lands with a migration
  note in `CHANGELOG.md`. `krsg/version.h` exposes compile-time and runtime version queries
  (mismatch is a load-time check in debug).
- Build both static and shared (`KRSG_BUILD_SHARED`); the app links **shared** in dev (fast
  iteration — the "precompiled library, quicker to reference" goal) and can pin static for release.
- `install(EXPORT)` produces `KRSGraphicsConfig.cmake` + version file; CI stage S2 proves a clean
  consumer builds against the **installed** package only (`engine/tests/consume/`), which is the
  packaging contract test.
- The in-repo vcpkg overlay port (`vcpkg-overlay/ports/krsgraphics/`, added when the API first
  stabilizes) is the dress rehearsal for repo extraction.

## 7. Decisions log

ADRs live in `docs/graphics/adr/` (`NNN-title.md`: context, decision, consequences). Standing:

- **ADR-001** Direct Vulkan core, Qt-free public API (owner, 2026-09-25) — supersedes the QRhi
  recommendation; rationale in the `VULKAN_PORT_PLAN.md` decision record.
- **ADR-002** Monorepo with CI-enforced boundary now; repo extraction on API stability (owner,
  2026-09-25).
- **ADR-003** Engine deps vendored via pinned FetchContent, not vcpkg — keeps the engine buildable
  anywhere in minutes and keeps graphics-ci independent of the app's 4-hour dependency build.
- **ADR-004** macOS via MoltenVK, revisitable: the sealed API means a native-Metal L0 backend is a
  backend swap, not an API break, if MoltenVK measurements (WP2 gate) demand it.
