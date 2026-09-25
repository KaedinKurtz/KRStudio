# KRS Graphics — Implementation Plan of Action (GL → Vulkan changeover)

Plan of record for building the direct-Vulkan KRS Graphics library and moving KRStudio's renderer
onto it. Companion docs: `ARCHITECTURE.md` (structure/contracts), `CI_PIPELINE.md` (the gates that
define "done"). Evidence base: `docs/VULKAN_PORT_PLAN.md` §2 (verified inventory: 147 shaders,
~2,000 GL call sites, 529 loose uniforms, 37 barrier sites, the six hazard areas) and §3.5
(red-team verdicts) — all of it carries over; only the implementation layer changed (direct Vulkan
instead of QRhi, per ADR-001).

Execution model: the agent (Claude) is the primary engineer. **Every work package (WP) opens by
landing its tests and closes only when its exit gate is green in graphics-ci** — the test-first
rule is structural, not aspirational. Effort is engineer-days for one senior graphics engineer;
agent-days compress wall-clock but not the verification path. The optimistic sum is ~200 d; **plan
around 240–260 d** (the direct-Vulkan premium the judges priced against QRhi buys the Qt-free
library the owner requires). GL ships as the default renderer until WP11's dashboard flips it.

What direct Vulkan gives back vs the QRhi plan (use them): **push constants** (per-draw transforms
stop needing a UBO ring slice), **indirect dispatch/draw** (`vkCmdDispatchIndirect` erases the
fluid live-count same-frame readback; the field visualizer keeps its GPU-written draw args),
**integer texture formats** (the RG32UI pick target ports as-is; no RGBA8 bit-packing), and full
barrier control (the render graph emits exactly the sync the hand-written dependency graphs
specify). What it costs: we own device/memory/sync plumbing (WP1–2) and MoltenVK is in the macOS
path (portability subset handled in WP2's capability table; native-Metal L0 remains an ADR-004
escape).

---

## Milestone map

```
WP0 harness ─ WP1 RHI core ─ WP2 caps/portability ─ WP3 graph ─ WP4 shader toolchain
     └─────────────────────────────────────────────────────────────────┘
WP5 seam (app-side sealing, GL untouched)          [gates: existing GL battery green]
WP6 deferred spine ─ WP7 compute/sim ─ WP8 overlays+pick ─ WP9 SSF/advanced
WP10 Qt adapter + viewport swap ─ WP11 parity closure + default flip ─ WP12 extraction
```

Dependencies run left→right; WP5 proceeds in parallel with WP1–4 (different tree). WP7 starts as
soon as WP4 lands (compute-first graft: the sim kernels are the actual GL 4.3 blocker and the
least-proven surface — attack them early).

---

## WP0 — Verification harness before anything (done ≙ this repo state)

Objective: the leash exists before the animal. Deliverables: `engine/` skeleton (build, install,
consume-test, null backend, first unit tests), `scripts/check_engine_boundary.py`,
`engine/shaders/manifest.json` + first shader through glslang/spirv-val, and
`.github/workflows/graphics-ci.yml` running S0–S4 green on the skeleton.
**Exit gate**: graphics-ci green on all platforms; boundary lint demonstrably fails a seeded
violation (negative test committed then reverted in the same PR).

## WP1 — RHI core (L0), null-first (20 d)

Order inside the WP is test-first per module: null-backend semantics + unit tests, then the Vulkan
implementation against the same contract, then gpu-tests on lavapipe.
- Handle tables (generational), `Result`, debug sink; `Instance/Device` bring-up: volk, VMA,
  queue selection (graphics+compute; async-compute deferred by ADR), timeline-semaphore frame
  pacing (MoltenVK emulates timeline semaphores — acceptable, measured in WP2).
- Resources: buffers (device/host-visible/readback rings), images + views (full format enum
  including R32UI/RG32UI — the pick path depends on it), samplers; deletion queues keyed by frame
  fence (kills the GL "delete from whichever context is current" class, `Texture2D.cpp:50-58`).
- Pipelines: SPIR-V modules, graphics/compute PSOs, pipeline cache persisted to disk; descriptor
  allocator (per-frame linear pools) around the **fixed set convention** (§ Shader changeover).
