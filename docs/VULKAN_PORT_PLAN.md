# KRStudio Renderer Port Plan — QRhi Parallel Backend (Vulkan on Linux/Windows, native Metal on macOS)

Honest boundary up front: **this is a plan, not a port.** Every claim below that could be checked against the
repo has been checked (file:line citations throughout); every claim about Qt 6.8.3 APIs was verified against the
headers actually installed under `vcpkg_installed/`. The effort numbers are estimates by construction. The plan's
own optimistic total is 25 engineer-weeks; three independent reviews re-baselined it to **32–36 weeks for one
senior graphics engineer**, and that is the number to plan around. The GL backend keeps shipping, untouched and
default, for the entire duration — "parallel port" is a hard requirement of this plan, enforced by CI, not a hope.

---

## 1. Executive summary

**Why.** macOS is blocked. The renderer is OpenGL 4.3 core (compute shaders, SSBOs, image load/store) accessed
exclusively through `QOpenGLFunctions_4_3_Core` — and `src/main.cpp:41` actually requests a **4.5** core context
(one debug helper uses 4.5 DSA at `RenderingSystem.cpp:144-175`). Apple's OpenGL stops at 4.1 and is deprecated.
There is no GL path to macOS.

**What.** A parallel second backend behind a sealed rendering seam, built on **QRhi** — Qt's own hardware
interface layer (public API in the installed Qt 6.8.3: `QtGui/rhi/qrhi.h`, `QtWidgets/qrhiwidget.h`). Backend is
chosen at boot: the legacy GL path ships unchanged; the QRhi path runs **Vulkan on Linux/Windows** (satisfying the
"Vulkan backend" goal and enabling lavapipe software-Vulkan in CI) and **native Metal on macOS** — no MoltenVK.
Native Metal deletes the largest single risk cluster identified in the subsystem maps: MoltenVK's emulated
tessellation, image-atomic support tiers, R32F filtering queries, watchdog-under-translation behavior, and the
unresolved question of whether vcpkg's Qt is even built with Vulkan on macOS (verified: it is not —
`QT_FEATURE_vulkan = -1` in the installed build — QRhi's Metal backend compiles unconditionally on Apple).

**How long.** 25 engineer-weeks optimistic / **32–36 weeks planned** (≈160–180 engineer-days), one senior
graphics engineer, phased so macOS bring-up lands on an already-gate-green backend. Confidence: the ±20% band
comes from three judge reviews that all flagged the same thin phases (deferred spine, sim compute, macOS closure).

**Scope facts** (census-verified): ~25k lines under `src/Rendering` + `include/RenderingHeaders`; 147 GLSL files
(42 vert / 68 frag / 30 comp / 3 geom / 2 tesc / 2 tese, 7,484 lines); ~2,000 raw `gl->gl*` call sites in 42–45
files; 529 loose default-block uniforms in 117 files; 37 `glMemoryBarrier` sites; ~40 headless env-gated selftests
(16 `*Gate.cpp` files) that become the port's regression oracle.

---

## 2. Current renderer inventory

### 2.1 Frame topology

One `RenderingSystem` (~3,900 lines) renders **all** viewports on a dedicated offscreen engine
`QOpenGLContext` + `QOffscreenSurface` (`RenderingSystem.cpp:262-275`), shared with Qt's per-widget contexts via
`Qt::AA_ShareOpenGLContexts` (`src/main.cpp:86`). `ViewportWidget::paintGL` only *presents*: it `glWaitSync`s a
shared frame fence and blits the engine's finished `finalColorTexture` into the widget's default framebuffer
(`RenderingSystem.cpp:2639-2690`, `ViewportWidget.cpp:384-400`). This engine/present split is the single most
Vulkan-friendly fact about the codebase — it maps almost 1:1 onto "engine records into per-viewport textures on a
shared device; the widget presents them."

Per frame (`RenderingSystem.cpp:2499-2592`), stage-major so each stage sits in one `GL_TIME_ELAPSED` query:

```
GPU sim compute (fluid PBF, smoke, MPM, FEM, orb probes)
  -> geometryPass ONCE into a shared 5x RGBA16F MRT G-buffer + D32F,
     using the FIRST viewport's camera (cpp:2804, 2841)   <-- documented quirk, one camera per instance
  -> per-viewport lightingPass (RGBA16F finalFBO)
  -> per-viewport postProcessingPass (SelectionGlow, ping-pong PP pair)
  -> per-viewport overlayPass: depth blit, inline skybox/room, then 15 hard-coded passes
     (Grid, Spline, FieldVisualizer, PointCloud, CollisionDebug, SelectionHighlight, Mpm,
      FemViz, FluidSurface, Glass, Smoke, Tonemap(ACES), GhostRobot, JointAxis, Gizmo, SnapOverlay)
  -> on-demand RG32UI pick pass, double-PBO async readback, 1-frame-latent spiral resolve
  -> optional HIL camera glReadPixels @30Hz
  -> GLsync fence + glFlush; widgets blit on their own schedule
```

Three `RenderingSystem` instances exist (MainWindow, RobotViewport, 3× PreviewViewport) sharing baked IBL textures
across the GL share group via `adoptEnvironmentFrom()` — a workaround for a second equirect bake rendering black
(`RenderingSystem.hpp:304-318`, `RobotViewport.cpp:33-40`).

### 2.2 Shader census

| Stage | Files | Notes |
|---|---|---|
| Vertex | 42 | 9 use `gl_VertexID` (attributeless / SSBO vertex-pulling); 7 write `gl_PointSize` |
| Fragment | 68 | 8 read `gl_PointCoord`; zero fragment-shader SSBOs (grep hits are comments) |
| Compute | 30 | local sizes 256 / 64 / 8³; **zero** shared memory, `barrier()`, or float atomics |
| Geometry | 3 | 2 live (`glow_line_geom`, `cap_geom`, SplinePass only); 1 orphaned (`instanced_arrow_outline_geom`) |
| Tess ctrl/eval | 2+2 | 1 live triangle-domain pair (`gbuffer_tess_*`); isoline pair (`spline_tesc/tese`) orphaned — `spline_tese.glsl:3` even misspells the domain keyword ("isoline"), so it never compiled |

