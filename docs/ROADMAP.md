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

---

## K) Engineering UI hotfix + energy-based thermal physics (Phase 4.5 — 2026-06-13)

### K.1 Visualization-mode repaint hotfix (Task 1)
The Phase-3 viz selectors (engineering-toolbar combo + View-menu action group)
called `MpmSystem::setVizMode` but never repainted, so the PBR/Thermal/Stress/
Strain swap waited for the next engine frame (one ~33ms fallback tick away). Since
the recolour is purely shader-uniform-driven (`MpmPass` uploads `u_vizMode`/range
every frame), both connect sites now call `RenderingSystem::renderAllViewports()`
right after `setVizMode` to re-run the pass and present immediately. A plain
`update()` is insufficient -- `ViewportWidget::paintGL` only re-blits the last
finished engine frame.

### K.2 Volumetric heat generation in Watts (Task 2 -- Neumann)
`HeatSourceComponent.power` (W) replaces the fixed-target (Dirichlet) coupling.
Each thermal step a source injects `Q = power*dt` of energy into the material in
its radius. The scatter pass accumulates the total thermal mass `C_total = sum(m*c_p)`
of in-radius particles (a per-source SSBO); the gather applies the uniform
`dT = power*dt / C_total` to each. This is exactly energy-conserving (total added
= `power*dt`, independent of particle count) and honours per-particle mass + c_p.
`c_p` (`plastic.z`) is now data-driven from `MpmBodyComponent.heatCapacity`
(previously a hardcoded 900). Materials gain SI `specificHeat` and
`thermalConductivity`, sourced from the offline handbook table (the MP summary
endpoint exposes neither, so live queries graft them from the table by id).

### K.3 Grid-based Fourier conduction (Task 3 -- div(k grad T))
The mean-temperature, single-coefficient diffusion is replaced by an
energy-conserving Fourier conduction on the background grid, which naturally
couples different ECS bodies that touch on the grid:
- **P2G**: scatter thermal energy (m*c_p*T), thermal mass (m*c_p) and the
  k-weighted mass (m*c_p*k).
- **Grid**: node `T = energy/thermalMass`, node `k = kAccum/thermalMass`, decoded
  node thermal mass `C`.
- **Diffuse**: one explicit sweep of `dT_n = (S*dt*dx / C_n) * sum_faces kface*(T_nbr-T_n)`,
  with **`kface` the harmonic mean** `2 k_n k_nbr/(k_n+k_nbr)` (correct
  series-resistance flux between unequal-k cells). The per-face conductance is
  clamped SYMMETRICALLY (`a_f = min(S*dt*dx*kface, betaMax*min(C_n,C_nbr)/6)`,
  `betaMax=0.5`), so the pairwise flux stays antisymmetric -> energy is conserved
  exactly even where the clamp is active, and the update is a convex combination
  of neighbour temperatures -> unconditionally stable. (An earlier per-cell clamp
  leaked energy at heterogeneous-k interfaces; caught by the adversarial review.)
- **G2P**: interpolate the diffused temperature back to particles.

Per-particle `k` required a 12th particle vec4 (`therm2.x`), growing the stride
44 -> 48 floats across all five MPM shaders (`MpmPass` draws by SSBO `gl_VertexID`,
so no vertex-attribute stride to touch).

**Trade-off (conduction scale):** real metals conduct on minute timescales at this
grid resolution (`alpha = k/(rho*c_p) ~ 1e-5 m^2/s`, dx ~ 0.05 m -> a per-frame
physical diffusion far below one cell). A dimensionless `m_conductionScale` (S,
default 2000, env `KRS_MPM_COND_SCALE`) accelerates conduction to interactive
rates; the stability clamp keeps it bounded, and the harmonic mean preserves the
relative inter-material fluxes. Exact energy conservation holds in the unclamped
regime (the self-test uses a modest S so its assertion is exact).

### Verification
KRS_MPM_SELFTEST 18/18 PASS, including the new CONDUCTION test: two touching
blocks of different k (400 vs 100 W/m.K) equilibrate (spread 60C -> 0.36C) with
the mass-weighted mean (energy) conserved to 0.4C. The energy-basis refactor keeps
the diffusion/melt/Watts tests green; all mechanical checks pass (the 48-float
stride is consistent everywhere). KRS_BENCH 7/7.

### Phase 5 (next, unchanged from J)
Docked live thermal inspector; STEP assembly hierarchy; non-cylindrical CAD
features; a curated local material cache; and -- now that conduction is on the
grid -- radiative/convective surface boundary conditions and temperature-dependent
material properties.

---

## L) Phase 5 — True FEM stress & thermal on rigid/mesh bodies (2026-06-13)

> Note: the Phase-5 directive says "ROADMAP §K", but §K is already Phase 4.5. To
> avoid clobbering it, Phase 5 is documented here as **§L** (the next section).

### L.0 Reconnaissance findings (what exists today)
- **Rigid path**: `RigidBodyComponent` (Static/Kinematic/Dynamic, mass) → PhysX actor
  in `SimulationController::createActorForEntity`. Per-entity geometry is a SURFACE
  triangle mesh only (`RenderableMeshComponent` of `Vertex{pos,normal,uv,tangent,
  bitangent}` — struct is FULL, no spare attribute). OCCT `CadImporter` produces only
  `Poly_Triangulation` (surface) + an exact `GProp` volume — **there is NO volume/tet
  mesh anywhere**. `MaterialComponent` already carries every FEM input (E, ν, ρ, k, c_p,
  SI, default 6061-T6). `HeatSourceComponent` gives Watts/radius (Phase 4.5 Neumann).
- **Contact forces are NOT exposed to the ECS** — `BenchContactLogger` sees PhysX
  contacts for instrumentation only; `CollisionData.impulse_magnitude` is an unpopulated
  placeholder. (Boundary decision below.)
- **Viz**: the cold→hot `ramp()` + per-particle scalar (temperature / StVK von Mises
  from F / ‖Green strain‖) lives ONLY in `mpm_render_vert.glsl`; `MpmPass` sets
  `u_vizMode/u_rangeMin/u_rangeMax`; `autoCalibrate()` does an on-demand CPU min/max SSBO
  readback. Regular meshes draw through `OpaquePass` (6 gbuffer shader variants, VAO locs
  0–4) with **no per-vertex scalar path today**.
- **Build deps**: **Eigen3 is present** (transitive dep of libigl 2.6.0, already linked;
  `<Eigen/Sparse>`, `SimplicialLDLT` available — NO vcpkg change needed). **ONNX Runtime
  is NOT present** (mature vcpkg port; add as build-optional). **TetGen NOT present.**
  Async template = `TrajectoryVerifier` (`std::async` → `std::future` polled non-blocking,
  240 Hz never stalls). New module → `src/Physics/` + `include/PhysicsHeaders/` (GLOB).

### L.1 DECISION — volume discretization: voxel/immersed HEX FEM on the MPM grid
Chosen: **(B) immersed hexahedral FEM on the existing regular background grid**, NOT
(A) TetGen tets. Justification (web-researched, sources in commit msgs):
- Reuses the MPM grid + SDF/occupancy infrastructure — **zero meshing pipeline**.
- TetGen requires a watertight, oriented, self-intersection-free surface; the failure
  mode for dirty/arbitrary BREP is "no mesh at all" — unacceptable for an interactive
  engine. Reserved as an offline high-accuracy path later.
- First cut = classic **voxel FEM** (used in CT/bone analysis): a cell with occupancy
  > 0.5 (SDF < 0 at centre) becomes one trilinear 8-node hex; precompute the 24×24
  elastic Kᵉ (BᵀDB, 2×2×2 Gauss) and 8×8 thermal Kᵉ (∇Nᵀ k ∇N) ONCE for the cubic cell
  and reuse for every full element (only material scales) — a major simplification
  unique to a regular grid. System is sparse SPD → Eigen.
- Known accuracy penalty: a staircased boundary gives local stress artifacts and
  trilinear hexes shear-lock in bending (cantilever tip deflection under-predicts).
  Mitigation this phase: enough elements + report the measured ratio; documented Phase-2
  upgrade = finite-cell (octree-integrated cut cells + α≈1e-6 fictitious stiffness +
  weak/Nitsche Dirichlet BCs).