- Command recording: one `Frame` API — begin/end, passes, draws, dispatches, copies, queries.
**Tests first**: unit (lifetime/validation, ~40 cases), gpu (triangle-to-readback, buffer
round-trip, checksum dispatch, R32UI store/load).
**Exit gate**: S0–S4 green incl. new tests; validation-clean under sync-validation.

## WP2 — Capability table & portability floor (5 d)

The single table every later WP consults: required core features (asserted at device creation,
named in the S4 ICD preflight), optional features with per-feature fallbacks (dynamic rendering vs
render passes; `shaderStorageImageExtendedFormats`; R32F filtering for SDF samplers —
`FluidSystem` needs the check the old plan flagged), and the **MoltenVK/portability column**
(triangle fans absent — WP8 already converts the 2 fan sites; tessellation via MoltenVK's
compute-emulated path measured here with an indexed patch pilot, informing WP6's
tessellation-fallback decision; image-atomic support probed for the caustics `r32ui` path with the
SSBO rewrite as the recorded fallback). Adds S5 (MoltenVK job) and the spirv-cross MSL preflight to
S1. **Exit gate**: capability doc + runtime asserts merged; S5 green on the WP1 test suite;
tessellation/atomics pilot numbers recorded in an ADR.

## WP3 — Render graph (L1) (12 d)

Passes declare attachments/reads/writes; graph compiles to layout transitions + barriers +
submission order; transient-attachment aliasing; per-pass GPU timestamps and debug labels.
The 37 `glMemoryBarrier` sites and the hand-drawn smoke/SSF dependency graphs from the port plan
become **unit-testable input**: graph tests assert the exact barrier sequence for replicas of those
chains on the null backend, then execute them on lavapipe under sync-validation.
**Exit gate**: graph unit suite (incl. smoke-ping-pong and SSF-chain replicas) green; zero
sync-validation messages on execution tests.

## WP4 — Shader toolchain + reflection (8 d)

CMake shader build (`glslangValidator → .spv`, dependency-tracked, parallel), embedded or packed
artifact loading, `spirv-reflect`-based layout extraction cross-checked against the manifest (S1
grows this check), spec-constants for compute local sizes, and the **name shim**: a reflection map
(name → set/binding/offset) that lets ported app call sites keep `set("u_view", …)` semantics
against std140 blocks during migration — with hard errors on names absent from reflection so dead
`set*` calls surface at first use (548 call sites migrate gradually; the shim is a bridge, deleted
in WP11). Dev loop: `krsg_shaderc --watch` rebuilds .spv without a C++ rebuild (preserves the
edit-and-restart workflow; no runtime GLSL compiler ships).
**Exit gate**: S1 validates stage/layout against reflection; golden triangle renders from a
manifest-built shader; shim unit tests (flattened names `areaLights[2].p0`, `u_sdf[3]`) green.

## WP5 — App-side seam sealing (GL untouched) (15 d, parallel track)

The port plan's P1, unchanged in substance, now aimed at the library boundary: opaque handles and
backend-neutral enums replace `GLuint`/`GLenum` in the 24 leaking public headers;
IRenderPass/RenderFrameContext v2 (camera **by value**, declared targets, explicit per-draw state);
mid-frame lazy creation moved to an upload phase; `GBufferShaderSelect` gains the capability
parameter (both consumers updated, gate extended); the `gbuffer_tessellated` never-registered bug
resolved **in writing** before the permutation table; boot-time `--renderer=gl|krsg` selector
stub; the sim→render buffer contract (`SimStream` named streams) specified. Every existing gate
stays green on GL through the sealed API — zero behavior change, enforced per commit.
**Exit gate**: full existing GL gate battery green; S0 raw-handle lint clean; seam ADR merged.

## WP6 — Deferred spine on the engine (18 d)