`#version` split: 104 @430, 25 @450, 13 @330, 5 @410. **Zero** `#extension` directives. All 20 atomic sites are
32-bit int/uint SSBO `atomicAdd`/`atomicMax`/`atomicExchange` with fixed-point encoding (`mpm_p2g_comp.glsl:4`:
"GL 4.3 has no float atomics" — fixed-point was the design from day one), plus exactly one `imageAtomicAdd` on an
r32ui image (`fluid_caustics_comp.glsl:48`). Uniforms: 529 loose default-block declarations in 117 files, set via
per-call `glGetUniformLocation` (`Shader.cpp:96-139`, ~548 `set*` call sites in 43 files). No UBOs in the pass
path. Roughly 86% of the shader set translates mechanically; ~12 files need hand edits (coordinate conventions);
8 files need rewrite or deletion (geometry shaders, isolines).

### 2.3 The six hazard areas

1. **Geometry shaders** (no Metal support, period): `glow_line_geom` + `cap_geom` registered at
   `RenderingSystem.cpp:439-440`, consumed only by `SplinePass.cpp` (lines 105, 128). Both emit 4-vertex
   screen-space quads — trivially re-expressible as instanced quads with the same NDC math in the vertex shader.
2. **Multi-context share-group architecture**: offscreen engine context + per-widget contexts + cross-context
   `GLsync` + `adoptEnvironmentFrom` texture borrowing (`cpp:2584-2586, 2652, hpp:304-318`). Vulkan/Metal have no
   contexts; this becomes one device (per top-level window — see §3.3) with explicit resource handles.
3. **Loose-uniform interface**: every one of ~148 programs uses string-named default-block uniforms, including
   runtime-composed names (`"areaLights[3].p0"`, `LightingPass.cpp:57-133`; `u_sdf[i]`, `FluidSystem.cpp:653-661`).
   Illegal in Vulkan GLSL; the single largest breadth item.
4. **Implicit GL state machine**: order-dependent per-pass state with known landmines — GizmoPass clears depth
   mid-frame (`GizmoPass.cpp:27`), FluidSurfacePass flips to `GL_GREATER` + additive blend
   (`FluidSurfacePass.cpp:225-249`), SelectionGlowPass uses raw non-wrapper `glPolygonOffset` calls
   (`SelectionGlowPass.cpp:112-126`). Audited totals: 12 `glBlendFunc`, 26 `glDepthFunc`, 50 `glDepthMask`,
   10 `glPolygonOffset`, **zero** `glLineWidth`, **zero** stencil — the state surface is finite and becomes
   explicit pipeline objects (well under ~300 PSOs).
5. **Same-frame GPU→CPU sync loops**: fluid compaction live count read back and used to size same-frame
   dispatches (`FluidSystem.cpp:489-493`), rigid-coupling impulses fed to the physics solver same-frame
   (`:796-813`), foam maxima EMA (`:776-782`), plus full-buffer readbacks up to 46 MB (MPM autoCalibrate,
   `MpmSystem.cpp:1033-1037`). Render passes consume sim SSBOs directly (`FluidSurfacePass.cpp:137-138`), so sim
   and rendering must port in the same stream — no GL/VK buffer interop exists on macOS.
6. **Headless gate dependency on GL bootstrap**: ~40 `KRS_*` selftests dispatch from inside
   `initializeSharedResources` (`RenderingSystem.cpp:818-2390`), each ending in `std::_Exit`, reachable **only**
   after a real `QOpenGLWidget` initializes (`ViewportWidget.cpp:360-363` → `onViewportAdded`). Today's Linux CI
   is explicitly a link/start smoke with no GPU (`.github/workflows/linux.yml:97-107`) — there is currently no
   headless gate harness on *any* backend.

---

## 3. Chosen architecture

**KRStudio QRhi Parallel Renderer Port** — as selected unanimously by a three-judge panel (scores 45/46/44 vs
34–41 for the two alternatives in §4), with the panel's grafts and every red-team finding folded in below.

### 3.1 The seam (Phase 1, prerequisite for everything)

No bespoke VkDevice/VMA/barrier layer. QRhi provides device/queue/swapchain management, pipeline and
`QRhiShaderResourceBindings` (descriptor) objects, and automatic resource-state tracking with implicit barriers —
erasing the hand-written-sync bug class that all eight subsystem maps flag (37 coarse `glMemoryBarrier` sites, the
~10-sub-pass SSF choreography, smoke ping-pong layouts). The app-level seam stays thin:

- **IRenderPass / RenderFrameContext v2**: `QRhiCommandBuffer*` instead of `QOpenGLFunctions_4_3_Core*`;
  camera/view/projection **by value** (killing the documented dangling-reference bug class at
  `RenderingSystem.cpp:2846/2895/3027`); declared render-target reads/writes; explicit per-draw pipeline state
  replacing the audited ad-hoc state machine.
- **Sealed resource layer first**: `Texture2D`'s public `GLuint _id`/`GLenum` members (`Texture2D.hpp:91-95`),
  public `Shader::ID`, and `GLuint` in 24 public headers become opaque handles with backend-neutral
  Format/Wrap/Filter enums, implemented by the existing GL code (behavior unchanged) and by QRhi. A **raw-handle
  CI lint** fails the build on any new public `GLuint`/`GLenum` in headers or new raw `gl->` sites outside the
  sealed GL backend (graft from the Direct-Vulkan plan) — the ~2,000-site port surface only shrinks.
- **Per-frame upload phase** via `QRhiResourceUpdateBatch` replaces mid-draw lazy creation
  (`getOrCreateMeshBuffers`, pick TBOs); QRhi's deferred-release semantics replace
  delete-from-whatever-context-is-current destructors (`Texture2D.cpp:50-58`).
