# KRStudio Roadmap — toward a state-of-the-art robot-training & rendering engine

*Living document. Created 2026-06-12 from a deep-research pass (fluid solvers, smoke,
USD, Isaac-class RL infrastructure); update at the end of each round.*

**Where we are:** Qt6 / OpenGL 4.5 compute / EnTT / PhysX 5.5 (vcpkg, GPU DLL present).
GPU PBF fluid (200k particles, real SI units, GPU stream-compaction so emitter+sink
flows reach steady state, curl-noise turbulence) with screen-space surface rendering
(anisotropic ellipsoid splats, two-interface refraction, Ihmsen whitewater, HDR/ACES),
CPU SPlisHSPlasH DFSPH reference tier, OpenVDB SDF baking + hero-still particle
meshing, glass (screen-space refraction + dispersion) + water caustics, and — new this
round — a **dense Eulerian smoke + fire gas solver** with volumetric rendering. Plus
auto exact-shape collision, analytic physics benchmark suite (KRS_BENCH), texture/asset
browsers, mesh-native materials, emitter/sink/smoke/fire placement tooling.

**Realized 2026-06-13 (gas round):** SmokeSystem (96³/128³ grid: semi-Lagrangian
advection, vorticity confinement, Jacobi pressure projection, combustion) + SmokePass
(HDR volumetric ray-march, blackbody fire emission, depth-occluded), FluidSinkComponent
+ GPU compaction, water turbulence, Gas properties dock, Add-menu/right-click sources.

**Realized 2026-06-13 (MLS-MPM round):** a GPU **MLS-MPM continuum solver**
(`MpmSystem`, Hu 2018) unifying fluid, elastic, sand and snow in one framework —
APIC transfers, deformation-gradient F tracking, fixed-corotated Neo-Hookean (GLSL
3×3 SVD), Drucker-Prager sand, and a coupled **thermal field** (grid heat diffusion +
phase change that melts solids to fluid in-solver). Fixed-point int-atomic P2G/G2P on
a uniform grid; CFL-adaptive substeps; lit point-sprite rendering (`MpmPass`); demos
(`KRS_MPM_DEMO=1..5`). Verified by a headless analytic suite (`KRS_MPM_SELFTEST`,
14/14): free-fall v=g·t to 7e-6, exact mass conservation, bounded fluid/elastic/sand
rest, sand column collapse, heat diffusion 55→0.6 °C with mean conserved to 0.06 °C,
729/729 particles melting on phase change. This closes section A below.

**Realized 2026-06-13 (Sim-to-Real HIL, Phase 1):** a reverse-mode **adjoint
MLS-MPM** differentiable physics core, an **async 1 kHz clock coordinator** with a
lock-free ring buffer, and **virtual camera / CAN bridges** behind OS-abstracted
interfaces. Verified headlessly (`KRS_MPM_SELFTEST`): ADJOINT_GRADIENT_CHECK to
~1e-9 (target 1e-5), HIL_JITTER max 0.05–0.07 ms (gate 1.5 ms), and
LOOPBACK_FRAME_INTEGRITY 1080p zero-drop bit-exact. Full design + trade-offs in
§H below.

The remaining tracks are the SOTA upgrades the user called out (the heavier
incompressible-projection + sparse-grid layer in §A1, two-phase flow,
participating-media rendering) and the robotics stack.

**Where we're going:** an MQTT-connected robot-training engine that imports USD
scenes and trains policies Isaac-Sim-style, with film-grade fluids and smoke.

---

## A) GPU MLS-MPM — ✅ SHIPPED (2026-06-13)

| Option | Verdict |
|---|---|
| **GPU MLS-MPM** | **DONE.** Built as `MpmSystem`: 3 GLSL compute kernels (P2G with int32 fixed-point atomicAdd, grid update, G2P) + APIC affine transfers + deformation-gradient F. Materials: weakly-compressible Tait fluid, fixed-corotated Neo-Hookean elastic (branch-light 3×3 Jacobi SVD), Drucker-Prager sand, Stomakhin snow clamp, plus a coupled thermal field with phase change. Uniform dense grid (64³ iGPU / 96³ CUDA), CFL-adaptive substeps. Verified 14/14 against analytic ground truth. |
| GPU DFSPH | Skipped — costs more (persistent neighbour lists, two iterative loops) and yields less (water only). |
| FLIP/APIC | Subsumed — MLS-MPM *is* the APIC route; explicit stress reaches all the materials. The incompressible-projection variant is §A1 below. |
| Neural surrogates | Nothing production-ready for liquids; re-evaluate in 12 months. |