G-buffer (5×RGBA16F + D32F) → lighting (LTC RGBA32F, IBL chain offscreen with the
second-bake-black verification) → tonemap → offscreen present target, all as L2 blocks driven by
the app through the seam; OpaquePass permutation table (~90–96 programs, PSO budget <300 with
startup warm-build + persisted cache); tessellation decision executed per WP2's pilot (port the
triangle-domain pair, or capability-fallback to Triplanar/UV — recorded either way).
**Tests first**: golden scenes (empty/grid/single-mesh/PBR-sphere-grid/area-light) land as
expected-fail, flip green as blocks land. **Exit gate**: S4/S5 goldens for the spine; frame
renders headless via the library with no Qt in the process (the consume-test grows a spine scene).

## WP7 — Compute & sim port (25 d — the long pole; starts after WP4)

All 30 compute shaders and the four systems, on the engine's compute framework, solver logic
app-side (the boundary): PBF fluid (14 SSBOs; live count via **indirect dispatch** — the readback
loop dies; impulse/foam readbacks on the WP2-measured readback ring with the 1-frame-latency
toggle + strict-sync deterministic mode and the physics re-baselining ritual), MPM (fixed-point
int atomics port as-is — census verified zero float atomics; substep batching; autoCalibrate as a
GPU reduction; render-stream packing kernel so raster never touches the 46 MB state), smoke
(3D storage-image ping-pong on the graph), SDF/EDT + bakers. Caustics: probe
image-atomic support (WP2) else the SSBO+resolve rewrite with its consumer edits.
**Tests first**: per-kernel checksum tests with fixed seeds; conservation harness replicas.
**Exit gate**: kernel checksums bit-stable across two runs; sim gates (fluid column, MPM selftests,
thermal, repose, EDT) green through the engine path on lavapipe; sync-validation clean.

## WP8 — Overlays, geometry-shader replacement, picking (15 d)

Instanced-quad `PolylineRenderer` replaces the 2 live geometry shaders (math verified
convention-safe in the red-team pass); triangle-fan→list at the 2 fan sites; per-frame-in-flight
vertex rings replace `glBufferData` orphaning; Grid/Spline/CollisionDebug/SelectionHighlight/
SnapOverlay/GhostRobot/JointAxis/Gizmo (mid-frame depth clear → clear-attachment)/SelectionGlow/
PointCloud; **pick pass keeps RG32UI** + 1-frame async readback ring + spiral resolve unchanged.
**Exit gate**: overlay goldens within tolerance; pick unit test (known scene → exact entity IDs at
probe pixels) green on lavapipe + MoltenVK.

## WP9 — Screen-space fluid & advanced visuals (13 d)

SSF chain as graph passes (sprites → instanced camera-facing quads with the uniform Y/UV
convention; narrow-range smooth ping-pong; GREATER-depth back pass; half-res thickness; foam;
caustics apply; composite with explicit scene copies), glass, smoke raymarch, MPM/FEM viz — and
the **three-class coordinate audit closed**: depth-range remaps (15 files), `gl_FragCoord` Y
origin (4 files), point-coord orientation; projection-element extraction stays banned (explicit
zA/zB uniforms). One named golden per audited file.
**Exit gate**: SSF/glass/smoke goldens on both S4 and S5; all 15 audit files' named tests green.

## WP10 — Qt adapter & viewport integration (10 d)

`krsg-qt`: `SurfaceSource` from `QWindow`, a presenter widget (Vulkan-rendered texture →
swapchain blit) honoring the app's existing present/blit contract, DPR handling, resize/minimize,
docking float/redock survival (device is per-engine-instance, not per-window — the multi-window
hazard from the QRhi plan largely dissolves because **we** own the one VkDevice; the WP2 pilot
re-verifies float/redock + `grab()` + the enrichment dialog). Boot selector `--renderer=krsg`
live; per-viewport present of engine-rendered textures; HIL 30 Hz publish on the readback ring.
**Exit gate**: app runs interactively on the engine on Linux/Windows dev hardware —
**OPERATOR VERIFICATION** (docking, overlays, screenshots, picking); headless app gates green on
both `--renderer` values in CI.

## WP11 — Parity closure & default flip (12 d)