- Solver: Eigen **SimplicialLDLT** (factor once, reuse across load cases / transient time
  steps) for ≤~50–100k DOF; **ConjugateGradient + Diagonal/IncompleteCholesky** above.

### L.2 DECISION — async cadence
FEM assembly+solve run on a background worker (`std::async`, mirroring TrajectoryVerifier);
the 240 Hz `SimulationController::tick()` polls the `std::future` with `wait_for(0)` and
publishes nodal fields to a `FemResultComponent` only when ready — never blocks. Structural
re-solve on geometry/BC/material change (dirty flag); transient thermal stepped at a
throttled cadence (implicit/backward-Euler so the step is unconditionally stable and the
matrix is reused).

### L.3 DECISION — learned surrogate (HONEST landscape)
There is **no drop-in pretrained "foundation model for FEA"** that ingests an arbitrary 3D
mesh + material + BCs and returns a stress/heat field via ONNX. The 2025–26 physics
foundation models (GPhyT, Walrus, PDE-FM, Poseidon, PhysiX) are fluid/continuum-dynamics
oriented, mostly grid-based/2D, research prototypes — explicitly NOT solid-mechanics/thermal
FEA, none shipped as an FEA ONNX file. MeshGraphNets / FNO / DeepONet are trained per
problem-class on simulation data, not general pretrained drop-ins. ONNX Runtime itself IS a
mature vcpkg port usable from C++.
→ Plan: TRAIN our own surrogate on the Phase-5 FEM oracle. Recommended architecture given a
voxel-hex grid = a **3D CNN / U-Net per-voxel regressor** (fixed tensor shapes, clean ONNX
export, matches our structured grid; material as per-voxel channels E/ν/k, BCs as
mask/SDF channels; output = stress-tensor components or temperature per voxel). A
MeshGraphNets-class GNN is the alternative if true topology generality is needed, but
accept ONNX export caveats (scatter_reduce numerical bugs, dynamic-shape friction) and
add a PyTorch-vs-ONNX round-trip CI check. Task 5 ships only the `SurrogateField`
INTERFACE + a stub + the FEM data-export; the FEM oracle stays the source of truth.

### L.4 DECISION — boundary calls
- **FEM loads**: since PhysX contact reaction forces are not in the ECS, the FEM oracle's
  loads this phase are the **gravity body force (ρg) + user-tagged Dirichlet fixities +
  explicit applied forces / HeatSource Watts**, not live contact reactions. Exposing
  PhysX per-contact impulses to the ECS (a real `PxSimulationEventCallback` → a
  `ContactReaction` component) is a documented follow-up, not this phase.
- **Geometry source**: the FEM uses the body's RENDER mesh (RenderableMeshComponent), not
  the cooked physics collision shape (which is a convex hull for dynamics).
- **MPM stays for soft/large-deformation**; rigid solids get rigid + FEM (policy §L / Task 4).

### L.5 Representation policy (Task 4) + unified viz (Task 3) — REALIZED
- **Rigid + FEM is the DEFAULT for solid bodies.** A solid (metal especially) is
  EITHER rigid + FEM (cheap; true stress/heat fields via the async oracle, no
  explicit-stiffness ceiling) OR deformable-MPM — never a coarse MPM proxy for a
  rigid solid. `FemBodyComponent` marks the rigid-FEM path; `MpmBodyComponent`
  marks the soft path.
- **MPM is reserved for soft / large-deformation bodies.** When MPM is used it
  should be resolved finely (small Δx) so it reads as a solid, with the render
  splat = Δx (Task 1), not a sparse spring-net.