What shipped: the explicit MLS-MPM stress formulation, which reaches fluid + elastic
+ sand + snow + thermo on a dense uniform grid **without** a global pressure solve or a
sparse grid. Tiers in practice: 780M ~100–240k particles, 4080 0.5–1M+. The PBF tier
stays shipping for screen-space water surface; CPU DFSPH remains the offline ground truth.

## A1) Heavier next layer over the explicit core — IC-PCG projection + sparse grid

These were on the user's wish list. Neither is required for the materials above — the
explicit Tait EOS already gives weakly-compressible water — so they are deliberately
deferred as a *quality/scale* layer, not correctness. Documented here as the concrete
next step:

- **Monolithic incompressible pressure solve (PCG + Incomplete-Cholesky).** Replace the
  fluid's weakly-compressible EOS with a true divergence-free Chorin projection: after the
  grid-momentum update, assemble the sparse pressure-Poisson system over fluid cells
  (multi-phase water/air via per-cell phase masks and a ghost-fluid free surface, p=0 in
  air) and solve `A p = div(v)` with a **Preconditioned Conjugate Gradient** + an
  **Incomplete-Cholesky(0)** preconditioner, then subtract ∇p. The smoke solver's existing
  divergence→Jacobi→gradient plumbing is the scaffold; the work is (a) a matrix-free SpMV
  over the MAC grid, (b) the IC(0) factorization/apply on GL 4.3 compute (no double-precision
  atomics — use a damped-Jacobi or red-black SSOR preconditioner if IC(0)'s serial triangular
  solves prove too divergent on GPU), (c) the CG reductions via fixed-point or two-pass
  float sums. Buys hard incompressibility (no EOS ringing, larger stable dt for water). Effort
  ~2–4 weeks; the explicit path stays the default and ground truth.
- **Sparse voxel grid (OpenVDB/NanoVDB or a custom block-radix tree).** The current grid is
  dense (memory ∝ N³), fine at 64–96³ but wasteful for large mostly-empty domains. A
  claymore-style block-sparse grid (allocate 4³/8³ bricks only where particles live) lifts the
  ceiling toward multi-million particles on the 4080 and large open scenes. NanoVDB (PNanoVDB.h)
  reads on GPU; allocation/compaction is the work. Effort ~2–3 weeks. Pursue only when a real
  scene needs >~1M particles or a domain too big to keep dense.

## A2) FLIP/APIC hybrid liquid — the next big solver after MPM groundwork

The user's direction (correct): move water off pure PBF/SPH onto a **hybrid
particle-grid** solver so it stops looking like blended spheres and gains real
incompressibility + low numerical dissipation. Two routes share ~80% of the code, so
build the transfer machinery once:

- **MAC staggered grid** (velocity components on cell faces, pressure at centres) — the
  one structural change from our current smoke grid (which is collocated). Reuse the
  smoke pressure-projection plumbing (divergence → Jacobi/MGPCG → gradient subtract),
  adapted to face-staggered sampling.
- **P2G / G2P transfer** with APIC affine moment matrices (Jiang 2015): scatter particle
  momentum to the grid (int32 fixed-point `atomicAdd`, the WebGPU-Ocean pattern that
  already underpins our compaction), solve incompressibility on the grid, gather back.
  FLIP blends `v_new = α·(v_p + Δv_grid) + (1−α)·v_grid_interp`, α≈0.95–0.99. APIC is the
  stable modern default (no FLIP noise, no PIC smear).
- **Free surface**: a narrow-band level set or particle-coverage mask marks air/fluid
  cells; solve pressure only in fluid cells with a free-surface (p=0) air boundary.