S6 scorecard fully populated (llvmpipe-GL vs lavapipe-VK image diffs + sim checksums), S7 perf
floors, macOS gate suite on MoltenVK in ci-mac, arm64 bundle packaging, name-shim deletion (call
sites land on typed UBO structs), dual-backend overnight dashboard sustained green ≥ 2 weeks →
flip default per platform (macOS ships engine-only; Linux/Windows flip is dashboard-gated;
GL path enters deprecation with a removal ADR).
**Exit gate**: `VULKAN_PORT_PLAN.md` §8 definition-of-done items 1–6, re-read against MoltenVK,
plus operator sign-off on Apple-silicon hardware.

## WP12 — Library extraction (5 d, when §1 extraction criterion holds)

`git filter-repo` split, in-repo vcpkg overlay port consumed by the app, engine CI moves with the
repo, version pinning + update ritual documented. **Exit gate**: app builds from a tagged engine
release with no source checkout of the engine.

---

## The shader changeover, mechanically (reference for every WP)

**Dialect delta applied to each converted file** (from the census: 104 @430, 25 @450, 13 @330,
5 @410; zero `#extension`s — unusually clean):
1. `#version 450` + explicit `layout(location=…)` on every stage in/out; vertex inputs get
   locations matching the fixed vertex-format registry (WP1).
2. Loose uniforms → `layout(set=S, binding=B, std140) uniform` blocks or push constants per the
   **set convention**: set 0 = per-frame (camera/view/projection/time/lights UBO), set 1 =
   per-pass, set 2 = material (textures+params), set 3 = per-draw storage (sim SSBOs);
   ≤128-byte per-draw data (model matrix + IDs) rides **push constants**. Samplers become
   `layout(set=2, binding=…) uniform sampler2D` — binding numbers assigned by the manifest, never
   ad hoc. The 529 loose uniforms map file-by-file in the conversion tracker
   (`engine/shaders/manifest.json` carries the mapping row per shader).
3. `gl_VertexID`→`gl_VertexIndex`, `gl_InstanceID`→`gl_InstanceIndex`; texture units → bindings
   (the ~106 `setInt(name, unit)` sites route through the WP4 shim to descriptor bindings).
4. **Conventions policy** (decided once, here): shaders keep GL clip-space math; the engine flips
   with a negative viewport height and uses 0..1 depth via `VK_EXT`/core depth-range defaults —
   so class-(a) files delete their `*2.0-1.0` depth remaps against explicit zA/zB uniforms,
   class-(b) files take the engine-provided `u_yFlip` constant, class-(c) point-coord files fix
   orientation at the sprite→quad rewrite (WP9). No shader may read projection-matrix elements to
   reconstruct depth (banned; enforced by an S1 grep rule).
5. Compute: `local_size` via spec constants; `glMemoryBarrier` deleted (the graph owns sync);
   fixed-point int atomics unchanged; `imageAtomicAdd` r32ui per the WP2 probe.
6. Conversion waves ride their consumer WP (spine files in WP6, sim in WP7, overlays in WP8, SSF
   in WP9); the manifest tracks state per file:
   `gl-only → converted → golden-tested → gl-retired`. ~86% mechanical, ~15 hand-audited, 8
   rewritten/deleted (2 live GS → PolylineRenderer; orphaned isoline/arrow-outline/dead-gbuffer
   files deleted in WP0's first cull with S1 re-entry errors).

## Risk register deltas vs `VULKAN_PORT_PLAN.md` §6

Carried: TBDR perf cliffs (now measured on MoltenVK in WP2, not P8), coupling-latency physics
change (WP7 ritual), coordinate-convention slips (WP9 named tests), parallel-drift (dashboard from
WP6). Changed: QRhi-compute-inadequacy and Qt-roadmap-coupling **retire**; in their place:
**(R-a) MoltenVK fidelity** — mitigated by WP2's capability table, S5 from WP2 on, and the ADR-004
native-Metal escape valve behind the sealed L0; **(R-b) hand-written sync bugs** (we own barriers
now) — mitigated by the graph being the only barrier emitter, sync-validation-as-failure in CI,
and the null-backend barrier-sequence unit tests; **(R-c) solo-owned plumbing** — mitigated by the
tests being the spec (every L0 module lands null-first with its contract suite) and by this
document being executable by any future engineer or agent without conversation context.