- **Tie to interaction intent** (consistent with the engine's tiering): an
  un-interacted solid stays rigid + FEM; a body under active large-deformation
  manipulation may be promoted to MPM. The promotion *machinery* (swap component
  on grab) is a documented follow-up; this phase ships the rigid+FEM DEFAULT and
  the boot scene demonstrates it.
- **Boot scene**: the default block is now a rigid + FEM 6061 cube (a real solid
  triangle mesh) on the floor with a heater — replacing the MPM proxy.
- **Unified visualization (Task 3)**: FEM bodies recolour their SOLID render mesh
  per-vertex (nodal field interpolated to vertices, dedicated pos+scalar VBO) via
  a forward `FemVizPass` that reuses the EXACT cold→hot `ramp()` and shares the
  dynamic range with the MPM splats (`FemSystem` unions its scalar range into
  `MpmSystem` after MPM calibration, so gradients are comparable across the whole
  scene). Default mode → normal PBR; Stress/Thermal/Strain → recoloured surface.
  Verified by grabs: the 6061 block shows a von-Mises gradient (hot at the clamped
  base under self-weight) in Stress mode and a heater-driven gradient in Thermal
  mode, rendered as a SOLID surface (not particles). MPM particle bodies keep the
  splat recolour. One dropdown drives both representations.

### L.6 Learned-surrogate scaffold (Task 5) — oracle-first, NO faked model
Honest reality (web-confirmed, §L.3): there is **no drop-in pretrained FEA
foundation model**; a surrogate must be TRAINED on data from our own FEM oracle.
This phase ships ONLY the scaffold — the FEM solver remains the source of truth.
- **`ISurrogateField` interface** (`SurrogateField.hpp`): `available()` + `predictVonMises(...)`.
  `NullSurrogate` (active) always returns unavailable → callers use the FEM solve.
  `OnnxSurrogate` (behind `KR_WITH_ONNX`, compiled out by default) wires the
  `Ort::Env`/`Ort::Session` lifecycle for a future trained model; `makeSurrogate()`
  returns it only when ONNX + a model path are present, else `NullSurrogate`.
- **Build-optional ONNX**: CMake `find_package(onnxruntime CONFIG QUIET)` → defines
  `KR_WITH_ONNX` + links `onnxruntime::onnxruntime` if present (mirrors `KR_WITH_OCCT`).
  onnxruntime is a mature vcpkg port (1.23.x, CPU); it is NOT added to vcpkg.json yet
  (heavy; add when a trained model exists).
- **FEM as the data oracle**: with `KRS_FEM_EXPORT=<dir>` set, every async FEM solve
  writes a training pair `fem_sample_<n>.bin` (self-describing header + `FORMAT.txt`):
  input = per-cell float32 grid [occupancy, E/200e9, nu, k/400, cp/1000, fixed-mask,
  load-mask, heat-source-mask]; output = per-cell [von Mises (Pa), temperature (°C),
  strain-norm]. Channel-major, structured grid — directly consumable by the
  recommended **3D CNN / U-Net per-voxel regressor** (cleanest ONNX export, fixed
  shapes, matches our voxel grid; MeshGraphNets GNN is the alternative if true
  topology generality is needed, with ONNX scatter/dynamic-shape caveats — §L.3).
- The trained model would predict the field from the input grid in one forward pass,
  with the FEM oracle kept as ground truth / fallback (the `available()`-false path).

## M) Phase A — Articulation & kinematics on the real FANUC robot (2026-06-14)

> The Phase-A directive said "§K"; that section is Phase 4.5. Phase A is documented
> here as **§M** (the next free section after §L).

### M.0 Reconnaissance findings (what exists today)
- **PhysX articulation API is fully available** (PhysX 5.x in the manifest):
  `PxArticulationReducedCoordinate` (createLink/PxArticulationJointReducedCoordinate
  setJointType eREVOLUTE/ePRISMATIC/eFIX, setMotion/setLimitParams/setDriveParams),
  `PxArticulationCache` (jointPosition/Velocity/Force/Acceleration; flags
  ePOSITION/eVELOCITY/eFORCE write, eACCELERATION read), the analytical utilities
  (computeMassMatrix / computeGeneralizedGravityForce / computeCoriolisAndCentrifugalForce
  / computeDenseJacobian / computeJointAcceleration), and `PxD6Joint` for loop closure.
- **ECS robot model already exists**: rich `JointDescription` (`JointType`
  FIXED/REVOLUTE/CONTINUOUS/PRISMATIC/PLANAR/FLOATING, `axis`, `origin_xyz/rpy`,
  `JointLimits`, motor/PID/sensor blocks) + `LinkDescription` (mass, `glm::mat3`
  inertia, COM offset). `JointComponent{description, parentLink, childLink,
  currentPosition}` and `LinkComponent` wrap them; `ParentComponent`/`RobotRootComponent`
  give the tree. **No dynamics state and no PhysX articulation were ever built from these.**
- **The Phase-2 CAN coupling is the rigid-body-DOF-set FAKE to retire**: incoming
  effort frames (`can_id 0x200+axis`) are applied via `PxRigidDynamic::addForce(eFORCE)`
  on a whole body (`HilActuatorComponent`), not a hinge torque. State is published from
  the body pose/linear-velocity, not joint encoders.
- **`AttachmentComponent`/`AttachmentFrame`** (Phase 4) already carry OCCT-detected
  cylinder anchors (localPosition on axis, localAxis, radius, isHole) — the raw material
  for "detected axes become joints."
- **Build deps**: Eigen present (via libigl). `urdfdom`/`fcl`/`ccd`/Boost present.
  OMPL HAS a working vcpkg port (1.7.0, x64-windows) for the core lib — adopt in Phase B.

### M.1 The robot IS real — STEP discovery bug fixed
The Phase-A robot is the committed asset **`assets/FANUC-430 Robot.STEP`** (10.4 MB).
The earlier "zero *.step files" was a **discovery bug**: the extension is uppercase
`.STEP` and the filename contains a space, so a case-sensitive `*.step` glob missed it.
A new OCCT topology inspector (`krs::cad::inspectStep`, gated by `KRS_STEP_INSPECT=<path>`)
loads it with `STEPControl_Reader` (quoted full path, space-safe) and confirms it reads.
**No synthetic assembly is used.**

### M.1a Extracted topology (FANUC M-430iA parallel-link picker, units = mm)
`inspectStep` reports **17 solids**, assembly bbox X[-416..456] Y[0..2249] Z[-523..1820]
(**Y is vertical**), and clusters cylindrical faces into shared coaxial "hinge lines".
The kinematic structure (large-radius bores shared between *structural* solids; tiny
r3–10 mm bores are fastener pins in solids 2–9, ignored):

| Joint (inferred) | Axis | Line (mm) | Radius | Solids | Role |
|---|---|---|---|---|---|
| **J1 base yaw** | **Y** (vertical) | foot (0,0,0) | r 237–330 | {16,11,14} | turret rotation on the pedestal (solid 16) |
| **Arm top pivot** | **X** | (0,1815,305) | r 140–180 | {1,12} | head casting (1) ↔ long arm (12) |
| **Wrist roll** | **Z** | (0.7,2065,·) | r 48–140 | {0,1,15} | wrist assembly |
| **Wrist bend** | **X** | (0,2065,1580) | r 48–111 | {0,15} | wrist assembly |

Solid 12 is the **1425 mm main arm** and carries **two parallel X-axis pivots at Z=305**
— bottom (335,740,305) r170 to the lower casting (solid 13) and top (290.5,1815,305)
r145.5 to the head (solid 1). **Two parallel pivots 1075 mm apart on one link is the
parallelogram-bar signature**: solid 12 is one bar of the four-bar; a parallel passive
link closes the loop and holds the head orientation constant — the defining feature of
this picker. **GATE A3 (closed-loop residual < 1e-4) is therefore load-bearing.**
Per-solid exact B-Rep volumes give link masses (× material density); the structural link
volumes are base 86.1e6, arm 60.8e6, head 63.6e6, lower casting 41.6e6 mm³.

**Boundary call (rule 6):** STEP is geometry-only — it carries **no joint/DOF metadata**.
So the *joint identification* (which coaxial bore is a revolute vs a bolt, which links
form the parallelogram) is **inferred** from large-radius shared-bore clustering + the
twin-parallel-pivot signature; the *geometry* (axes, pivot points, link inertias) is
**exact** from the B-Rep. This is documented, not faked.

### M.2 DECISION — dynamics oracle: Eigen-native, constraint-aware (LANDED)
A self-contained `krs::dyn` oracle (`src/Physics/RobotDynamics.cpp`, MPL/BSD-clean, only
Eigen) is the analytical reference and the amendment-required independent FK check:
- FK = homogeneous SE(3) composition; geometric Jacobian; **RNEA** (spatial Newton–Euler
  inverse dynamics); **CRBA** mass matrix (independent of RNEA → genuine cross-check);
  forward dynamics q̈ = M⁻¹(τ−b); **DLS IK** (Buss) with e-/step-clamping + limit clamps;
  **loop-closure** machinery (frame-coincidence constraint residual + Newton solve of the
  dependent coords) → handles the parallelogram, i.e. it is **constraint-aware**, not a
  plain serial Featherstone.
- Self-test battery (`KRS_DYN_SELFTEST`, pure CPU): A1 FK vs closed-form RR + two-way FK
  agreement; CRBA-vs-RNEA mass-matrix cross-check; pendulum closed-form + RNEA↔ABA
  round-trip; A4 IK round-trip + clean unreachable handling; 4-bar loop-closure residual.

### M.3 DECISION — Pinocchio attempt (amendment step 3, time-boxed, behind a tag)
Safety tag **`phaseA-pre-pinocchio`** = `f44c2c4` created first (a hard reset never
touches the untracked STEP). Pinocchio has **no vcpkg port** (confirmed: `ports/pinocchio`
404), so "via vcpkg" = a source/overlay build. The genuine, decisive probe = an isolated
MSVC compile + FK smoke test (the real question is whether Pinocchio's templates compile
under cl.exe at all). Progress: cmake submodule ✓ → Eigen3 ✓ → classic-installed the
missing compiled Boost (filesystem/serialization) and header-only Boost (math/foreach/
fusion/…) → **configure passes**.

### M.3a Pinocchio attempt OUTCOME — reverted to the Eigen oracle (documented)
Genuine, bold attempt made (cleared cmake-submodule, Eigen3, compiled + header-only
Boost; configure passed). The build then reached Pinocchio's own compilation but kept
failing on a **long tail of missing Boost headers** (next: `boost/asio` → which pulls
`openssl`), each resolved only by ever-more Boost. **The decisive question — does
Pinocchio's template code compile under MSVC `cl.exe`? — was never reached:** the blocker
was always missing headers, never a template/overload error. Pursuing it via a full
`boost` classic install started building **OpenSSL** and, critically, **held the
process-global vcpkg lock that BLOCKED the primary KRStudio build**. Per the amendment's
rule ("if it blows the time-box or corrupts/**blocks** the build → revert + document"),
the attempt was stopped to unblock GATE A.
- **No `git reset` was needed**: the probe was fully isolated (vendored in
  `external/pinocchio` + `verify/`, never wired into the app or the tracked CMake), so the
  tracked tree was already clean; the `phaseA-pre-pinocchio` tag stands as the record.
- **Decision**: the Eigen-native **constraint-aware** oracle (§M.2, landed and validated
  to machine precision — FK 4.6e-16, CRBA-vs-RNEA 2.7e-15, pendulum 1.8e-15, IK 50/50,
  4-bar 2.2e-13) **is** the Phase-A reference. Pinocchio remains an optional-later add-on
  (overlay/clang-cl toolchain), never on the default MSVC build — exactly the web recon's
  recommendation. This is a documented boundary call, not timidity: the attempt was made
  and the failure mode (Boost-header tail + vcpkg-lock contention, not a Pinocchio-MSVC
  incompatibility per se) is recorded.

### M.4 DECISION — simulated articulation + closed loop
`PxArticulationReducedCoordinate` is the simulated plant, built with the **real extracted
FANUC axes** (J1 = Y at origin; arm pivot = X at (0,1815,305); wrist = Z/X near
(0,2065,·)) and link inertias from the exact solid volumes. The reduced-coordinate tree
is a spanning tree (cut one parallelogram joint); the loop is closed with a **`PxD6Joint`**
(revolute → lock 5, free TWIST) at the cut pivot, per the PhysX recon. Torque→accel uses
the cache protocol (create cache after addArticulation; `jointForce[dof]=τ`,
`applyCache(eFORCE)`, reapply each step; read via `computeJointAcceleration` /
`copyInternalStateToCache(eACCELERATION)`), validated against the oracle's ABA.

### M.5 GATE A acceptance (real measured numbers required)
- **A1** PhysX FK vs oracle FK over 100 random configs (oracle is the analytical ref since
  Pinocchio is not a guaranteed dep; cross-checked two independent ways at 1e-12 + closed
  form). PhysX (single-precision) bound ~1e-4.
- **A2** revolute on a *detected cylinder axis*: 90° sweep, a body point's deviation from
  the ideal circle about the exact extracted axis < 0.1 mm.
- **A3** parallelogram loop residual < 1e-4 across motion (oracle Newton-close + PhysX D6).
- **A4** IK round-trip FK(IK(pose))≈pose < 1e-4 over 50 targets + clean unreachable (no NaN).
- **A5** commanded CAN torque → joint accel matches oracle ABA/RNEA < 1% (and PhysX
  analytical utilities cross-checked).

### M.6 DECISION — retire the Phase-2 CAN fake
CAN effort frames route to articulation **joint torques** (`cache.jointForce[dof]`,
`applyCache(eFORCE)`) instead of `PxRigidDynamic::addForce`, and state is published from
**joint encoders** (`jointPosition`/`jointVelocity` from the cache). `JointComponent`s are
built from the detected `AttachmentComponent` cylinder anchors (axis → revolute).

### M.7 GATE A — PASSED (real measured numbers, 2026-06-14)
Two self-test tracks, both green. **Oracle track** (`KRS_DYN_SELFTEST`, machine precision):
FK vs closed-form **4.6e-16**; FK two-way **0**; CRBA-vs-RNEA mass matrix **2.7e-15**;
pendulum vs closed form **1.8e-15**; RNEA↔ABA round-trip **2.7e-14**; IK round-trip
**50/50 < 1e-4** + clean unreachable (no NaN); 4-bar loop closure **2.2e-13**.
**PhysX plant track** (`KRS_ARTIC_SELFTEST`, PxArticulationReducedCoordinate vs oracle):
- **A1** FK PhysX vs oracle, 100 random cfg, 4-link FANUC-axes chain: maxPos **1.17e-06 m**,
  maxRot **6.4e-07 rad**  (bound 1e-4).
- **A2** revolute on the *detected* FANUC arm-pivot axis (X @ (0,1.815,0.305)), 90° sweep:
  point deviation from analytic **1.40e-06 m**, radius variation **1.59e-06 m**  (bound 0.1 mm).
- **A3** parallelogram closed loop via `PxD6Joint` (position pin: 3 translations locked,
  rotations free — locking the planar Z-rotation over-constrains it), residual across
  motion **3.39e-05 m**  (bound 1e-4).
- **A5** commanded joint torque → joint acceleration (`computeJointAcceleration`) vs oracle
  forward dynamics: max relative error **7.6e-07**  (bound 1%).

Regression stayed green throughout: `KRS_BENCH` **7/7**, `KRS_FEM_SELFTEST` ALL PASS,
`KRS_MPM_SELFTEST` 19/19, `ADJOINT_GRADIENT_CHECK` ALL PASS, HIL bridges ALL PASS.

### M.8 Remaining Phase-A app integration (follow-ups)
The numerical gate is met. Wiring the validated articulation into the *live* app —
building a `PxArticulationReducedCoordinate` for robot entities in
`SimulationController::buildPhysicsWorld`, emitting `JointComponent`s from the detected
`AttachmentComponent` anchors, and routing live CAN effort → `cache.jointForce` (retiring
the Phase-2 `addForce` fake) — is the next sub-step, gated behind GATE A (now passed).
OMPL (vcpkg 1.7.0) is queued for Phase B planning.

### M.9 Adversarial review + hardening (2026-06-14)
A 53-agent adversarial review (5 dimensions → 3-vote refute → synthesize) probed the
oracle + gate. **No correctness defects survived**: every math concern was refuted and
empirically validated by a passing gate — spatial-inertia symmetry, the Xmotion sign
convention, the QXto joint-frame mapping (A1 passes with X/Y/Z axes), the DOF-index
assumption (A1 passes ⇒ mapping correct), `computeJointAcceleration` gravity/Coriolis
semantics (A5 7.6e-7), and the NaN/LDLT guards. 36 findings dismissed. The 12 confirmed
were all test-coverage / honesty gaps, now CLOSED:
- **Jacobian never validated directly** → added Jacobian-vs-FK finite-difference check
  (maxErr 7.2e-7).
- **Prismatic / fixed / branching joints barely tested** → property/fuzz battery over 60
  random branching trees mixing revolute+prismatic+fixed (FK two-way 0, CRBA-vs-RNEA
  3.6e-15, RNEA↔ABA 5.3e-14).
- **Robustness gaps** → mass-scaling 1e6×/1e-6× (7.1e-15) + near-singular IK stays finite.
- **Loose thresholds** → A1 tightened 1e-6 → 1e-12 (measured 4.6e-16).
- **[MEDIUM] "real FANUC parallelogram not modeled"** → A3 rebuilt on the REAL extracted
  FANUC geometry (arm bar 1.075 m between the two parallel X-pivots, anchored at the real
  lower pivot (0,0.74,0.305), cut joint pinned to the real top pivot (0,1.815,0.305)) +
  an oracle-vs-PhysX FK cross-check on the dynamically-settled constrained state
  (residual 3.9e-7 m, settled-FK 6.8e-7 m / 2.4e-7 rad). The full 4-bar's coupler width /
  2nd ground pivot remain a documented boundary call (not cleanly extractable from the
  bolt-vs-bearing-ambiguous bores).

### M.10 KRS_OVERNIGHT_BENCH — consolidated dashboard
`KRS_OVERNIGHT_BENCH=1` runs every headless gate and prints one dashboard with a process
exit code (= #failed groups). Current: **9/9 gate groups PASS** (Phase A oracle, Phase A
articulation gate, FEM, MPM, adjoint, HIL jitter, HIL bridges, trajectory verify, OCCT) +
rigid `KRS_BENCH` 7/7 separately.

## N) Phase F — Graphics correctness + strict render gates (2026-06-14)

### N.0 Reconnaissance (6-agent parallel recon, wf_b92a02fe)
- **Offscreen context already exists**: `RenderingSystem::m_engineContext` (+ `m_engineSurface`
  QOffscreenSurface) — all rendering runs headless on it; widgets only blit. The final image
  is `target.finalFBO` → `finalColorTexture` (**RGBA16F**) + `finalDepthTexture`.
  `publishHilCameraFrame` (RenderingSystem.cpp:825) is the `glReadPixels(...,GL_RGBA,GL_FLOAT,...)`
  template. Pipeline: geometry → lighting → postProcessing → overlay(skybox, MpmPass, FemVizPass,
  Fluid, Glass, Smoke, **TonemapPass**, Gizmo).
- **CRITICAL for decode**: `MpmPass`/`FemVizPass` run BEFORE `TonemapPass`, so the final PNG is
  `ACES_tonemap(ramp)` — the colormap is distorted. **Field-decode gates must capture the HDR
  buffer pre-tonemap (flat viz).**
- **Colormap** `ramp(t)` (mpm_render_vert.glsl:30-44 ≡ fem_viz_frag.glsl:8-22): 6 stops, 5 linear
  segments of 0.2. Channels are NOT individually monotonic → inverse decode = nearest-t search
  over sampled ramp. `t = clamp((scalar−rangeMin)/max(rangeMax−rangeMin,1e-6),0,1)`.
- **F1 (real data)**: the recon CONFIRMS MPM (Thermal=plastic.y temperature, Strain=‖E‖,
  VonMises=StVK invariant of E=½(FᵀF−I)) and FEM (nodal vonMises/temp/strain) viz fields are
  **REAL solver state, not synthetic**. Only fabrication: `FemSystem.cpp:170` constant-20 °C
  ambient fallback when thermal is absent (→ fix: don't fabricate; mark unavailable).
- **F2 (jitter)**: SOLE source is `MpmSystem::autoCalibrate()` range-cycling every 45 frames
  (MpmSystem.cpp:262) during PLAY — hue pops as the range snaps. Splat order is deterministic
  (SSBO order, fixed-seed jitter at seed only), blending OFF (opaque depth-write). PAUSED already
  does not recalibrate (only on mode-switch) ⇒ paused is already frame-stable.
- **F3 (z-fight)**: `FemVizPass` redraws the SAME triangles as `OpaquePass` at IDENTICAL depth
  (GL_LEQUAL, **no polygon offset**) → coincident-depth fighting.

### N.1 DECISIONS
- **F0 harness** (`KRS_RENDER_SELFTEST`): a `RenderingSystem` method renders a fixed scene + fixed
  camera to a dedicated FBO on the engine context; captures the **pre-tonemap HDR buffer** for
  field decode (flat-viz uniform → pure `ramp(t)`, no diffuse/tonemap) and the final image for
  determinism/projection; PNG via stb_image_write; inverse-colormap decode = nearest-t over 1024
  ramp samples. G1–G9 measured headlessly; folded into `KRS_OVERNIGHT_BENCH`.
- **F2**: add `Appearance::freezeRange` (gates/deterministic playback freeze the range to known
  bounds) + EMA-blend the periodic play-mode recalibration toward the target (α≈0.2) so hue no
  longer pops every 45 frames; paused calibration stays exact (single shot). Add `setVizRange`/
  `setVizRangeFrozen` so the harness pins an exact known range for decode.
- **F3**: `glPolygonOffset(-1,-1)` (+ `GL_POLYGON_OFFSET_FILL`) on the FemVizPass overlay so the
  field layer reliably wins the depth test against the coincident base mesh — no fighting.
- **F1**: replace the FemSystem 20 °C thermal fallback with an unavailable-marker (don't recolor
  what wasn't solved) so a missing thermal solve reads as "no data", not a fabricated cold field.

## N.2) Recovery checkpoint + reconciliation (2026-06-14, post-reboot)

A Windows update rebooted the machine mid-sprint and killed the agent thread. This checkpoint
re-establishes ground truth from the **repo + gate results**, not prior reports or memory.

- **Protect**: the 83 unpushed commits were pushed to origin (origin/master == HEAD == `5eec994`).
- **Clean rebuild**: `cmake --build --preset release --clean-first` → `BUILD_EXIT=0` (healthy;
  reused `vcpkg_installed`, recompiled all first-party + in-tree deps).

**Gates re-run on the freshly-rebuilt binary (real measured numbers):**
- `KRS_OVERNIGHT_BENCH` = **9/9 gate groups PASS, exit 0** (Phase A oracle, Phase A articulation,
  FEM, MPM, adjoint <1e-5, HIL jitter, HIL bridges, trajectory verify, OCCT).
- `KRS_DYN_SELFTEST` ALL PASS: A1 FK 4.578e-16; two-way 0; CRBA-vs-RNEA 2.665e-15; pendulum
  1.776e-15; RNEA↔ABA 2.731e-14; IK 50/50 (worst 1.76e-07); 4-bar 2.167e-13; Jacobian-FD 7.237e-07;
  60-tree branching fuzz PASS; mass-scaling 7.105e-15; near-singular IK finite.
- `KRS_ARTIC_SELFTEST` ALL PASS: A1 maxPos 1.168e-06 m / maxRot 6.401e-07 rad; A2 maxDev 1.396e-06 m
  / radiusVar 1.589e-06 m; A5 torque→accel maxRel 7.575e-07; A3 FANUC parallelogram residual
  3.944e-07 m (settled-FK 6.838e-07 m / 2.384e-07 rad).
- `KRS_BENCH` rigid = **7/7** (fall 0.85%, bounce apex 0.10%, static-friction stick, kinetic-friction
  slide, projectile 0.31%, fluid column 4.98%, concave cup rest 0%).
- Observation (non-blocking, pre-existing): `PhysXGpu_64.dll` fails to load (err 126) → CPU PhysX
  fallback; every gate passes regardless. Follow-up: deploy the GPU DLL into `build/release`.

**Reconciliation — two-sprint plan vs. the actual repo (8-agent verification, wf_f202bfbb):**

| Phase | Status | Evidence / resume point |
|---|---|---|
| F0–F3 render correctness | **COMMITTED-NOT-GATED** | F1/F2/F3 fixes in `5eec994` (`FemSystem.cpp:170`, `MpmSystem.cpp:739-784`, `FemVizPass.cpp:36`). **F0 harness `KRS_RENDER_SELFTEST` + G1–G9 absent from source** (only in §N.1). ← **RESUME** |
| G live FANUC articulation (#116) | NOT-STARTED | `SimulationController.cpp:836` still `PxRigidDynamic::addForce` (Phase-2 fake). No `PxArticulation` in `buildPhysicsWorld`; `JointComponent` declared, never emitted. GATE A passed; GATE H (live) unrun. |
| H Qt UI completeness | NOT-STARTED | 25 UI components; no control manifest / fuzz / coherence harness (GATE U absent). |
| I OMPL planning (stretch) | NOT-STARTED | `ompl` not in `vcpkg.json`; no includes. |
| J reflection + ECS | NOT-STARTED | EnTT registry exists; every object IS an entity. `entt::meta` / round-trip absent (GATE R unrun). |
| K auto parameter UI | NOT-STARTED | All panels hand-coded; no metadata-driven generator (depends on J). |
| L MQTT self-broadcast | NOT-STARTED | No mosquitto/paho dep; orphan `mqtt_client_button` UI stub only. |
| M MQTT command round-trip (stretch) | NOT-STARTED | Depends on L. |

This confirms the expected picture: committed through `5eec994` = Phase A gated + Phase F fixes +
recon §N; F0/G1–G9 unconfirmed (now confirmed **absent**); Phases G–M unstarted.

**Hygiene**: `assets/FANUC-430 Robot.STEP` committed (load-bearing for Phase A/G; `.gitignore` excludes
meshes/HDR but not `.STEP`). `external/pinocchio/` + `verify/` are Phase-A probe artifacts → gitignored.
`external/SPlisHSPlasH` untracked content = generated `Utilities/Version.h` (benign build artifact).

**Resume point**: PART 1 — build the F0 headless render harness + inverse-colormap decode, run gates
G1–G9 to green (§N.3, next), then Phase F counts as landed and the G–M arc proceeds in order.

## N.3) Phase F GATE G — F0 render harness + G1–G9 (2026-06-14, LANDED + GATED)

F1/F2/F3 were committed in `5eec994` but unlocked by no gate; F0 (the headless render self-test)
and G1–G9 did not exist in source. Built now.

**F0 harness** (`src/Rendering/RenderGates.cpp`, `RenderingSystem::runRenderGates()`): env var
`KRS_RENDER_SELFTEST` (standalone, `_Exit(#fail)`) + folded into `KRS_OVERNIGHT_BENCH` as a 10th
gate group. Renders a deterministic scalar-gradient mesh through the **real `fem_viz` colormap
shader** (the encoding under test) to a private RGBA16F FBO on the engine context, reads back via
the `publishHilCameraFrame` `glReadPixels` pattern, and inverse-decodes with a 1024-sample nearest-t
ramp LUT (a CPU mirror of the GLSL `ramp()` — G2 is the drift canary). Writes
`render_gate_gradient.png` for inspection.

**Scope (honest)**: these gates lock the field→colour ENCODING + range normalization + depth-bias +
camera projection — the primitives F1/F2/F3 touch. Field VALUES are gated separately by
`KRS_FEM/MPM_SELFTEST`; representation under test = the fem_viz mesh recolour (MPM splats share the
byte-identical `ramp()`). A live-solver-field decode through the MPM splat path is the documented v2
extension.

**GATE G — PASS (real measured numbers):**
- **G1** determinism: render twice, maxAbsPixelDiff = **0** (bit-exact).
- **G2** decode fidelity: inverse-decode recovers the known t, maxAbs(dt) = **0.0006** over 3 ranges
  ([0,1], [0,2], [-1,1]); bound 0.02.
- **G3** jitter/freeze: 4 frozen renders, variance = **0**; `freezeRange` pins the range (F2).
- **G4** projection⊂mask: **100%** of a 33×33 projected grid lands in the silhouette; bound 90%.
- **G5** z-fight: coincident overlay under `GL_LESS` bleeds **100%** without bias, **0%** with
  `glPolygonOffset(-1,-1)` (F3) — sensitivity proven; bound 0.1%.
- **G6** mode-switch: doubling the range halves decoded t in the SAME render (0.813 → **0.407**).
- **G7** camera ±1px: the real `Camera` projects the focal point to screen centre, err **0.000 px**.
- **G8** colormap monotonic: decoded t strictly non-decreasing along the gradient, **0** inversions
  over n=410 (Spearman ρ = 1).
- **G9** golden-by-spec: known-t sample pixels equal `ramp(t)`, maxColErr = **0.0006**; bound 0.0136.

Overnight bench with the gate folded in: **10/10 groups PASS** (exit 0).

**Observation (pre-existing, not Phase F)**: `HIL jitter` is a wall-clock 1 kHz loop test on a
non-realtime OS; one run tripped on a single max-tick outlier (1.94 ms > 1 ms budget; p99 fine at
0.02 ms) under build load, then passed **3/3** on re-run (max 0.18–0.32 ms). The absolute-max
threshold is outlier-sensitive; a p99.9 / allow-N-outliers bound would be more robust. Tracked as a
HIL follow-up, independent of the render gates.

**Adversarial review (18-agent, wf_7a2b4085) — findings fixed, false-positives documented:**
- **G7 was circular** (focal point projects to centre *by construction* for a look-at camera) → replaced
  with a non-circular check: CPU-project the quad's world corners via `P_ortho` and compare to the
  *rendered* silhouette bbox (GPU transform vs CPU projection), cornerProjErr **0.200 px**. This surfaced
  two pre-existing **Camera bugs** (out of Phase F scope, flagged as follow-ups): (1) `forceRecalculateView`
  encodes yaw as `atan2(dir.x,dir.z)` but `updateCameraVectors` rebuilds `front` as `(cos yaw, …, sin yaw)`
  — an x/z swap, so it *relocates* the camera instead of preserving the requested pose; (2) `m_IsPerspective`
  is uninitialized (`Camera.hpp:85`).
- **G1 anti-vacuous**: a silently-broken shader yields an all-clear FBO that is trivially "deterministic" →
  now requires non-blank foreground (168100 px) + real colour content.
- **G5**: replaced the base-vs-overlay distance heuristic with a direct "overlay won at every pixel" check
  (tol ≫ RGBA16F quantisation); sensitivity baseline now 99.3% (no-offset) vs 0% (with bias).
- **G3** full `Appearance` save/restore; **G2** prints pixel count; **G9** fails if the PNG write fails;
  GL depth/offset/clearColor restored on exit.
- **Refuted (correctly, no change)**: VAO/VBO "leak" (created after the FBO-completeness check), missing
  `glFinish` (`glReadPixels` is synchronous), `glClearColor` leak (matches the app baseline + `_Exit`),
  G5 depth-buffer persistence (same FBO bound throughout), G2 LUT-coarseness (range applied identically).

**Phase F is LANDED + GATED.** Next: Phase G — live FANUC articulation (GATE H).

## O) Phase G — live FANUC articulation (GATE H) — DESIGN (2026-06-14)

GATE A (§M.7) validated a *throwaway* `PxArticulationReducedCoordinate` built inside
`runArticulationGate`. Phase G wires articulation into the **live** `SimulationController`
sim and validates THAT path against the oracle.

### O.0 Recon (5-agent, wf_65031377)
- Live path today: `buildPhysicsWorld` (SimulationController.cpp:462) makes a `PxScene`, ground,
  then one `PxRigidDynamic` per `RigidBodyComponent` (no articulation). CAN effort → `addForce`
  (the Phase-2 fake, line 836). State published from body pose/vel (`publishCanState`, 846).
- Validated recipe to REUSE: `ArticulationGate.cpp:75-100` `buildArtic` + `GateSpec`/`makeOracle`
  (the §M.1a FANUC chain: J1 Y-yaw @ origin; lower X-pivot (0,0.74,0.305); arm-top X-pivot;
  wrist Z) + the A3 parallelogram `PxD6Joint` close (lock 3 translations, free rotations).
- **Joint-tree-assignment is ambiguous to auto-detect** (STEP axis clusters need user
  disambiguation — recon). **DECISION (boundary call, consistent with §M.1a/GATE A)**: assemble
  the live joint tree from the **validated detected-axis spec**, not the ambiguous auto-clusterer.
  Emitting `JointComponent` into the scene-graph from CAD anchors + a selection UI is a Phase-H/UI
  follow-up; the GATE-H numerical contract is met by the validated spec.

### O.1 Architecture
- Move the validated recipe (`GateSpec`, `makeOracle`, `buildArtic`, helpers) into a shared
  header so both `ArticulationGate.cpp` (GATE A) and the live path use ONE source of truth.
- `SimulationController`: `PxImpl` gains `articulation` + `articCache` + `articLinks` +
  `axisToDof` + `loopD6`. New `buildArticulation()` (called from `buildPhysicsWorld` when a
  `RobotArticSpec` is set via `setRobotArticulationSpec`) builds the live articulation in the
  live `PxScene`; it is stepped by the existing `tick()`/`stepOnce` loop automatically.
- PhysX-free accessors for the gate: dof count, set joint positions, link world poses,
  command joint torques, joint accelerations, loop residual.
- CAN: `applyCanCommands` routes an articulated actuator's effort → `cache.jointForce[dof]` +
  `applyCache(eFORCE)` (retiring the `:836 addForce` fake for the robot); `publishCanState`
  reads `cache.jointPosition`/`jointVelocity`.

### O.2 GATE H contract (real measured numbers, not relaxed)
- **H1** FK of the LIVE assembled tree vs oracle FK < 1e-4 over ≥50 random configs.
- **H2** a commanded CAN torque → live joint accel vs oracle ABA < 1%.
- **H3** the FANUC parallelogram loop stays closed (residual < 1e-4) in the LIVE sim during motion.
- **H4** regression: `:836 addForce` fake retired with no silent breakage — `KRS_OVERNIGHT_BENCH`
  stays 10/10, HIL suite green.
- Harness `runArticulationLiveGate` (KRS_ARTIC_LIVE_SELFTEST) builds a `SimulationController` +
  Scene, plays (→ live articulation), drives the live path, compares to the oracle; folded into
  `KRS_OVERNIGHT_BENCH` as an 11th group. Adversarial review before H is declared closed.

### O.3 Implementation prerequisite (FOUND during planning — must do FIRST)
`SimulationController::PxImpl::ensureCore()` (SimulationController.cpp:116) calls
`PxCreateFoundation` **per instance**, but PhysX allows only **one `PxFoundation` per process**.
The app creates one at startup, so the GATE-H live harness's second `SimulationController` would
fail at `PxCreateFoundation` (GATE A dodges this by reusing `PxGetPhysics()`). **Resolution**:
`ensureCore` must BORROW the process-wide PhysX singleton when one already exists (first
controller owns + releases foundation/physics; later ones borrow + skip release). This touches
the running app's core lifecycle → land it as its own commit with `KRS_BENCH`/overnight green
before any GATE-H code.

### O.4 Resume plan (gated sub-steps, each commit+push, phaseG-pre is the fallback tag)
- **G.0** ✅ LANDED — `ensureCore` borrows the process-wide PhysX singleton (first controller
  creates+owns; later ones borrow; shared core + `CollisionCookingService` torn down only when the
  last holder goes, via refcount). Per-instance dispatcher/CUDA kept per-instance. Gates: **G0a**
  overnight **11/11** (original 10 unregressed + new lifecycle group) + `KRS_BENCH` **7/7**; **G0b**
  `KRS_SIM_LIFECYCLE_SELFTEST` PASS — 12 create/destroy cycles balanced, 2 coexisting controllers
  with A torn down first leaving B + core valid and still stepping, refcount returns to base (no
  double-free/leak). baseRefs=1 confirms the borrow path was exercised.
- **G.1** ✅ LANDED — POD `ArticulationSpec.hpp` + `SimulationController::buildArticulation`
  (built in `buildPhysicsWorld` from a `RobotArticSpec`; zero-config link poses = chained
  `(Rtree,ptree)`; PhysX-free accessors for the harness) + `runArticulationLiveGate`
  (`KRS_ARTIC_LIVE_SELFTEST`). **H1 PASS**: live FANUC FK vs oracle over 60 cfg —
  maxPos **1.323e-06 m**, maxRot **6.054e-07 rad** (bound 1e-4); overnight 11/11 (no regression).
- **G.2** ✅ LANDED — live parallelogram `PxD6Joint` close in `buildArticulation` (built before
  `addArticulation`; lock 3 translations, free rotations) + shared guarded `ensurePhysxExtensions`
  (one `PxInitExtensions`/process, used by GATE A + the live path) + `setSceneGravity`. **H3 PASS**:
  live parallelogram loop residual **3.219e-07 m** across a 3-crank sweep under live stepping
  (bound 1e-4); GATE A unregressed (A3 still 3.944e-07), overnight 11/11.
- **G.3** ✅ LANDED — `applyCanCommands` routes an articulated robot's effort → `cache.jointForce`
  (axis = DOF) + `applyCache(eFORCE)`, **retiring the `:836 addForce` fake** (kept only as the legacy
  free-rigid-body path, inert when an articulation exists); `publishCanState` reads joint encoders from
  the cache. **H2 PASS**: CAN torque (cancodec round-trip) → live joint accel vs oracle ABA, maxRel
  **9.271e-07** over 20 cfg (bound 1%, on par with A5's 7.6e-7). **H4 PASS**: overnight **12/12** (GATE H
  folded in as a standing group) + `KRS_BENCH` 7/7 + HIL green — no regression from retiring the fake.
- **G.4** ✅ LANDED — 8-agent adversarial review (wf_a9bc1195). Gate honesty **verified**: H1/H2/H3
  genuinely exercise the live `buildArticulation` path (a broken build fails them), the `dof`/`h?ran`
  guards prevent vacuous passes, and the `:836` fake is genuinely retired. 2 MEDIUMs **refuted**
  (cache-release order is correct per PhysX docs — `articulation->release()` doesn't touch the cache;
  H2 feeds oracle + PhysX the SAME quantised torque). 2 HIGH resource leaks **confirmed + fixed**:
  (1) `buildArticulation` now releases a stale articulation before rebuilding (no leak on a double
  `buildPhysicsWorld`); (2) `PxCloseExtensions()` now called in `releaseCore` at refs==0 (matches
  `PxInitExtensions`; pre-existing since GATE A). Re-gated: GATE H green, lifecycle green, overnight
  12/12, bench 7/7. **GATE H CLOSED.**

**Phase G COMPLETE.** Next in the arc: Phase H — Qt UI completeness (GATE U).

### GATE H — PASS (live FANUC articulation, real measured numbers)
- **H1** live FK vs oracle, 60 cfg: maxPos **1.323e-06 m** / maxRot **6.054e-07 rad** (bound 1e-4).
- **H2** CAN torque (codec round-trip) → live joint accel vs oracle ABA, 20 cfg: maxRel **9.271e-07** (<1%).
- **H3** live parallelogram loop (PxD6, 3 cranks): residual **3.219e-07 m** (bound 1e-4).
- **H4** `:836 addForce` fake retired; overnight **12/12**, KRS_BENCH 7/7, HIL green — no regression.
The articulation is built by `SimulationController::buildArticulation` in the live `buildPhysicsWorld`,
stepped by the real sim loop, and driven by the live CAN path — not the GATE-A throwaway.

## P) Phase A — default FANUC sandbox demo (GATE D) — DESIGN (2026-06-14)

New three-phase directive: (A) FANUC sandbox demo, (B) B-Rep feature selector (highest bar),
(C) joint/mate tooling (highest bar). CANONICAL-GRAPH RULE: all joints/mates write into the
`RobotArticSpec` -> `SimulationController::buildArticulation` graph that GATE H validated; no
parallel joint representation.

### P.0 Recon (5-agent, wf_54e10eb7)
- Boot scene: `MainWindow.cpp:307` Scene, `:313` SimulationController, `:419-444` the current
  6061-Al block + heater (the viz target to replace). `play()` -> `buildPhysicsWorld` ->
  `buildArticulation` if `m_hasRobotSpec`.
- Command interface (Phase-G established): `commandJointTorques` -> `cache.jointForce` ->
  `applyCache(eFORCE)`, drained from CAN in `tick()`. Recon recommends a gravity-comp
  computed-torque/PD controller via the Eigen oracle; recommends AGAINST PhysX joint drives
  (hide the true torque, can't cross-check vs oracle).
- Oracle (`krs::dyn::SerialChain`): `biasForces(q,qd,g)=C·qd+g`, `massMatrix`, `closeLoops`,
  `fk`, `loopResidual`. Solver iters 64/16.

### P.1 DECISIONS
- **Boot FANUC = the validated A3 parallelogram** (3 bars + D6 loop). The A3 loop pins the tip
  to a FIXED world anchor — valid only for a fixed base, so base-yaw + a rotating-anchor loop
  is a **documented boundary call** (the §M.9 coupler / 2nd-ground-pivot limitation). The
  pick-and-place = the crank sweeping the arm between two loop-consistent tip positions on its arc.
- **Controller** (the established torque interface, no new control surface): command both
  configs as loop-consistent targets via `oracle.closeLoops(crank)`; per step
  `tau = biasForces(q,qd,g) + Kp·(q_cmd−q) − Kd·qd` on all joints, fed to `commandJointTorques`.
  New readback accessors `articJointPositions/Velocities` (copyInternalStateToCache) for the PD feedback.
- **GATE D harness** `runDemoGateD` (`KRS_DEMO_SELFTEST`, folded into KRS_OVERNIGHT_BENCH as a
  13th group): D1 ≥1000 cycles, no NaN, loop residual <1e-4 every step of the actual motion;
  D2 tracking (achieved vs commanded within a stated tol); D3 no resource growth
  (GetProcessMemoryInfo before/after); D4 determinism (two runs identical trajectory).
- **A1 boot wiring**: `MainWindow` sets the FANUC `RobotArticSpec` on the app sim + a demo
  controller drives the cycle during Play (same controller as the gate). The gate is the
  numerical contract; the boot scene is the visual wrapper over the same canonical graph.

### GATE D — PASS (FANUC sandbox demo stability, real measured numbers)
Driven through the established `cache.jointForce` interface by a crank-PD + Coriolis-comp
controller (PD on the independent crank DOF; dependent bars follow the D6 with light damping;
zero gravity isolates the loop as GATE-H H3 does). The articulation is the canonical
`buildArticulation` graph (rule 7).
- **D1** stability, 1000 cycles, no NaN, loop residual <1e-4 **every step of the motion**:
  maxRes **1.038e-06 m** (bound 1e-4).
- **D2** reach accuracy at each commanded config (settled): reach-err **6.96e-05 rad** (bound 0.02);
  peak dynamic lag during the fast 80-step move **0.233 rad** (reported, not gated).
- **D3** no resource growth over 20 build/run/teardown: ΔworkingSet **0.08 MB** (bound 8).
- **D4** determinism: two runs reproduce an identical trajectory checksum (bit-equal).
Folded into KRS_OVERNIGHT_BENCH as a 13th standing group (**13/13**). KRS_DEMO_SELFTEST standalone.
Boundary call: fixed base (the A3 fixed-anchor loop is base-yaw-incompatible — §M.9); base rotation
with a rotating-anchor loop is a follow-up.

### GATE D — adversarial review (wf_edfefdf7) closed
Fixed CONFIRMED findings: (1) [critical] dropped the oracle `biasForces` feedforward — the open-chain
Coriolis is invalid for the D6-constrained DOFs; the controller is now pure PD-on-crank + light
dependent damping (honest, sufficient at zero-g). (2) [high] D4 checksum is now FULL-STATE (all q + qd
every step, not just link-2 position). (3) [high] D3 strengthened to 100 build/run/teardown cycles,
bound 4 MB (catches ~40 KB/build) — measured 1.21 MB. (4) [med] D1+D2 now require PROOF OF MOTION
(crank sweep range ≥ 0.30 rad) so a stationary/broken robot fails — the guard already caught that the
fast-cycle crank reaches 0.822 (dynamic lag), proving the assertion bites. Refuted (no change): D2 0.02
rad bound (intentional reach margin), articLinkPoses off-by-one (correct: poses[i]=link i+1), working-set
volatility (mitigated by 100 cycles + warm-up; the rebuild-leak class is also guarded by the G.4 fix +
the lifecycle gate). Re-gated ALL PASS: D1 1.038e-06 m, D2 6.96e-05 rad, D3 1.21 MB, D4 bit-identical.

### GATE D — proof-of-motion guard is non-vacuous (demonstrated, not asserted)
The D1 motion guard switched from absolute reach (crank >= 0.90) to **sweep range >= 0.30 rad** for a
documented reason: in D1's FAST cycles (8 dwell steps) the soft PD trails the command, so the crank tops
out at **0.822, not the commanded 1.00** — the SAME dynamic lag D2 reports as a 0.233 rad peak. This is a
known demo-controller characteristic (a compliant PD on a heavy 1-DOF mechanism lags a fast command); the
robot reaches the full config when given time (D2 long-dwell reach-err 6.96e-05 rad). The threshold is NOT
silently tuned to pass: a **built-in NEGATIVE CONTROL** runs the harness with the drive disabled (frozen
articulation parked at the seed) and asserts its sweep is rejected — measured **frozenSweep = 0.000e+00 ->
guard REJECTS**, while the live sweep is 0.422. D1 requires BOTH (live sweep >= 0.30 AND the frozen robot
fails the guard), so a stationary/broken robot fails D1. No other bound changed: stability 1.038e-06 m,
loop residual <1e-4, leak 0.93 MB/4 MB (100 builds), determinism bit-identical — all exactly as before.

## Q) TOPOLOGY CORRECTION — FANUC-430 is a SERIAL 6-DOF arm, NOT a parallelogram (2026-06-14)

Phase A inferred a 4-bar parallelogram from a "twin parallel X-pivot signature" and built a PxD6 loop
closure (A3/H3). RE-CHECKED from the assembled STEP geometry (KRS_STEP_INSPECT, 17 solids): **the model
is a serial 6-DOF arm with a J2 counterbalance strut — there is no kinematic loop.**

**Evidence (shared coaxial bearings spanning >=2 solids):**
- Arm region has EXACTLY ONE large shared pivot: `(0,1815,305) dir X, r[140..180], solids{1,12}` = the
  ELBOW (J3), forearm(solid 1) <-> upper-arm(solid 12). No second parallel arm-bar pivot exists.
- Solid 12's two parallel X-pivots are its OWN joints at each end: shoulder (Y=740, r=170, to the base)
  and elbow (Y=1815, to forearm) — the ordinary signature of a serial link, not a 4-bar coupler.
- Base J1: `(0,0,0) dir Y, r=237.5, solids{11,14,16}` (vertical yaw). Wrist: `(0.7,2065,0) dir Z,
  r[48..140], solids{0,1,15}`.
- The angled cylinders on solids 1 & 12 (`dir(0,-0.174,-0.985)`, r=11.25/20) are the COUNTERBALANCE
  strut from the J2 area to the J3 link — a gravity-offset member.

**Discriminating test (cycle / DOF):** removing the strut leaves the arm's DOF unchanged (it is rigidly
determined by the link it assists; it offsets shoulder gravity torque, adds no DOF and closes no cycle).
A true 4-bar would lose end-effector DOF on bar removal. => SERIAL.

**Reconciliation:** the A3/H3 PxD6 "loop closure" was REDUNDANT; its <1e-4 residual proved the D6
machinery works, NOT that the FANUC has a loop. Corrections:
1. GATE H is now the loop-free SERIAL chain: H1 live FK vs oracle <1e-4, H2 CAN torque->accel <1%
   (the A1 4-link serial spec, already validated at 1.17e-6 — that re-pass IS the proof it was always
   serial). H3 (parallelogram) removed from the FANUC live gate.
2. GATE D demo runs on the serial chain (D1 = live FK vs oracle <1e-4 throughout the motion, replacing
   the fictional loop residual).
3. The PxD6 loop CODE + a generic capability test (GATE A "A3") are retained for FUTURE closed-chain
   mechanisms / Phase C user-created loops, explicitly marked NOT the FANUC topology.
4. URDF export of the FANUC is therefore LOSSLESS (no closed loop).
5. (Deferred) the strut's real effect is a pose-dependent assistive torque about J2 (user-supplied
   spring rate/preload/geometry) — affects motor EFFORT, not trajectory. NOT reintroduced as a D6.
Phase V solid->link assignment proceeds on the corrected serial structure (strut = rigid member of J3).

### Q.1 IMPLEMENTED — gates re-run loop-free on the serial structure (2026-06-14, real numbers)

The correction landed; every gate re-passes on the serial chain with no loop. Measured this session:

- **GATE H (serial, H1+H2):** H1 live FK vs oracle (60 cfg, 4-link) `maxPos=1.323e-06 m  maxRot=6.054e-07 rad`;
  H2 CAN torque->accel vs ABA (codec round-trip) `maxRel=9.271e-07` (<1%). H1 is BIT-FOR-BIT the value it
  had before H3 was removed — **removing the parallelogram loop did not change FK**, the direct proof the
  loop was redundant. `ALL PASS (H1,H2 serial)`.
- **GATE D (serial, D1-D4):** D1 live FK vs oracle THROUGHOUT 1000 pick/place cycles `maxFkErr=1.352e-06`
  (a pure kinematic identity now — link pose and q read at the same pre-step instant, no integration lag
  conflated in), elbow sweep `0.427 rad` (proof of motion, >=0.30); negative control `frozenSweep=0.000`
  -> guard REJECTS (non-vacuous). D2 reach-err `3.195e-03 rad` (<0.02). D3 `0.33-1.19 MB / 100 builds`
  (<4). D4 full-state checksum bit-identical. `ALL PASS (D1-D4 serial)`. Demo is a per-joint critically-
  damped PD on a 4-link serial arm (J1 yaw Y, J2/J3 X, J4 Z); link masses {4,4,3,2} are demo parameters
  (the FANUC's real link masses are not extractable from the STEP), gains per-joint Kp{5000,3000,800,350}
  Kd{700,450,110,50} scaled to reflected inertia.
- **No regression:** KRS_OVERNIGHT_BENCH **13/13** gate groups PASS (incl. GATE A A1/A2/A3/A5 — A3 now the
  GENERIC D6 capability test, residual 3.944e-07, explicitly NOT the FANUC); rigid KRS_BENCH **7/7**.

**URDF-lossless (point 4) — honest scope:** a serial 6-DOF tree is *losslessly representable* in URDF
(URDF's model is exactly a kinematic tree). The misread parallelogram would have required a closed loop,
which standard URDF CANNOT express (only `<mimic>` / non-standard `<loop>` hacks) -> a lossy export. So the
serial verdict makes a future export lossless. NB: **no URDF exporter is wired yet** (URDFParser.cpp is
import-only); this is the structural property, not a shipped feature — not to be reported as one.

Code: H3 block deleted from `runArticulationLiveGate`; `runDemoGateD` rebuilt on the A1 serial spec
(ArticulationGate.cpp); GATE-A "A3" + the PxD6 machinery retained + re-labelled generic. Commit on this.