- **Two-way rigid coupling** (user's ask): rasterise each rigid body's velocity + a
  solid mask onto the MAC grid; treat solids as moving velocity boundary conditions in
  the pressure solve; integrate the resulting pressure·normal over the solid surface to
  get buoyancy/drag/torque back onto the PhysX body. This is strictly better than our
  current per-particle impulse coupling (it captures transmitted hydrostatic pressure,
  fixing the N5 low-ride floater).

**Two-phase flow (2026 SOTA, the user's spray/foam point):** simulate air as a second
phase (or a cheap density-ratio drag term) so a fast water surface induces aerodynamic
drag ∝ v_rel² that rips it into spray/foam *physically*, retiring the ad-hoc
"speed > X → spawn foam" rule. Stage this AFTER FLIP/APIC lands — it needs the grid
velocity field of both phases. Reference: Adaptive Phase-Field-FLIP.

**Library option:** rather than hand-roll, evaluate vendoring a proven hybrid core —
**Taichi** (taichi_elements MLS-MPM/FLIP, Apache-2.0, but Python/JIT — would need the
C++/AOT path), or porting **nialltl/incremental_mpm** + **taichi_mpm**'s 88-line core to
GLSL compute (recommended: keeps everything in our engine context, no runtime dep). MPM
first (section A) because its APIC transfers and grid solve carry directly into FLIP/APIC.

**Effort:** MAC + APIC transfers + free-surface pressure ≈ 3–5 weeks; rigid coupling +1–2;
two-phase +2–3. Multi-week, own track — not a single round.

## B) Smoke milestone 1 — ✅ SHIPPED (2026-06-13), upgrades below

Done: dense Eulerian grid (96³ default / 128³), semi-Lagrangian advection, vorticity
confinement, Jacobi pressure projection with free-slip walls, combustion/cooling/
dissipation, emitter splats, HDR volumetric ray-march with blackbody fire and depth
occlusion. The original spec (kept below for reference) called for several upgrades not
in the MVP — **these are the smoke follow-ups:**
- MacCormack advection with 8-neighbour min/max clamp (we ship semi-Lagrangian — more
  dissipative; vorticity confinement compensates but MacCormack sharpens detail).
- Solid coupling: bake the existing OpenVDB SDF + a solid-velocity field into the grid so
  smoke flows around scene geometry and moving objects (currently free-slip box walls
  only).
- Half-res ray-march + bilateral upsample + blue-noise temporal accumulation (we ship a
  full-res single-frame march; half-res is the iGPU perf win).
- Light-march into a transmittance volume for proper multi-scatter self-shadow (we ship a
  cheap 4-tap shadow).
- Smoke→particle drag (couple the gas velocity onto foam/spray) and pressure forces on
  rigids.

### Original spec (reference)

Dense **128³ Eulerian grid** on GL 4.5 compute, ~30–40 MB of GL_TEXTURE_3D:
velocity RGBA16F ×2 (ping-pong), scalars (density/temperature/fuel/spare) RGBA16F ×2,
pressure R32F ×2, divergence R32F, curl RGBA16F, obstacle SDF R16F + solid velocity
(baked by our EXISTING OpenVDB pipeline), transmittance R16F.

Pass order: MacCormack advection **with mandatory 8-neighbour min/max clamp** →
scalar advection (reuse backtrace) → emitter splats → buoyancy `(−α·ρ + β·ΔT)·up` +
radiative cooling → curl → vorticity confinement (ε ≈ 2–10) → solid-aware divergence →
Jacobi ×20–40 (red-black GS ≈ 2× faster/sweep) → gradient subtract + free-slip at the
SDF boundary → per-cell short march to the light (8–16 steps) into the transmittance
volume → half-res view raymarch (64–128 jittered steps, early-out α > 0.99) →
blue-noise temporal accumulate + depth-aware upsample.

Budgets (bandwidth-model — profile before trusting): 780M = 96³ comfortable / 128³
stretch (~5–8 ms sim + 2–3 ms render); 4080 = 256³ dense. **Fire is nearly free**: a
reaction-coordinate scalar driving a 1000–3000 K blackbody LUT as raymarch emission.
Coupling order: solids→smoke via SDF (free) → smoke-drag on foam/spray particles →
pressure forces on rigids later. Sparse bricks only when a use case demands them
(NVIDIA Flow 2 source, BSD-3, is the architectural reference; PNanoVDB.h for reading
baked VDBs in GLSL).

## B2) State-of-the-art liquid rendering (the user's CGI-fidelity ask)

We have the screen-space pieces (aniso splats, two-interface refraction, Beer-Lambert,
HDR) and an offline OpenVDB hero-still mesher. The path to "film CGI" water:

1. **Anisotropic particle meshing as a realtime/near-realtime surface** — promote the
   hero-still OpenVDB mesher (already in-tree) toward an interactive surface: fit
   ellipsoids (Yu & Turk weighted PCA — we already compute this for splats) and surface
   them, OR keep the screen-space surface but drive it from the anisotropic depth (done)
   plus **flow-advected detail normals** (research brief B5) for moving micro-ripples.
   A continuous mesh gives the sharp, glass-like refraction sprites can't.
2. **Physically-based volume shading** — extend the composite to true dielectric volume:
   per-channel Beer-Lambert absorption by optical depth (have it), plus **subsurface
   scattering** at thin geometry (wave crests glow green/blue before breaking) via a
   thickness-driven forward-scatter term, and a proper Fresnel + GGX-rough specular
   (have F0/Fresnel; add roughness from surface curvature).
3. **White-water as participating media** — render foam/spray not as opaque sprites but
   as a heterogeneous scattering medium: accumulate multiple-scattering through the
   diffuse-particle cloud (cheap analytic dual-lobe phase or a few light-march taps),
   giving the soft, thick, self-shadowed look. Reuse the SmokePass ray-march machinery
   on a foam density splat.
4. **Caustics** beyond the current Wallace splat — temporal accumulation + chromatic
   dispersion (research brief C).

These are incremental on the existing renderer (days–1 week each), highest-impact first:
detail normals → SSS → participating-media foam.

## C) USD import

**TinyUSDZ + Tydra first** (Apache-2.0, C++14, STL-only — vendor it): reads
USDA/USDC/USDZ, Tydra hands over a triangulated RenderScene with resolved
UsdPreviewSurface bindings → days to EnTT integration behind an `IUsdImporter`
interface. Milestone 1 scope: meshes + xforms + materials from **flattened** stages
(composition arcs are TinyUSDZ's weak spot — flatten in usdview/usdcat as the
workaround). Conformance-test against usd-wg/assets.

**OpenUSD spike at month 4–6**: vcpkg port `usd` v26.3 (deps now just tbb+zlib, MSVC
x64 supported, no Python). Known trap: `ArDefaultResolver` plugin discovery breaks if
the `usd/` plugInfo.json layout isn't preserved next to the binaries (vcpkg #37947) —
validate `UsdStage::Open` from an INSTALLED layout. Swap to OpenUSD when composition
and first-party UsdPhysics parsing are needed.

## D) Robot training (Isaac-class, scoped honestly)

- **Articulations:** `PxArticulationReducedCoordinate` is already in our vcpkg tree
  (header + PhysXGpu_64.dll confirmed). Fix/Prismatic/Revolute/Spherical joints,
  implicit-PD drives that map 1:1 onto `UsdPhysicsDriveAPI`, limits, tendons.
  **URDF first** — urdfdom is already in the manifest, so a 6-DOF test arm lands
  before any USD schema work.
- **Deterministic headless stepping:** fixed 1/240 step (already our config),
  destroy-and-recreate scene on reset with identical actor insertion order,
  `eENABLE_ENHANCED_DETERMINISM`, replay-hash determinism test in CI from day one.
- **Vectorization:** N CPU PhysX scenes on a thread pool — hundreds of envs, plenty
  for state-based RL. The GPU tensor pipeline is an explicit **non-goal for year one**.
- **Sensors:** reuse the offscreen renderer for RGB/depth annotators (batch per-env
  cameras into one tiled target); lidar = PhysX raycasts. No RTX-faithful sensor sim.
- **Python bridge:** pybind11 module exporting a batched VecEnv
  (`reset() → obs`, `step(actions) → obs/rew/done` as NumPy) wrapped for
  Gymnasium/SB3 and rl_games. **Keep MQTT/gRPC out of the training loop** —
  serialization latency kills throughput.
- **MQTT = the digital-twin layer:** paho-mqttpp3 (vcpkg) + mosquitto broker.
  Topics `krstudio/<robot>/telemetry/{joint_states,pose}`, `…/cmd/{joint_targets,estop}`,
  `…/status` (retained + last-will). QoS 0 for high-rate streams (stale commands are
  worse than lost ones), QoS 1 for discrete commands/e-stop, never QoS 2 in control
  loops. Skip ROS 2 on Windows; bridge later only if the ecosystem demands it.
- **Domain randomization:** plain engine code on the recreate-on-reset path (masses,
  friction, drives, sensor noise, spawn poses). Trivial once reset is solid.

## E) 12-month staged plan (dependency-ordered)

| Mo | Fluids/rendering | Smoke | USD/robotics |
|---|---|---|---|
| 1 | ~~Ihmsen foam~~ ✅, ~~hero-still meshing~~ ✅ (this round) | — | TinyUSDZ+Tydra importer → EnTT |
| 2–3 | **MLS-MPM MVP**: P2G/grid/G2P, APIC, water; validate against DFSPH | Dense 128³ smoke on the 780M; SDF one-way coupling | Articulation wrapper; URDF import; PD-drive a 6-DOF arm |
| 4–6 | MPM materials (sand/snow/viscous); SSF adapted to MPM particles | Fire channels + blackbody LUT; two-way rigid forces | UsdPhysics parsing; OpenUSD spike; MQTT twin |
| 5–7 | — | 256³ on 4080 arrival; profile | Sensors: RGB/depth annotators, raycast lidar |
| 7–9 | Block-sparse MPM (>1M on 4080) | Brick-sparse grid if domains demand | **Headless + vectorized** envs; determinism CI |
| 9–11 | Offline hero renders via DFSPH + Jeske tension | MGPCG pressure if needed | **pybind11 VecEnv → SB3 PPO**; cartpole/reach end-to-end |
| 11–12 | Re-evaluate FLIP/APIC (post-Vulkan call) | Froxel fog integration | Domain randomization; first sim-trained policy driven over MQTT |

Ordering rationale: MPM before any FLIP (APIC code carries over); URDF before USD
physics schemas (test robot without blocking on parsing); determinism strictly before
the RL bridge (RL on a nondeterministic sim is misery); randomization last because
it's trivial once reset is solid.

## F) Standing risks

1. **MPM P2G atomics on RDNA3** — fixed-point atomicAdd contention may underperform on
   the 780M; mitigate with shared-memory grid tiling, accept the iGPU tier cap. The
   4080 is the real MPM target.
2. **Smoke budgets are bandwidth models, not benchmarks** — build dense 128³ first and
   profile on the 780M (it shares LPDDR5 with the whole frame).
3. **OpenUSD deploy fragility on MSVC** (plugInfo.json layout) — TinyUSDZ-first makes
   it a non-blocking spike.
4. **TinyUSDZ composition gap** — real robot USD assets love references/variants;
   flatten as the workaround, OpenUSD as the trigger to swap.
5. **PhysX determinism discipline** — actor insertion order, recreate-on-reset,
   thread counts; a replay-hash CI test guards all of it.
6. **Scope creep toward Isaac parity** — RTX sensors, Replicator-scale synthetic data,
   GPU tensor pipeline, deformables: explicit non-goals until a policy trains
   end-to-end.
7. **Known engine debts:** N5 coupling rides low (boundary-particle density coupling is
   the planned upgrade — also fixes the perpetual foam churn over submerged bodies);
   TGS restitution phase loss (#38); Weiler2018 viscosity AVX crash (#39); V-HACD
   multi-hull dynamics (#40); near-black POM material on the demo dragon (#63);
   MaterialDirectoryTag not yet DB-persisted.

---

## H) Sim-to-Real HIL infrastructure (Phase 1 — SHIPPED 2026-06-13)

Three subsystems toward hardware-in-the-loop training and deployment. All are
verified headlessly by the extended `KRS_MPM_SELFTEST` suite.

### H.1 Differentiable physics — reverse-mode adjoint MLS-MPM (`MpmAdjoint`)
A CPU double-precision differentiable twin of the realtime GPU `MpmSystem`. Exact
forward math (quadratic B-spline weights, `D⁻¹=4/dx²` APIC, fixed-corotated
Neo-Hookean, Drucker-Prager) + a tape-based reverse pass: adjoint P2G/G2P through
the APIC `C` matrix (incl. position gradients through interpolation weights and
node offsets), an analytic 3×3 SVD adjoint, and analytic constitutive adjoints
(Neo-Hookean stress; DP return-map gradients flowing through the log-singular-value
cone projection). `ADJOINT_GRADIENT_CHECK` (central FD vs analytic, control =
initial velocity of a deforming elastic block) matches to **~1e-9** (target 1e-5).

**Trade-offs (documented):**
- **CPU double precision, not GPU**, and a *separate* core from the realtime GPU
  solver. This mirrors the engine's existing GPU-PBF (realtime) / CPU-DFSPH
  (reference) split. Rationale: the 1e-5 gradient-check bar is unreachable through
  the GPU forward path because its **fixed-point int32 atomics are order-
  nondeterministic** (the same reason that scatter is deterministic bit-to-bit is
  exactly what makes its rounding path-dependent across runs), and reverse-mode
  needs an exact transpose of the forward linearization. Double-precision CPU also
  sidesteps the `SCALE=1e7` fixed-point quantization the forward GPU pass uses —
  the adjoint carries **no fixed-point scaling**, so there is no adjoint-side
  scaling bound to tune.
- **SVD-adjoint degeneracy clamp:** the per-pair coupling `1/(σ_j²−σ_i²)` is set to
  zero when `|σ_j²−σ_i²| < 1e-9` (repeated singular values). This is the standard
  differentiable-SVD guard; gradients at exactly-repeated singular values are
  subgradients.
- **Plasticity at the yield boundary:** the DP return map is piecewise (tip /
  inside-cone / projecting); its adjoint is exact within each branch and a
  subgradient on the cone surface. The gradient check runs in smooth regimes.

### H.2 Async clock coordinator + lock-free ring (`HilClock`)
Physics runs on a dedicated thread at a rigid rate (default 1000 Hz); the sensor/
render pipeline runs on another thread at a lower rate (default 30 Hz), handed off
through a lock-free SPSC `StateRing` (sequence-stamped latest-value, power-of-two
slots, acquire/release). No locks on the physics hot path. Cadence held by a
sleep-then-spin wait on a steady high-res clock + a 1 ms Windows timer period.

**Trade-off (documented): the 0.15 ms max-jitter target needs a real-time kernel.**
Stock Windows 11 / non-PREEMPT_RT Linux cannot *guarantee* a worst-case scheduling
bound — the scheduler can preempt any user thread. The local `HIL_JITTER` gate is
therefore **1.5 ms** (measured mean ~0.1 µs, p99 ~0.3 µs, max ~0.05–0.07 ms over
10,000 ticks — already inside 0.15 ms in practice, but not *guaranteed*). The hard
0.15 ms determinism guarantee requires a **PREEMPT_RT host** (or an RTOS / isolated
CPU + `SCHED_FIFO`), which is the deployment target.

### H.3 OS bridges — virtual camera + CAN (`HilBridges`)
`IVirtualCamera` / `IVirtualCAN` abstractions so the engine dispatches identically
regardless of backend.

- **Linux (deployment):** `v4l2loopback` (`/dev/videoX`, `V4L2_PIX_FMT_RGBA32`) and
  SocketCAN RAW on `vcan0`, compiled under `#ifdef __linux__`. An external
  perception/SLAM stack opens `/dev/videoX` as a standard capture device.
- **Windows (this dev box):** a named cross-process **shared-memory frame ring**
  (header + 8 slots, sequence-stamped, zero-copy) an external reader maps by name;
  and a **UDP-localhost** transport carrying the exact 16-byte SocketCAN `can_frame`
  byte layout (`static_assert(sizeof(CanFrame)==16)`), bidirectional.

**Trade-off (documented):** V4L2/SocketCAN are Linux-kernel facilities with no
Windows equivalent, so the dev-box backends are functional stand-ins, not the
literal devices. `LOOPBACK_FRAME_INTEGRITY`'s zero-drop/bit-exact guarantee at
1080p is a property of the **reliable** transport (shared memory; or the kernel on
the v4l2 target) — a lossy datagram path could not meet it, which is why the camera
loopback is shared-memory rather than UDP. The CAN transport uses UDP because CAN
frames are tiny (16 B, no fragmentation) and the rates are low.

### Verification (extended `KRS_MPM_SELFTEST`)
| Module | Result on this box | Target / note |
|---|---|---|
| `ADJOINT_GRADIENT_CHECK` | max rel err ~1e-9 | < 1e-5 |
| `HIL_JITTER` (10k @ 1 kHz) | mean ~0.1 µs, p99 ~0.3 µs, **max ~0.05–0.07 ms** | local gate 1.5 ms; 0.15 ms needs PREEMPT_RT |
| `LOOPBACK_FRAME_INTEGRITY` | 1080p, **0 drops, bit-exact** (5 s default; `KRS_HIL_LOOPBACK_SECS=60` for the full window) | 60 s continuous |
| `CAN_LOOPBACK` | 64/64 frames, 0 corrupt, bidirectional | — |

**Env hooks:** `KRS_MPM_SELFTEST=1` runs all modules; `KRS_HIL_LOOPBACK_SECS=<n>`
sets the camera loopback window.

### Phase 2 — integration (SHIPPED 2026-06-13)
The Phase-1 components are now wired into the live engine.

- **CAN telemetry ↔ plant.** `SimulationController` (when `KRS_HIL_CAN` is set)
  drains incoming `can_frame` effort commands each fixed physics step, applies them
  as continuous body forces *before* `simulate()`, and publishes each actuator
  body's pose / velocity / applied-effort back as state frames after
  `fetchResults`. Entities opt in via `HilActuatorComponent{axisId}`. A CANopen-
  style `cancodec` packs int16 channels onto the 16-byte SocketCAN frame
  (cmd 0x200+axis, state 0x180/0x1C0/0x140+axis). **Boundary:** the live sim steps
  at 240 Hz on the Qt main thread (PhysX is not stepped cross-thread), so the
  exchange runs in that step, not on the standalone 1 kHz `HilClock` thread —
  driving PhysX from the HilClock thread is the Phase-3 reconfiguration. No PhysX
  articulations exist yet, so an "axis" is a rigid-body DOF set. Verified by
  `CAN_PLANT` (command 8 N → decoded 8 N → mock plant → encoder 0.0400 m within
  the 1 mm CAN quantization).
- **Camera → shared memory.** `RenderingSystem::publishHilCameraFrame` reads the
  finished `finalColorTexture` (RGBA16F) into sysmem and publishes RGBA8 frames to
  the shared-memory ring at 30 Hz. **Boundary:** GL is thread-affine, so the
  readback runs on the engine/render thread (the async sensor thread is the ring
  *consumer*), not literally inside the HilClock sensor thread. Verified live
  (30 Hz stream into the ring proven bit-exact by `LOOPBACK_FRAME_INTEGRITY`).
- **Multi-fidelity trajectory verification.** `TrajectoryVerifier::submit` runs a
  conservative inertial surrogate sweep (flags > 75% yield) and forks only flagged
  segments to background `std::async` workers running the exact double-precision
  `MpmAdjoint` stress pass; the planner gets non-blocking `std::future` tokens.
  Verified by `TRAJECTORY_HIL_LOOP` (transient REJECTED, surrogate-flagged moderate
  bump cleared SAFE by the exact pass, submit 0.1 ms vs 34 ms/exact).

### Phase 3 (next)
Run the physics plant *on* the `HilClock` 1 kHz thread (single-owner PhysX scene,
removing the main-thread 240 Hz coupling) for true hard-RT HIL; PhysX articulations
(`PxArticulationReducedCoordinate`) so a CAN axis is a real joint, not a free body;
use `MpmAdjoint` as the gradient backend for trajectory optimization / system-ID,
and graft the verified adjoint kernels onto the GPU forward solver for batched
differentiable rollouts.

---

## I) Deep visualization & dynamic thermodynamics (Phase 3 — SHIPPED 2026-06-13)

### I.1 Multi-mode physics visualizer
The viewport hot-swaps between **Default (PBR)**, **Thermal**, **Stress (von
Mises)** and **Strain** via *View > Physics Visualization* (or `KRS_MPM_VIZ=1/2/3`).
The MLS-MPM particle splats are recoloured per-particle in the render vertex
shader: thermal reads the stored temperature; stress/strain are computed from
the deformation gradient F. Range is auto-calibrated from the live field on mode
change and every ~0.75 s (dynamic normalization), and is also directly
configurable. Rigid meshes show heat via an emissive tint (below), so a hot body
glows in any mode.

### I.2 Projection strategy & performance trade-off (Task 3 deliverable)
**Chosen: direct per-particle splat recolour — zero projection cost.** The
physics scalars (temperature, F) already live per-particle in the solver SSBO, so
the render shader maps scalar → colour in the *same* vertex invocation that
places the splat. There is no separate projection pass and no added draw cost
over the existing particle render.

Alternatives considered and why they were not used as the default:
- **Distance-weighted scatter/gather particle → high-poly surface mesh.** Naive
  cost is O(meshVerts × nearbyParticles); even with a grid acceleration
  structure it adds a per-frame pass and a readback/bind dependency between the
  solver and the mesh draw. Unjustified when the deformable bodies *are* the
  particles — meshing them first only to recolour them is pure overhead.
- **Grid-to-mesh interpolation.** The MPM thermal grid is a dense field a mesh
  vertex could trilinearly sample; this is the right tool for tinting an
  *external* surface (e.g. a rigid arm) by the surrounding temperature, and is
  available via the smoke/MPM grid getters. Kept as an option, not the default.

**Stress proxy trade-off.** The visualized von Mises uses the StVK 2nd-Piola
stress from the Green strain `E = ½(FᵀF − I)` and the body Lamé params — **no
SVD in the render shader**. This is exact in the small/moderate-strain regime and
visually faithful elsewhere; it differs slightly from the solver's fixed-corotated
stress (which needs the SVD) but costs a few matrix ops per particle instead of a
full Jacobi SVD, keeping the recolour effectively free at 60 fps. Strain mode
shows ‖E‖ directly.

**Rigid meshes → emissive tint (CPU).** Bodies carrying heat (a
`HeatSourceComponent`, or anything the thermo system marks hot) are visualized by
driving `MaterialComponent.emissiveColor` from their temperature on the CPU —
reusing the existing emissive G-buffer path, touching none of the five gbuffer
shader variants. Cost is one ECS write per hot body per frame.

**Auto-range cost.** Dynamic normalization reads the particle buffer back to the
CPU at ~1.3 Hz (mode-change + every 45 frames) to compute min/max — the same
readback used for diagnostics, negligible at demo particle counts; at very high
counts it would move to a GPU min/max reduction (noted, not yet needed).

### I.3 Dynamic thermodynamics
- **Flame → MPM:** the heat-gather samples the SmokeSystem temperature texture at
  each particle where the domains overlap (flame g=1 ≈ 600 °C), so a fire scorches
  material in its plume.
- **HeatSourceComponent:** motor-coil / friction heat — drives MPM particles within
  a radius toward a target temperature (up to 8 sources/frame) and glows the
  source mesh.
- **Conduction & dissipation:** particles relax toward the hottest local influence
  and diffuse among themselves, so heat spreads through a body and bleeds to cool
  ambient when the source is removed.

**Verification:** `HEAT_SOURCE` self-test — a body warms 20 → 250 °C under a
source then dissipates to 20 °C when removed; thermal/stress viz confirmed by
grabs (flame-heated particles render hot, sand-collapse stress 0–6600 Pa). The
MPM (15 checks), adjoint, HIL and `KRS_BENCH` (7/7) suites stay green.

### Phase 4 (next)
Continuous coloured surfaces for the deformable bodies (graft the scalar onto the
SSF / OpenVDB mesher); grid-to-mesh thermal sampling on rigid arms for true
surface scorch maps; a GPU min/max reduction for auto-range at >1M particles.

---

## J) CAD pipeline & engineering UI overhaul (Phase 4 — 2026-06-13)

Transforms the engine toward a CAE workspace: STEP ingestion, material ground
truth, and an engineering toolbar.

### J.1 Engineering UI & Qt layout trade-offs (Task 1)
The visible top bar is a `.ui`-generated custom `StaticToolbar`; the real
`QMainWindow` menu bar is hidden (`menuBar()->setVisible(false)`). A consequence
worth recording: the Phase-3 visualization menu added under *View* was never
visible. Phase 4 resolves this.

**Trade-off chosen:** rather than hand-edit the uic-generated `StaticToolbar` .ui
(fragile, regenerated) or re-show the menu bar (breaks the clean look), the
engineering tools live on a standard `QToolBar` added via
`QMainWindow::addToolBar`. It docks in the toolbar area above the central widget,
sitting just above the custom `StaticToolbar`. Cost: two stacked toolbars (a
minor vertical-space cost); benefit: zero coupling to the generated UI, trivially
extensible, and it finally surfaces the viz control. The toolbar carries: Import
CAD (STEP), a Visualization-mode dropdown (PBR/Thermal/Stress/Strain ->
`setVizMode`), Assign Material, Add Heat Source, Inspect.

Legacy "game-engine" spawns (primitives, lights) were already off the main
workflow (they live only on the hidden menu bar / right-click context menu), so
no removal was needed — they are simply not surfaced on the engineering toolbar.

The ECS inspector is dialog-based (Assign Material / Add Heat Source / Inspect),
reading and writing the selected entity (tracked by the `SelectedComponent` tag)
MaterialComponent + HeatSourceComponent. A docked live-editing panel is a future
refinement; dialogs were the lower-risk path that avoided weaving into the
existing gridPropertiesWidget.

### J.2 OCCT STEP ingestion + feature recognition (Tasks 2-3)
`CadImporter` (behind `KR_WITH_OCCT`; opencascade added to vcpkg.json):
`STEPControl_Reader` -> `TopoDS_Shape`; one ECS entity per `TopAbs_SOLID`;
`BRepMesh_IncrementalMesh` triangulation (adaptive deflection = 0.4% of the
solid bbox diagonal) emitted into `RenderableMeshComponent` with smooth normals;
`GProp_GProps` exact B-Rep volume onto the MaterialComponent. Feature
recognition: `TopExp_Explorer` over faces, `BRep_Tool::Surface` ->
`Geom_CylindricalSurface` down-cast; each cylinder's `Axis()` + `Radius()` become
an `AttachmentFrame` (hole vs shaft from face orientation) on an
`AttachmentComponent` for kinematic anchoring.

**Build trade-off:** OpenCASCADE is not pre-installed; it is built once via vcpkg
(~30-45 min). The CAD module is guarded by `KR_WITH_OCCT` with an `#else` stub so
the engine builds and the toolbar degrades gracefully without OCCT. Linked via
`${OpenCASCADE_LIBRARIES}` (the whole toolkit set) rather than naming `TKSTEP`,
because OCCT 7.8/8.0 renamed the STEP toolkit to `TKDESTEP` — the config variable
is version-robust.

### J.3 Materials Project ground truth (Task 4)
`krs::materials::query` resolves a name or mp-id to SI density / bulk / shear
modulus. Live path: a Python `mp_api.client.MPRester` subprocess (used only when
`MP_API_KEY` is set + mp-api installed); offline path: a canonical-material table
(steel, aluminium, titanium, copper, tungsten, ...). `deriveElastic` gives E, nu
from K, G. Mass = density x volume, where volume is the OCCT B-Rep `GProp`
volume (or a signed-tetrahedra integral of the triangle mesh for non-CAD bodies).

**Constraint on this machine:** network reaches the MP API, but there is no API
key and mp-api is not installed, so live queries cannot authenticate here — the
offline DB is the verified path; the live path activates when a key is provided.
Live mp-api field names/units follow the documented API and are best-effort
(unverified here).

### Verification
KRS_BENCH 7/7 throughout. UI: engineering toolbar confirmed visible by grab.
Materials: offline values + derived E/nu sanity-checked (steel E~204 GPa,
nu~0.29). OCCT: a headless CAD self-test (box-minus-cylinder STEP round-trip:
solid count, cylinder-feature detection, GProp volume) gates Tasks 2-3.

### Phase 5 (next)
A docked live-editing engineering inspector; STEP assembly hierarchy / instance
naming; non-cylindrical features (planar mating faces, threaded holes); a curated
local material cache so common mp-ids resolve offline.