- **Escape hatch, documented**: `QRhi::nativeHandles()` gives raw VkImage/MTLTexture/VkDevice access per pass.
  Pre-designed uses: `vkCmdDispatchIndirect` / Metal `dispatchThreadgroups(indirectBuffer:)` for the fluid
  compaction live count, indirect draw for the field visualizer, and a timeline-semaphore staging ring if QRhi's
  frame-granular readbacks jitter the HIL 30 Hz publish. Each is a swap, not a redesign, because the seam and the
  sim→render buffer contract (named streams, explicit hand-off points) are specified in P1.

Qt-coupling caveat, stated plainly: QRhi ships under a *limited compatibility promise* (source-level changes
allowed between Qt minors), and `qrhi.h` lives in versioned semi-private include dirs (the backend module links
`Qt6::GuiPrivate`). Mitigation: Qt pinned by the vcpkg baseline; every QRhi include confined to one backend
module; the P1 sealing work is the insurance policy — a future raw-Vulkan/Metal backend is a backend swap.

### 3.2 Shader strategy

Single-source Vulkan-flavored GLSL compiled by **qsb** (qtshadertools — **must be added to `vcpkg.json`**, it is
absent today) via `qt_add_shaders`: glslang → SPIR-V, SPIRV-Cross → MSL for Metal (+ optional HLSL, + GLSL 430
enabling a QRhi-on-OpenGL debug backend from the same sources — a one-enum triage lever for "shader bug vs driver
bug vs port bug").

- **529 loose uniforms → std140 blocks.** QRhi has *no push constants* (verified against `qrhi.h`), so the
  convention is one per-frame dynamic-offset uniform-buffer ring with per-draw slices. A reflection shim built on
  `QShaderDescription` keeps the ~548 `Shader::set*` call sites source-compatible, mapping name → (binding,
  offset) and flushing at draw. Red-team correction folded in: the shim is **two routed paths** — UBO-member
  names go to the ring; the ~106 `setInt(name, unit)` sampler-unit sites (plus 127 `glActiveTexture`/`glBindTexture`
  sites) route to a `QRhiShaderResourceBindings` cache keyed by the bound (texture, sampler) tuple, with a defined
  eviction policy. The resolver must flatten array/struct-member names (`u_sdfInvModel[3]`,
  `areaLights[2].p0` — six `std::to_string` builders in `LightingPass.cpp`) and hard-error on names absent from
  reflection so dead `set*` calls surface at first use.
- **Coordinate conventions.** Red-team widened the audit from "~12 NDC-z files" to a **three-class audit**:
  (a) depth range — 15 files with `*2.0-1.0` remaps, incl. `gl_FragDepth` writes in the SSF chain;
  (b) `gl_FragCoord` Y origin — 4 files deriving NDC from it, plus cross-pass render-target sampling per
  `QRhi::isYUpInFramebuffer`; (c) point-coord orientation — `fluid_ssf_depth_frag.glsl:39` hardcodes `-uv.y`, and
  `gl_PointSize`/`gl_PointCoord` appears in 15 files, beyond the set P7 converts to quads. Additionally,
  **backend-baked depth constants are banned**: `fluid_ssf_composite_frag.glsl` extracts projection-matrix
  elements directly (`u_projection[3][2]/(ndcZ+u_projection[2][2])`) — these break differently on the corrected
  (Vulkan/Metal) vs identity (GL debug) backends, so linear-depth reconstruction moves to explicit per-backend
  (zA, zB) uniforms supplied by the frame context. One named image-diff test per audited file (graft).
- **Rewrites (unavoidable on Metal under any plan):** the 2 live geometry shaders become an instanced-quad
  `PolylineRenderer` (segment endpoints per-instance, corner by `gl_VertexIndex`, identical clip-space offset math
  moved into the VS — red-team verified the math is symmetric and NDC-z-free, so it is convention-safe).
- **Deletions, enforced:** `instanced_arrow_outline_*` trio, `spline_tesc/tese` isoline pair, 4 dead gbuffer
  files — all grep-verified unreferenced. A shader manifest makes any unreferenced file a build warning and
  re-entry of retired files a build **error** (graft). Filename-suffix stage inference with its silent
  default-to-fragment (`Shader.cpp:15-24`) is replaced by explicit stage manifests with hard errors.
- **Tessellation** (red-team verdict: *complicates* — two plan corrections). The live triangle-domain
  `gbuffer_tessellated_triplanar` pipeline uses QRhi's tessellation stages (present in the 6.8.3 header; Qt maps
  them to Metal's compute-based tessellation). Corrections: (1) the plan previously claimed an "existing
  GBufferShaderSelect fallback" — **there is none**; `selectGBufferShaderKind` (`GBufferShaderSelect.hpp:28-44`)
  takes no capability input. P1 adds a capability parameter (Tessellated→Triplanar/UV downgrade, gated on
  `QRhi::isFeatureSupported(Tessellation)`), updates both consumers (`OpaquePass.cpp:193`,
  `AppliedTextureGate.cpp:122`) and extends the gate to assert both capability values. (2) The qsb recipe must
  include the Metal tessellation options (`--msltess`, tess-mode triangles, tess-vertex-count 3) or the tesc/tese
  pair silently emits no valid MSL. The P0 pilot exercises an **indexed** patch draw (the GL path is
  `glDrawElements(GL_PATCHES)`, `OpaquePass.cpp:301-303`) with real heightmap sampling, on Metal *and* lavapipe.
- **Live bug resolved, not ported:** `GBufferShaderKind::Tessellated` requests a `"gbuffer_tessellated"` program
  that was never registered (`OpaquePass.cpp:157` vs `RenderingSystem.cpp:378-395`), silently hiding meshes — and
  it is user-reachable via the displacement dropdown (`ObjectPropertiesWidget.cpp:389-391`). Decide in writing
  before the OpaquePass permutation table: register a real program or collapse the enum value. The image-diff
  harness must not treat "mesh invisible" as the parity reference.
- **Hot-reload nuance (red-team):** no runtime shader reload exists today, but the dev loop is
  edit-`.glsl`-and-restart with **no C++ rebuild**. Preserved by (a) an isolated, dependency-light qsb CMake
  target, and (b) a dev-only `QShaderBaker` path reading the `.glsl` tree directly at startup. Shipping builds use
  precompiled `.qsb` packs only.
- **PSO policy (red-team):** concrete budget from the audited state surface (~90–96 programs, 6 G-buffer kinds,
  state variants bounded by the counted blend/depth/bias sites → well under 300 PSOs); startup warm-build of the
  deferred-spine + overlay set; `QRhi::EnablePipelineCacheDataSave` with `pipelineCacheData()` persisted to disk
  (API verified in 6.8.3); pipeline-creation latency measured in P0 on lavapipe and Metal.

### 3.3 Qt integration and the WSI reality check

`QRhiWidget` (Qt 6.7+, confirmed installed) preserves the four things a native `QVulkanWindow`/window-container
would destroy: backing-store composition under ads docking, child-QWidget overlays (stats QLabel, MeasureHud,
ConstraintIconOverlay, RobotViewport slider), `QWidget::grab()` (~15–20 selftest/screenshot sites in
`MainWindow.cpp` that would otherwise go black — the private header's `grabFramebuffer()` override proves grab
works), and per-widget backing-texture resize (no per-widget swapchain to churn during dock drags).

Red-team verdict on WSI: **complicates** — the plan's original "one shared QRhi for everything" claim was wrong
and is corrected as follows. A QRhiWidget's QRhi belongs to its **top-level window's** backing store, and KRStudio
is a multi-top-level-window app: ads floating containers (`MainWindow.cpp:2751-2767`), the floatable Robot View
dock, `RobotEnrichmentDialog` (a QDialog hosting a PreviewViewport, `RobotEnrichmentDialog.cpp:48`), and the
top-level test viewport (`MainWindow.cpp:3379-3386`). On Vulkan each window's QRhi is a distinct VkDevice with no
cross-QRhi resource sharing. Folded-in changes:

- **Device arenas**: resource caches re-key from `QOpenGLContext*` to `QRhi*`. `QRhiWidget::initialize()` detects
  `rhi() != lastRhi` (exactly what happens on float/redock), re-registers the viewport, and drops stale
  presentation resources via `QRhi::registerCleanupCallback` — replacing the survivor-context hack
  (`RenderingSystem.cpp:3185-3243`), not merely deleting it.
- **v1 floating policy, in writing**: extend the *already-shipped* behavior — `updateViewportLayouts()`
  (`MainWindow.cpp:3389-3408`) hides floated main-scene viewports today — to Robot View on the QRhi path (or clear
  `DockWidgetFloatable`). True multi-device rendering of floated viewports is a named post-parity follow-up, not
  an assumed freebie.
- **IBL per window**: shared `QRhiTexture*` replaces `adoptEnvironmentFrom` only while consumers share a window;
  other windows re-bake or copy. P0 verifies the "second bake renders black" GL bug does not reproduce under QRhi
  (it is the only reason the borrowing exists).
- **Frame-driver election**: the engine loop moves from `frameSwapped` (hardwired to viewport1,
  `MainWindow.cpp:1873` — precisely the dock that gets hidden on float) to `QRhiWidget::frameSubmitted` with an
  explicit rule: re-elect the first visible QRhiWidget as driver on visibility changes, 33 ms fallback timer
  escalating when nobody presents; carried-deadline fps cap kept; the documented 1-and-2-steps-per-frame judder
  scenario A/B-tested in P0 and again in P3. Engine recording happens inside the driving widget's `render(cb)`
  before its present pass, or in a `beginOffscreenFrame` between widget frames — stated, because the plan
  previously never said where.
- **P0 spike additions**: real ads float/redock/tab-drag on Vulkan and Metal asserting the
  `releaseResources()`/`initialize(new rhi)` sequence (including ads' drag preview, which calls `grab()`);
  parentless top-level QRhiWidget viability (else gate `onTestNewViewport`); two concurrent QRhi/VkDevice
  instances affordable on lavapipe (RobotEnrichmentDialog open while the main window renders).
- **Build plumbing**: the backend module links `Qt6::GuiPrivate` for the versioned `rhi/qrhi.h` include.

Backend choice is a boot-time selector (mirrors the restart-only vsync pattern in `main.cpp:37-57`); GL and QRhi
widgets never mix in one window; GL remains the default until the parity dashboard says otherwise.

### 3.4 Compute strategy

QRhi compute is real (`QRhiComputePipeline`, storage buffers, load/store 3D textures) and the codebase is
unusually compute-portable — red-team verdict on the atomics hazard: **handled**. The feared hazard does not
exist: zero float atomics, zero shared memory, one uint image atomic. Folded-in specifics:

- **Fixed-point int SSBO atomics port as-is** (they translate to MSL `atomic_fetch_*_explicit` mechanically).
  Census wording corrected per red-team: `atomicAdd`/`atomicMax`/`atomicExchange`, all 32-bit int/uint — the P0
  qsb pilot includes `fluid_grid_comp.glsl` (atomicExchange) and the struct-member atomic in
  `field_visualizer_comp.glsl` to confirm SPIRV-Cross emission for all three.
- **Caustics rewrite** (mandatory twice over: MoltenVK-independent, and QRhi has no integer texture formats):
  `imageAtomicAdd` on r32ui → flat uint SSBO `atomicAdd` + resolve pass. Red-team consumer-side additions:
  retype `fluid_caustics_apply_frag.glsl`'s `usampler2D texelFetch` path, preserve its exact normalization
  contract (64 counts/splat, 9-tap sum, /3 baseline, 0.4 floor) with an image-diff test, and replace the
  R32UI-FBO-clear workaround (`FluidSurfacePass.cpp:378-399`) with a buffer clear.
- **No indirect execution in QRhi** (verified): field visualizer's GPU-written indirect command becomes a fixed
  max-count `drawIndexed` with the VS reading the compacted count from the SSBO and emitting degenerates; the
  gate asserts the count via `readBackBuffer`. Fluid compaction: 1-frame-stale async count with safety margin —
  **or** the grafted `nativeHandles()` indirect-dispatch escape, prototyped in P0, with the stale-count path as
  the lavapipe/CI fallback.
- **No integer texture formats**: the RG32UI pick target becomes 2× RGBA8 MRT with bit-packed 32-bit IDs (unorm8
  round-trips bytes losslessly), `readBackTexture` preserving the 32×32 rect, 1-frame latency, and CPU spiral
  resolve unchanged; SelectionHighlight gates are the acceptance test; fragment-shader SSBO writes over the pick
  rect kept as the recorded fallback design.
- **Coupling latency is a reviewed physics change, not drift** (graft): the 1-frame impulse/foam latency ships
  behind a toggle, gates re-baselined in a dedicated commit with physics-owner sign-off, and a strict-sync mode
  (`beginOffscreenFrame`/`endOffscreenFrame` = synchronous readbacks) is the permanent deterministic reference.
- **Compute-first ordering** (graft, all three judges): a real-scale sim compute pilot — MPM P2G fixed-point
  scatter and smoke 3D storage-image ping-pong under `QRhiComputePipeline` — runs in **P0**, on lavapipe *and*
  Apple silicon, because heavy compute (200+ dispatches/frame, 14-SSBO PBF) is QRhi's least-proven surface. The
  full sim port (P6) starts as early as its dependencies allow, ahead of the long overlay tail. Per-system
  dependency graphs are written for the smoke Jacobi ping-pong and the SSF chain before porting, and QRhi's
  automatic barrier placement is audited against them under sync-validation (graft: trust but verify).
- **Apple TBDR hot spots measured in P0, not discovered in P8** (graft): the 108-atomic/particle P2G scatter
  (threadgroup pre-accumulation pre-budgeted as mitigation) and the 80-tap narrow-range SSF filter (sigma cap /
  compute variant pre-costed). MPM render path gets a **packing kernel** (graft): GPU-side compaction to a
  ~32 B/particle render stream so the raster path never touches the 46 MB solver state; same pattern applied to
  HIL publish, telemetry, and autoCalibrate (which becomes a small GPU min/max reduction).
- Metal watchdog: the 120k-substep selftest loops split across batched offscreen frames.

### 3.5 Red-team verdict summary (all six findings incorporated)

| Hazard | Verdict | What changed in this plan |
|---|---|---|
| Geometry shaders on Metal | **handled** | Nothing structural — replacement design, orphan census, and P5 costing all verified correct; build-error re-entry guard kept |
| Tessellation | **complicates** | Capability fallback *built* (not "existing"); `gbuffer_tessellated` bug resolved in writing; qsb `--msltess` options; indexed-patch + heightmap P0 pilot; isoline pair deleted with evidence recorded |
| Float/image atomics | **handled** | Census wording fixed (Add/Max/Exchange); caustics consumer-side edits itemized in P7; P0 pilot covers all three atomic ops; P2G microbenchmark kept |
| Qt multi-viewport WSI | **complicates** | Per-top-level-window QRhi devices acknowledged; device arenas replace (not retire) perContext machinery; v1 floating policy; per-window IBL; frame-driver election rule; GuiPrivate link; expanded P0 spike |
| GL→Vulkan semantic gaps | **complicates** | Audit widened to 3 convention classes / 15 files; projection-element extraction banned; shim split into UBO + SRB paths with name flattening and hard errors; concrete PSO budget + persisted cache; dev-loop preservation; gate readback Y/half-float normalization |
| CI + parity on lavapipe | **complicates** | Perf-gate SKIP mode + distinct exit code (77); **two** headless bootstraps (surface-free QRhi *and* QOffscreenSurface+llvmpipe GL) budgeted; image-diff utility is new code (FidelityScorecard has none); Mesa-vs-Mesa pinning; ICD preflight; explicit CI gate subset with per-gate timeouts; vcpkg cache-key sequencing |

---

## 4. Alternatives considered

### 4.1 Direct-Vulkan parallel backend ("KRS-RHI/VK") — 48 weeks. Not chosen.

Port to a hand-written Vulkan 1.2 backend behind a bespoke thin RHI: VkDevice/VMA/timeline semaphores, precise
barriers, push constants, `vkCmdDispatchIndirect`, macOS via MoltenVK. It is the most complete engineering
document of the three and has the highest performance ceiling, and its parallel-port structure (RHI extracted from
existing shapes, dual-backend gate dashboard, raw-handle lint) is excellent — several of its disciplines are
grafted into this plan. Why not: (1) macOS — the stated prize — arrives at ~week 39–44 of 48 *through the full
MoltenVK translation stack* (compute-emulated tessellation, emulated timeline semaphores, portability subset,
watchdog quirks); (2) its present path assumed Qt-with-Vulkan, and the installed Qt has `QT_FEATURE_vulkan = -1` —
on macOS a MoltenVK-linked Qt is the rare, undertested configuration, making its bespoke
`VK_EXT_metal_objects`/IOSurface fallback more likely than the plan admitted; (3) one engineer owns
device/barrier/descriptor plumbing and MoltenVK quirk knowledge forever — the classic bus-factor hazard; (4) its
honest 48-week estimate prices in ~35–40% undifferentiated plumbing that QRhi ships already-hardened. The
performance it buys (indirect execution, push constants, hand-tuned barriers) is mostly blunted by the sim
chains' serial dependencies, and on the platform that motivated the port, translated Vulkan likely *under*performs
native Metal.

### 4.2 Vulkan Strangler port — 42 weeks (38 without the alpha). Not chosen.

Compute-first strangler: port the four sim systems to Vulkan compute behind a SimRenderFeed seam with VK/GL
external-memory interop (CPU bridge elsewhere), optionally ship an early macOS alpha on a GL 4.1 graphics profile,
then grow an RHI and port graphics in waves. Its census insight is the sharpest of the three and fully verified:
the GL 4.3 hard dependency is the 30 compute shaders plus exactly 5 SSBO vertex-pulling shaders (zero fragment
SSBOs — apparent grep hits are comments), so compute-first attacks 100% of the macOS blocker with ~20% of the
code. That ordering insight is grafted into this plan. Why not the plan itself: VK/GL external-memory interop is a
vendor-driver minefield that injects Vulkan risk *into* the shipping GL path; the macOS alpha is honestly costed
at ~25–51 MB/frame of CPU-bridged sim traffic (not shippable quality); ~6 weeks of deliberately throwaway bridge
work; up to four coexisting configurations to test; and full macOS still lands last, through the same MoltenVK
stack as 4.1. Two artifacts of it are kept as documented insurance: the compute-first ordering, and the **GL 4.1
contingency census** (only 5 SSBO verts, 4 `glCopyImageSubData` sites, 1 BaseInstance draw, 1 DSA helper exceed
macOS GL 4.1 for graphics) — recorded in the P0 decision doc as a cheap sim-disabled macOS demo fallback, never
scheduled.

---

## 5. Phased roadmap

Effort in engineer-days (5/week). Optimistic sum: 125 d (25 wk). **Planned: 160–180 d (32–36 wk)** — apply the
contingency to P3/P6/P8, which all three judges flagged as thin. Every phase ends with a **GL parity gate**: the
full existing gate battery green on the GL backend, plus the raw-handle lint clean. GL is the shipped default
throughout; renderer feature work during the port lands in both trees or not at all (enforced by the dual-backend
dashboard from P4 on).

### Phase 0 — De-risk spike + quick wins (10 d) — no dependencies

Deliverable: a written go/no-go architecture record with running evidence.
- vcpkg manifest, day one: add `qtshadertools`; add the `vulkan` feature to qtbase (+ loader dep) for
  Linux/Windows (`QT_FEATURE_vulkan = -1` today). Land in its own PR so the vcpkg cache re-seeds once (§7).
- QRhiWidget in an ads dock on Vulkan (Linux) and Metal (Mac): overlays, `grab()`, float/redock/tab-drag with
  the `releaseResources()`/`initialize(new rhi)` assertion, parentless-widget check, two-concurrent-QRhi check.
- qsb pilot on representative shaders incl. compute with atomicAdd/Max/Exchange and the struct-member atomic;
  Metal tessellation pilot: indexed patch draw, `--msltess`, heightmap sampling, also on lavapipe.
- **Compute-at-scale pilot** (graft): MPM P2G scatter + smoke 3D ping-pong on QRhiComputePipeline, lavapipe and
  Apple silicon; P2G 108-atomic microbenchmark and 80-tap SSF filter benchmark on Apple TBDR.
- RGBA8 bit-packed pick-target prototype; `nativeHandles()` indirect-dispatch prototype; frameSubmitted
  frame-pacing A/B vs the documented judder scenario; pipeline-creation latency measurement.
- lavapipe + sync-validation running in CI from week 1 (graft) — even if only against the pilots.
- Gap register recorded: no indirect execution, no integer formats, no push constants, GS/isoline absence,
  per-window QRhi devices, GL 4.1 contingency census.
- Quick wins landable immediately: shader census tooling (manifest + unreferenced-file warning), orphan deletions
  with re-entry build errors, `particle_render_vert` point-size clamp, the PP-FBO depth-texture leak fix
  (`RenderingSystem.cpp:3826-3836`) on the GL path.

### Phase 1 — Seam + resource sealing (15 d) — depends: P0

Sealed resource API (opaque handles, enums, `GLuint` purged from 24 headers); IRenderPass/RenderFrameContext v2;
per-pass GL state audit captured as pipeline-state declarations; boot-time backend selector; raw-handle CI lint;
device-arena design (QRhi*-keyed caches) and v1 floating policy in writing; sim→render buffer contract specified;
`GBufferShaderSelect` capability parameter + gate update; `gbuffer_tessellated` decision recorded;
`Qt6::GuiPrivate` link confined to the backend module.
**Parity gate**: every existing gate passes on GL through the sealed API, zero behavior change.

### Phase 2 — qsb toolchain + uniform shim (15 d) — depends: P1

`qt_add_shaders` integration (SPIR-V + MSL + HLSL + GLSL debug output, tess options); first ~40 shaders converted
(G-buffer family, lighting, post, fullscreen); the two-path reflection shim (UBO ring + SRB cache, name
flattening, hard errors); pipeline + SRB caches with the PSO budget, warm-build list, and persisted
`pipelineCacheData()`; stage manifests replacing suffix inference; dev-loop preservation (fast qsb target +
dev-only QShaderBaker path); orphan cull finalized.
**Parity gate**: GL untouched; qsb compile-all green in CI for converted shaders.

### Phase 3 — Core deferred spine on QRhi (15 d, thin — hold contingency here) — depends: P2

G-buffer (5× RGBA16F + D32F, once-per-frame first-camera constraint documented) → per-viewport lighting (single
std140 lights UBO) → tonemap → QRhiWidget present with SSAA downsample; frame-driver election + judder
re-validation; OpaquePass permutation table (with the capability fallback); IBL bake chain offscreen
(RGB16F→RGBA16F promotion, per-face/mip views, second-bake-black verification); LTC RGBA32F upload; skybox/room
as a real pass; QRhi timestamps wired to Diagnostics labels; per-draw UBO-ring profiling on real scenes.
**Parity gate**: GL default unchanged; deferred spine visually compared by eye + first diff scenes.

### Phase 4 — Headless gates + CI GPU coverage (10 d) — depends: P3

**Two** headless bootstraps (red-team): a `main()`-level entry point creating (a) surface-free QRhi and (b)
QOffscreenSurface + GL context, so gates no longer require a live QOpenGLWidget; readback-normalization layer
(half-float decode, Y-flip policy, pick-rect coordinates); perf-gate software-GPU SKIP mode + exit code 77
(`LiveSdfGate.cpp:69,91`, `LiveTrackGate.cpp:151`); the **new** image/buffer diff utility (FidelityScorecard has
no image comparison); G1–G9, AppliedTextureGate, FanucRenderGate, irradiance selftest green on QRhi-Vulkan;
lavapipe ICD preflight + pinned sync-validation in `linux.yml`; enumerated CI gate subset with per-gate timeouts;
`KRS_BACKEND` switch in the overnight bench — dual-backend dashboard live from here to the end.
**Parity gate**: both backends on one dashboard; Mesa-vs-Mesa (llvmpipe GL vs lavapipe VK) pinned for CI diffs.

### Phase 5 — Overlays, GS replacement, picking (15 d) — depends: P3, P4

Per-frame-in-flight vertex ring (replaces `glBufferData` orphaning); triangle-fan→list emitter (2 sites — Metal
has no fans); instanced-quad PolylineRenderer replacing glow_line/cap GS with image-diff parity; Grid, Spline,
CollisionDebug, SelectionHighlight, SnapOverlay (batched), GhostRobot, JointAxis, Gizmo (clear-attachments
strategy for the mid-frame depth clear), SelectionGlow graph, PointCloud (RGB→RGBA8); pick pass on 2× RGBA8
bit-packed MRT preserving the 1-frame spiral-resolve contract; SelectionHighlight/Indicator/FieldVisualizer gates
green on QRhi.
**Parity gate**: deterministic-overlay image diffs (grid, wireframes, spline glow) within tolerance.

### Phase 6 — GPU sim compute port (20 d, thin — hold contingency; start as early as P4 allows per the
compute-first graft) — depends: P4

All 30 compute shaders + 4 systems: FluidSystem PBF (14 buffers; live-count via nativeHandles indirect dispatch
or 1-frame-stale fallback; impulse/foam on the latency toggle with strict-sync gate mode; SDF sampler-array UBO
with R32F-filtering feature check), MpmSystem (substep batching under the Metal watchdog; thermal chain;
autoCalibrate → GPU reduction; render-stream packing kernel), SmokeSystem (3D storage-image ping-pong; dependency
graph audited under sync-validation), GpuSdfEdt + staged SDF uploads; field-viz max-count draw; caustics SSBO
rewrite incl. consumer edits; physics re-baselining commit with sign-off.
**Parity gate**: fluid/MPM/thermal/repose/EDT/field-viz gates green on QRhi-Vulkan; conservation harness runs on
QRhi to catch fixed-point drift.

### Phase 7 — Fluid surface + advanced visuals (12.5 d) — depends: P5, P6

SSF chain: point sprites → instanced camera-facing quads (uniform Y/UV convention, edge-clip fix); narrow-range
smooth ping-pong; GREATER-depth back pass; half-res thickness; world-anchored foam with dummy-texture bindings (no
texture-0 fallback in QRhi); caustics apply; composite with explicit scene copies; whitewater; GlassPass; smoke
raymarch; MPM/FEM viz; the full 3-class coordinate audit closed across all 15 flagged files with one named
image-diff test each; `KRS_SSF_DEBUG` probe on staged readback.
**Parity gate**: SSF/glass/smoke diff scenes within tolerance on both backends.

### Phase 8 — macOS bring-up + parity closure (12.5 d, thin — hold contingency) — depends: P7

Native-Metal validation on Apple silicon: tessellation path + fallback decision, sprite behavior, buffer atomics
under MPM load, format matrix, Retina DPR sizing, HIL cadence; arm64 app-bundle packaging; macOS CI runner
running the full gate suite on Metal; TBDR perf pass with concrete targets (G-buffer format slimming/packing —
QRhi has no subpasses/memoryless — half-res thickness/foam, UBO-ring churn); final GL-vs-QRhi scorecard;
default-flip criteria (macOS ships QRhi-Metal; Linux/Windows flip to QRhi-Vulkan on sustained dashboard parity).
**Parity gate**: §8 definition of done.

---

## 6. Risk register (top 10)

| # | Risk | Mitigation | Early-warning signal |
|---|---|---|---|
| 1 | QRhi compute inadequate at sim scale (200+ dispatches/frame, implicit barrier tracker) — the plan's least-proven surface | P0 compute-at-scale pilot on lavapipe + Apple silicon; designed escape: sim systems on raw Vulkan/Metal compute behind the P1 SimRenderFeed seam via `nativeHandles()` | P0 pilot frame times or sync-validation churn; barrier-audit mismatches vs the hand-written dependency graphs |
| 2 | Per-top-level-window QRhi devices break floating/dialog viewports (no cross-device sharing on Vulkan) | Device arenas keyed by `QRhi*`; v1 float-hides-viewport policy (already shipped behavior); per-window IBL story; P0 float/redock spike | `initialize()` observing a new `rhi()` in flows the spike didn't cover; leaks on dock close in the arena accounting |
| 3 | Qt roadmap coupling: QRhi limited-compat API, semi-private headers, qsb/QRhiWidget evolve with Qt Quick's needs | vcpkg baseline pin; all QRhi includes in one module; sealed seam + nativeHandles makes raw-backend exit a swap; P1 sealing is the insurance | Deprecation warnings or source breaks on any deliberate Qt bump; upstream QRhi changelog watch |
| 4 | Apple TBDR perf cliffs: 108-atomic P2G scatter, 80-tap SSF filter, 40 B/px G-buffer | P0 microbenchmarks with pre-budgeted mitigations (threadgroup pre-accumulation, sigma cap/compute filter, format slimming in P8) | P0 benchmark numbers vs desktop baseline; Metal frame captures showing atomic serialization |
| 5 | 1-frame coupling latency changes physics behavior (impulses, foam, compaction) | Reviewed redesign with sign-off; toggle + strict-sync deterministic reference; gates re-baselined in a dedicated commit; nativeHandles indirect-dispatch removes the compaction case entirely | FluidRigidGate/repose/thermal deltas beyond re-baselined tolerances; visible sloshing/energy drift in the conservation harness |
| 6 | Frame-pacing judder regression on frameSubmitted (documented historical failure mode) | Driver-election rule; fallback timer; A/B in P0 and P3; decoupled engine-tick fallback design ready | The 1-and-2-steps-per-frame signature in the P0/P3 A/B traces |
| 7 | Reflection-shim overhead (548 set* sites through one dynamic-UBO ring; SRB cache churn) | Profile in P3 on real scenes; migrate hottest passes (OpaquePass materials, SSF loop) to packed UBO structs — the shim is a bridge, not the end state | P3 CPU frame-time profile; SRB cache miss/eviction counters |
| 8 | Coordinate-convention bugs slipping through (15 files, 3 classes, plus readback orientation) | Ban on projection-element extraction; explicit (zA,zB) uniforms; per-file named image-diff tests; gate-layer Y/half-float normalization | Any single-backend-only diff failure; depth-compare artifacts in SSF/glass scenes on exactly one backend |
| 9 | CI software-Vulkan gaps and runtimes (perf gates, multi-minute MPM substeps, disk-heavy Qt rebuild) | SKIP-not-FAIL for wall-clock asserts (exit 77); reduced iteration counts on lavapipe with full counts on the Metal runner; ICD preflight step; manifest change landed solo to re-seed the vcpkg cache | Preflight failures; gate timeouts; cold-cache disk exhaustion on the first post-manifest run |
| 10 | Parallel-port drift: two shader trees, one engineer, ~8 months | qsb GLSL output can feed a QRhi-GL backend making the Vulkan-GLSL tree the single source; feature freeze or land-in-both rule; dual-backend dashboard from P4 catches drift mechanically | Dashboard divergence on scenes untouched by the current phase; GL-only shader edits appearing in review |

---

## 7. CI/CD integration

Rides the harness documented in `docs/CI.md`. Changes, in dependency order:

1. **vcpkg manifest** (P0, own PR): add `qtshadertools` (qsb); add the `vulkan` feature to qtbase plus the
   Vulkan loader dependency for Linux/Windows. Both are absent today and the installed Qt has
   `QT_FEATURE_vulkan = -1`. This changes qtbase's ABI hash and **invalidates the vcpkg binary cache key**
   (`hashFiles('vcpkg.json')`) — on a runner that already needed a disk-eviction step, so: land solo, let the
   cache re-seed once, verify disk headroom on the cold run, before any gate job depends on it. No
   vulkan-headers/VMA/glslang/spirv-cross entries needed — QRhi and qsb carry all of that; this is the dependency
   argument for the architecture in miniature. macOS needs nothing: the Metal backend builds unconditionally.
2. **Shader-compile CI step** (P2): qsb compile-all (SPIR-V + MSL + GLSL targets) on every PR — catches shader
   breakage in seconds without a GPU, on all backends' output formats at once. Manifest check: unreferenced
   shader file = warning, retired file re-entry = error.
3. **lavapipe gates on Linux** (P0 pilot, P4 full): install `mesa-vulkan-drivers` + `libvulkan1`/`vulkan-tools`
   on ubuntu-22.04; hard preflight asserting lavapipe enumerates with apiVersion ≥ 1.3,
   `shaderStorageImageExtendedFormats`, `fragmentStoresAndAtomics`, `vertexPipelineStoresAndAtomics` — fail loudly
   rather than half-run. Sync-validation layers from a pinned LunarG SDK (the ubuntu-22.04 distro package is
   1.3.204-era, predating the major sync-validation fixes). Both halves of parity diffs pinned to Mesa
   (llvmpipe GL 4.5 via `LIBGL_ALWAYS_SOFTWARE=1` vs lavapipe Vulkan — shared rasterizer core, near-zero noise).
4. **Headless entry point** (P4): gates run from `main()` before any widget exists — surface-free QRhi for the
   QRhi suite, QOffscreenSurface+llvmpipe for the GL suite. This is the first time *any* render gate runs in CI
   (today's linux.yml treats headless app start as expected-to-fail).
5. **Gate subset per PR**, not `KRS_OVERNIGHT_BENCH` (~100 gates incl. MQTT and STEP parsing): enumerated
   render/compute `KRS_*` steps with per-gate timeouts sized for software rasterization (MPM selftests run up to
   6,000 GPU substeps per invocation, `MpmSystem.cpp:558` — expect minutes). Perf-asserting gates SKIP (exit 77)
   on `VK_PHYSICAL_DEVICE_TYPE_CPU` / `KRS_SOFTWARE_GPU=1`.
6. **macOS job** (P8): the existing `ci-mac` runner gains the full gate suite on **native Metal** — no MoltenVK
   anywhere in CI or production. Per `docs/CI.md`'s own honest boundary, software-rasterized CI green still does
   not equal "runs well on hardware": operator verification on a real Apple-silicon machine remains part of §8.
7. **Overnight dual-backend dashboard** (P4→end): `KRS_BACKEND` switch runs both backends on one PASS/FAIL
   dashboard; it is the authoritative parity signal and the default-flip criterion.

---

## 8. Definition of done — "macOS unblocked"

All of the following, no exceptions:

1. KRStudio builds as a signed arm64 macOS app bundle from `ci-mac`, renderer on QRhi-Metal, no MoltenVK, no
   Vulkan SDK on the user machine.
2. The full enumerated gate suite (render gates G1–G9, applied-texture, FANUC, irradiance PI·L,
   selection/indicator, field-visualizer, fluid column, MPM selftests/thermal/repose, EDT) passes on the macOS
   Metal CI runner, with zero SKIPs attributable to missing features (SKIPs for software-GPU timing asserts are
   Linux-only by construction).
3. GL-vs-QRhi image-diff scorecard within recorded tolerances on all deterministic scenes, including one named
   diff test per coordinate-audited shader file, and conservation-harness parity on fluids/MPM after the
   re-baselined coupling-latency commit.
4. Interactive acceptance on real Apple-silicon hardware (OPERATOR VERIFICATION, per `docs/CI.md` convention):
   multi-viewport docking incl. float/redock under the v1 policy, overlays and `grab()` screenshots, picking and
   mate-selector hover, fluid/smoke/MPM playback at interactive rates at Retina resolution, robot preview dialog.
5. GL backend still default and green on Linux/Windows (parallel-port property held to the last day); the
   Linux/Windows QRhi-Vulkan flip is a separate, dashboard-gated decision.
6. The decision record contains: the P0 gap register, the GL 4.1 contingency census, the tessellation fallback
   decision, the `gbuffer_tessellated` resolution, the physics re-baselining sign-off, and the per-window
   device/floating policy — so the next engineer inherits reasons, not just code.
