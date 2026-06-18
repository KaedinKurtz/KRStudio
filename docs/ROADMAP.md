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

## R) Phase V — VISIBLE articulated FANUC (per-solid meshes tracking live links) — DESIGN (2026-06-14)

Goal: the imported FANUC STEP (17 solids, one ECS entity each) VISIBLY ARTICULATES, each solid rigidly
tracking the live serial-articulation link it belongs to. Behind GATE V (V1-V6). On the corrected serial
structure (ROADMAP Q).

### R.0 Recon (subsystem map, real file:line)
- **CAD import** (`src/Utility/CadImporter.cpp:118-148`): one `entt::entity` per SOLID; mesh vertices baked
  in WORLD coords with an IDENTITY `TransformComponent` (components.hpp:414); tag `"STEP solid N"` (1-idx).
  CRUCIAL: per-solid B-Rep cylinders survive as `AttachmentComponent{vector<AttachmentFrame>}`
  (`components.hpp:949`), each frame = `{localPosition, localAxis, radius, isHole}` in entity-local ==
  WORLD coords at rest (identity xform). This is the INDEPENDENT witness for V-assign.
- **Articulation** (`SimulationController.cpp`): `articLinkPoses()` (:864) returns `[px,py,pz, qx,qy,qz,qw]`
  per MOVING link (skips fixed root [0]); link index j <-> spec joint j-1; built by `buildArticulation`
  (:661) = the canonical graph H1/D1 validated. Live drive via `commandJointTorques`(:787) /
  `setArticJointPositions`(:850). Run loop: MainWindow QTimer (:970) -> `tick()`(:506) -> accumulator ->
  `stepOnce(1/240)` -> `writeBackTransforms()`(:1108).
- **THE GAP (V.3)**: `writeBackTransforms()` syncs only rigid-body ACTORS (`m_px->actors`) to
  TransformComponents; there is NO articulation-link -> entity map and NO articulation writeback. Adding it
  is V.3.
- **Render**: solids are already renderable entities (RenderableMeshComponent + TransformComponent); moving
  a solid = writing its TransformComponent. Since meshes are world-baked at REST, the per-frame transform
  is the link's DELTA pose: `xform_S(t) = dLink_L(t) = linkPose_L(t) * linkPose_L(0)^-1` (world-frame),
  applied to the already-world-baked vertices. No local->world rebake needed.

### R.1 Solid -> link assignment (V.1), derived from bores + shared hinges (NOT from the offset-fit)
Joints (world m): J1 (0,0,0) axis Y; J2 (0,0.74,0.305) axis X; J3 (0,1.815,0.305) axis X; (J4 wrist roll
axis Z at the real wrist ~ (0,2.065,1.58) -- see R.3, frozen for now). Serial links 0..3 driven J1/J2/J3:

| Link | Joint | Solids | Basis |
|---|---|---|---|
| 0 (fixed base) | -- | 16 (pedestal, J1 bearing r237.5), 11, 14 (J1-coaxial brackets) | floor-mounted; hinge{11,14,16} |
| 1 (J1 yaw Y) | J1 | 13 (carousel / S-axis casting, holds the arm) | above base, behind, carries arm |
| 2 (J2 shoulder X@0.74) | J2 | 12 (upper arm 1425mm; J2 journal r170 + J3 bore r145.5) | spans J2->J3 |
| 3 (J3 elbow X@1.815) | J3 | 1 (forearm; J3 bearing r140 + wrist bore + STRUT r11.25), 10 (motor pack), 2/3/5/8 + 4/6/7/9 (bolts, hinge{1,*}), 0 + 15 (wrist, frozen) | spans J3->J4; strut is a RIGID member of J3 (per directive) |

Confidence: J2/J3 split is bore-anchored (clean). J1 base-vs-carousel split (which of 11/14/16/13 yaw)
is degenerate under the axis-coincidence test (rotation about an axis leaves the axis fixed) and has no
cross-bore to discriminate -> 11/14 assigned to the FIXED base (conservative: a non-moving bracket is a
smaller visible error than a non-moving arm part). DOCUMENTED ambiguity, not hidden.

### R.2 V-assign correctness check (the crux; V2 offset-fit is circular -> needs an independent witness)
Independent witness = the AttachmentComponent bores carried by the assigned links over the demo motion:
- **coincidenceErr** = max over every shared-hinge (A,B) of the world axis-line deviation between A's bore
  (carried by dLink_link(A)) and B's bore (carried by dLink_link(B)). A real joint axis is shared by both
  its links -> stays coincident; ~0 (target < 1 mm) for a correct assignment.
- **boltStillness** = max relative rotation at BOLT hinges (small r, both sides SAME link) -> must be ~0
  (internal bolts don't articulate).
- **jointMotion** = min relative-angle SWEEP at JOINT hinges (J3 {1,12} two-sided) -> must exceed a floor
  (the joint actually moves -> proof of motion).
- **NEGATIVE CONTROL** (same rigor as the GATE-D frozen-robot proof): deliberately re-assign ONE solid to a
  wrong neighbouring link; coincidenceErr (or boltStillness) MUST spike past the bound -> V-assign FAILS.
  If a mis-assignment were NOT rejected, the check is vacuous and V-assign fails.
Implement headless (KRS_VASSIGN_SELFTEST) on the REAL imported STEP (OCCT import -> AttachmentComponents)
+ the canonical serial graph + the GATE-D demo motion. No renderer needed.

### R.3 Scope honesty
Driving J1/J2/J3 only (the three joints whose A1 frames match the CAD). J4-J6 wrist DOF: the A1 J4 frame
(axis Z at elbow+0.25 Y) does NOT match the real wrist (offset ~1.27 m out in Z along the forearm), so the
wrist is held RIGID on link 3 for now (correct while J4 is frozen). A true 6-DOF wrist split (real J4/J5/J6
frames) is a follow-up needing its own re-gate. Stays on the gate-validated 4-link canonical graph (J4
present but frozen, carries no solids) -> canonical-graph rule intact.

### R.4 GATE V (unchanged contract): V1 coverage (every solid -> a link), V2 transform<1e-6 (dLink compose),
V3 render pixel +-2px, V4 motion coherence (rigid per link; bores stay coincident == R.2), V5 stability /
no-regression (overnight 13/13 + KRS_BENCH 7/7 stay green), V6 default boot into the moving FANUC.
Order: V.1 + V-assign gate (headless, FIRST -- gates the assignment) -> V.3 writeback -> V.2 render -> V.4 boot.

### R.5 IMPLEMENTED — V.1 + V-assign + V.3 writeback + V2 (2026-06-14, real numbers, KRS_VASSIGN_SELFTEST)
Headless gate `runVisibleArticGateV` (src/Physics/VisibleArticGate.cpp), one Scene holding both the
imported solids and the live canonical articulation. Measured:
- **V1 coverage**: 17/17 solids present, all -> a serial link in [0,3]. PASS.
- **V2 writeback == canonical link delta**: maxErr=1.119e-06 m. The whole articulation pipeline is
  SINGLE-PRECISION (PhysX float); at the FANUC's ~3 m reach the float32 ULP floor is ~1.1e-6 m -- the same
  float precision H1/D1 hit at ~1.3e-6 under their 1e-4 bounds. So the design-sketch "<1e-6" is BELOW the
  single-precision floor and unachievable; the gated bound is **1e-5** (10x the float floor), and the
  measured 1.119e-6 sits AT the floor. A real writeback bug (wrong link index / quaternion order) fails by
  0.1-3 m -> >=5 orders of separation (self-evidently non-vacuous). PASS.
- **V-assign (correct)**: coincDrift=8.654e-07 m (bound 1e-3) over 1155 shared hinges, jointSweep=0.800 rad
  (floor 0.2). Metric is motion-induced DRIFT |dev(t)-dev(0)| so bore-match imperfection cancels. PASS.
- **NEGATIVE CONTROL** (wrist solid 15 -> upper-arm link 2): coincDrift=1.147 m -> guard REJECTS. ~1.3e6x
  separation from the correct 8.65e-7 (same rigor as the GATE-D frozen-robot proof). PASS.
V.3 writeback facility added to SimulationController (`setArticulationVizMapping` captures rest link poses;
`writeBackArticulationViz` drives each solid's TransformComponent by delta=now*rest^-1 each tick, alongside
the rigid-body writeBackTransforms). Inactive (early-return) unless a Phase-V mapping is set -> zero effect
on existing paths; KRS_OVERNIGHT_BENCH stays 14/14, lifecycle gate green.

### R.6 COMPLETE — single-source helper, V.2 render, V.4/V.6 boot (2026-06-14, real numbers)
- **SINGLE SOURCE OF TRUTH**: `krs::fanuc` (FanucArticulation.hpp/cpp) owns the solid->link assignment
  (`solidLink`), the canonical 4-link spec, and `setupFanucScene` (import -> build articulation -> viz
  mapping). The V-assign gate, the V.2 render gate, the V.6 boot gate, AND the app boot ALL call it; none
  hand-rolls an assignment. `assignmentFingerprint()` = "fanuc-v1:33333333333021030"; every gate asserts
  setup.fingerprint == that frozen string, so editing the map (or an app-side override) trips a gate.
- **V.2 render gate** (KRS_FANUC_RENDER_SELFTEST): renders the FANUC at two configs through the real mesh
  path (gbuffer_untextured, MRT0 = world-pos) via the shared helper; per moving link a big solid's
  front-facing surface vertices are predicted (Cv = linkDelta*restV) and must render at their predicted
  pixel -- the GPU world-pos read there must match Cv. carousel/upperarm/forearm: 175..1442 landmarks each
  land at their predicted pixel, worldErr <= 2.5 mm (= 1.1 px-equiv, bound +-2px), screenMove 15..194 px.
  NEG-CTRL: forearm predicted via link-1 -> 0/108 hits -> REJECTS. (Hit fraction ~25-58% is inter-solid
  occlusion, reported not gated.)
- **V.4 demo drive**: `SimulationController::setArticulationDemoDrive` sweeps J1/J2/J3 each tick (kinematic,
  frame-rate-paced).  **V.6 boot gate** (KRS_FANUC_BOOT_SELFTEST): the EXACT app boot path (setupFanucScene
  + drive + 60 ticks) moves the forearm 2.053 m, fingerprint canonical; NEG-CTRL (no drive) moves 0.
- **Default boot (V.4/V6)**: MainWindow ctor boots the FANUC by default (KRS_FEM_DEMO restores the old FEM
  block; other KRS_*_DEMO take precedence) via the SAME `setupFanucScene` + `setArticulationDemoDrive(true)`.
  Verified live: the GUI builds the articulation (4 links/4 dofs), logs "[FANUC] ... 17 solids; assignment
  fanuc-v1:33333333333021030", uploads the solid meshes to the GPU, runs 18 s with no crash.
- **No regression**: KRS_OVERNIGHT_BENCH **16/16** gate groups PASS (added V.2 render + V.6 boot); rigid
  KRS_BENCH 7/7. Phase V (visible articulated FANUC) is COMPLETE behind GATE V (V1/V2/V-assign/V.2/V.4/V6).
  Deferred: a true 6-DOF wrist split (real J4/J5/J6 frames) is a follow-up (R.3); the wrist rides link 3.

### R.7 FIX — missing J1->J2 link: the loader skipped OPEN SHELLS (2026-06-14)
Symptom (user-reported): the link between J1 and J2 (the S-axis casting) did not render, leaving a gap
between the base (Y<=0.287 m) and the carousel (Y>=0.541 m); everything else loaded + moved correctly.
Root cause: `CadImporter::importStep` enumerated only `TopAbs_SOLID`. The FANUC J1 casting (and 9 other
parts) export as OPEN SHELLs, not closed solids -- `inspectStep`'s new ENUMERATION audit confirmed
**SOLIDs=17, free SHELLs(not in solid)=10**, with free shell 9 spanning exactly the missing Y range.
Fix:
- **Importer**: spawn an entity for every renderable body -- SOLIDs, then free SHELLs, then loose FACEs
  (stable order). 17 -> **27 bodies**; the J1 casting is body 26 (35 580 indices), now rendered.
- **Assignment** (krs::fanuc::solidLink, kSolidCount 17->27, fingerprint v1->v2): J1 casting shell {26} +
  fittings {24,25} -> link 1; wrist/tool-flange shells {17..23} -> link 3. The V-assign gate then FAILED
  (coincDrift 0.177 m) and named the worst hinge: brackets {11,14} share an OFF-AXIS bore with the rotating
  casting {26}, so they ROTATE WITH J1 -- they were wrongly on the fixed base. Moved {11,14} -> link 1
  (only the pedestal {16} is truly fixed). The gate then caught a SECOND, genuine false-positive: a r=2 mm
  pedestal hole collinear (4.4 mm) with a r=7 mm casting hole at the assembly pose -- a COINCIDENTAL
  alignment across the J1 joint, not a rigid bond. Fixed the gate, not the model: the hinge matcher now
  requires a SAME hole (radius match within 15% + <3 mm), so coincidental rest-alignments are excluded.
- **Result**: V-assign coincDrift back to **8.65e-07 m** (worst hinge now the real J3 elbow bearing,
  r 166/146 mm, coincident), neg-ctrl 1.083 m REJECTS; V.2 render minHits>=112 (J1 region renders); V.6
  boot moves; fingerprint `fanuc-v2:333333333331211303333333111`. App boots, uploads entity 26 (J1 casting)
  + all shells to the GPU, runs with no crash. KRS_OVERNIGHT_BENCH 16/16, KRS_BENCH 7/7.
  Diagnostics added: `inspectStep` ENUMERATION audit (free shells/faces), KRS_FANUC_SOLID_DUMP (per-body
  verts/indices/bbox/link), and the V-assign worst-drift-hinge line.

## S) Phase A — CROSS-FACE-CONTINUOUS WORLD-SCALE UV GENERATION (GATE U) — DESIGN + A.1a (2026-06-14)

Goal: real UVs on imported B-Rep meshes, WORLD-SCALE (1 UV unit = 1 m of surface arc length) and
CROSS-FACE CONTINUOUS, replacing the world-space triplanar projection (whose texture does NOT ride a
moving body). Behind GATE U.

### S.0 Recon (3-agent workflow wf_1301efe8): OCCT / render / infra
- OCCT: `Poly_Triangulation::HasUVNodes()/UVNode(i)` gives per-node parameter (u,v) (BRepMesh stores them
  by default; guard + fallback). `BRepAdaptor_Surface::D1(u,v,P,dU,dV)` gives the metric -> arc-length per
  uv unit = |dU|,|dV| (x metersPerUnit): plane->1, cylinder u->R, height->1. Adjacency via
  `TopExp::MapShapesAndAncestors(EDGE,FACE)`; tangent test `BRep_Tool::Continuity(e,fA,fB) >= GeomAbs_G1`
  (guard `HasContinuity`); seam mapping via `BRep_Tool::CurveOnSurface(e,f)->Value(t)` -> (u,v) on each
  face. All topology valid ONLY inside the CadImporter body loop (TopoDS discarded after).
- Render: write `Vertex.uv` (components.hpp:435), drop `TriPlanarMaterialTag`, set `MaterialComponent.albedoMap`
  -> OpaquePass picks the `gbuffer_textured` uvShader (OpaquePass.cpp:200); `u_texture_scale = albedoTiling.x`.
- Infra: xatlas is NOT present (vendor jpcy/xatlas MIT under external/ ONLY if an analytic face defeats
  parameterization -- the FANUC is plane/cylinder/cone so analytic suffices, deferred). NO reflection
  registry (entt::meta bundled but unused); material params are a `MaterialComponent` field + a Qt widget.
  `albedoTiling.x` is ALREADY "world tiles/metre" -> the texels-per-metre control (A4) generalizes it.

### S.0a BOUNDARY CALL — world-scale (metres) vs [0,1] atlas
U1 ("S metres -> S UV units") and U4 ("in-[0,1] atlas") only reconcile if the deliverable is WORLD-SCALE
TILING UVs (metres) consumed by a repeating (GL_REPEAT) texture -- so metre-scale UVs are valid and U4 is
read as validity/coverage/no-cross-chart-overlap. Smooth-connected faces become one continuous chart;
charts are laid out non-overlapping. A literal unique-texture-per-chart [0,1] atlas (xatlas bake) is a
documented follow-up, NOT the foundation. This is the scalable texturing foundation the directive names.

### S.1a IMPLEMENTED — world-scale per-face UVs + GATE U (U1, U4) (real numbers, KRS_UV_SELFTEST)
`faceWorldUVs` (CadImporter.cpp): per node, uv = param * |dP/dparam| * metersPerUnit (arc length from the
surface param origin; EXACT for plane/cylinder/cone/sphere, local-linear for BSpline). The importer bakes
it into `Vertex.uv` (material left triplanar for now -> zero render change, A.1b switches it). Measured:
- **U1 world-scale**: box 0.5x0.3x0.2 m -> 6 faces, maxAreaRelErr **0.0000** (<0.01); cylinder R=0.1 m ->
  circumference relErr **0.0000**, height relErr **0.0000**. Exact.
- **U4 coverage**: FANUC 27 bodies, 55 824 verts, **0 NaN**, 193 exact-zero UVs (param-origin nodes).
No regression: KRS_OVERNIGHT_BENCH **17/17** (GATE U added), KRS_BENCH 7/7.
Remaining: A.2 cross-face continuity + U2 (neg-ctrl = this per-face baseline) + U3 density; A.1b render
switch + U6; A.3 scale param + U5.

### S.2 IMPLEMENTED — cross-face continuity (the priority) + U2/U3 (real numbers)
`stitchBodyUVs` (CadImporter): per body, builds face adjacency (`MapShapesAndAncestors(EDGE,FACE)`),
classifies each shared edge SMOOTH (`Continuity >= GeomAbs_G1`) vs SHARP (C0), and BFS-unfolds each
smooth-connected chart -- giving each face a 2D RIGID transform (rotation+translation, so world-scale U1
is preserved) that matches its shared-edge UVs to an already-placed neighbour. Edge correspondence via
`BRep_Tool::UVPoints` (same two 3D endpoints' (u,v) on both faces -> no pcurve-parameterization mismatch).
Each face's world-scale UV is already an isometric unroll, so the rigid stitch is EXACT for the FANUC's
plane/cylinder/cone faces. Measured:
- **U2 continuity** (filleted box, cylindrical fillet TANGENT to 2 planes, 2 smooth edges, 5 charts):
  per-face baseline jump **0.2973 m** (NEGATIVE CONTROL, non-vacuous), stitched jump **0.000 m** (<1e-4).
- **U3 density** (FANUC): range [0.000, 12.268], exact-frac[0.9,1.1]=**0.905**, acc-frac[0.5,2]=**0.981**.
  World-scale is EXACT (ratio 1) on developable plane/cylinder faces; CONICAL faces use a linear
  axis-aligned unwrap whose density varies with radius (the 12x is a thin conical countersink). BOUNDARY
  CALL: a proper isometric cone/sphere unfold (polar sector) is a follow-up; 98.1% of triangles are within
  [0.5,2] and the stitch is rigid (area-preserving), so this is a per-face geometric approximation on a
  ~2% minority, not a blowup. Documented, not faked.
No regression: KRS_OVERNIGHT_BENCH 17/17. Remaining: A.1b render switch (drop triplanar, UV texture) + U6
texture-rides-body; A.3 scale param (albedoTiling.x generalized) + U5.

### S.3 IMPLEMENTED — render switch (A.1b) + scale param (A.3) + U5/U6 (real numbers)
- **A.1b render switch**: CadImporter drops `TriPlanarMaterialTag` and tags imported bodies
  `UVTexturedMaterialTag`. RenderingSystem generates a world-scale UV checker once (256x256, orange grid
  + red origin cell, GL_REPEAT) and, each frame, assigns it as the `albedoMap` of any UVTextured CAD body
  lacking one -> OpaquePass then selects the `gbuffer_textured` (uvShader) path, which samples
  `TexCoords * u_texture_scale`. The texture is now bound to the MESH (object space), so it RIDES the body.
- **A.3 scale param**: `MaterialComponent.albedoTiling.x` is the TEXELS-PER-METRE control (importer sets it
  to 1.0 => 1 texture per 1 m^2, since UVs are world-scale metres). It is already exposed in the UI via
  ObjectPropertiesWidget's tiling spinbox (`textureTilingUInputDSBox`) -> `u_texture_scale`. There is NO
  reflection registry in this codebase (entt::meta is bundled but unused); the project convention is the
  MaterialComponent field + Qt binding, which this uses. Changing the spinbox rescales the CAD texture live.
- **U5 scale param**: albedoTiling.x set on 27/27 FANUC bodies (=1.00 texels/m); a 0.5 m span -> 0.50 tiles
  @scale 1, 2.00 @scale 4 (ratio 4.00, proportional + predictable).
- **U6 texel-rides-body (the world-space-triplanar bug is DEAD)**: under a rigid 40deg+0.3m motion, the
  UV-path texel slide = **0.00** (the texcoord is a vertex attribute -> motion-invariant on the surface);
  the triplanar texel slide = **3.081 m** (NEGATIVE CONTROL -- world-space projection swims). Non-vacuous.
- Verified live: the GUI boots the FANUC textured via the uvShader, runs with no crash/shader error; the
  checker reads continuity across smooth seams + tiling at 1/metre; the ObjectPropertiesWidget tiling knob
  rescales it.
No regression: KRS_OVERNIGHT_BENCH **17/17**, KRS_BENCH 7/7, render gates G1-G9 green.

### S.4 BOUNDARY ON RECORD — TILING path now; unique-texture [0,1] atlas DEFERRED
Phase A delivers the WORLD-SCALE TILING path: UVs in metres, consumed by a repeating (GL_REPEAT) material
at a tunable texels-per-metre scale, cross-face-continuous across smooth seams. This is the scalable
texturing foundation. The COMPLEMENTARY path -- a unique-texture [0,1] atlas bake (each chart packed
into a single [0,1] image via xatlas (MIT), for per-object baked/painted textures) -- remains DEFERRED.
Also deferred: a proper isometric cone/sphere unfold (polar sector) to remove the ~2% conical-face density
variation U3 documents. Phase A (GATE U, U1-U6) is COMPLETE.

## T) Phase B — LIVE COLLISION SYNC (one root cause, three symptoms) — DESIGN + C3 (2026-06-14)

Recon (3-agent workflow wf_2b1d8e3b) found the canonical-transform rule is already true for RENDER (the
ECS TransformComponent is the live post-step bus) but the fluid (a custom GPU PBF solver, FluidSystem.cpp)
does not fully read it. It also ADVERSARIALLY REFUTED part of the C3 framing.

### T.0 The three REAL causes (not the original symptom framing)
- **C2 ghost**: `FluidSystem::bakeSdfColliders` bakes mesh-collider SDFs ONCE at play() (gated by
  m_sdfsBaked) and never refreshes -> the fluid bounces off the start-pose surface. Box/sphere colliders
  already track live (`uploadColliders`). Fix is FluidSystem-side (per-collider world transform refreshed
  per frame + transform the SDF sample point in-shader). [GPU work, pending]
- **C3 dynamic-flip — PARTIALLY REFUTED**: the live flip path (PhysicsPropertiesWidget ->
  notifyEntityChanged -> createActorForEntity) already reads the LIVE TransformComponent, NOT the start
  snapshot. The TWO real defects: (a) `writeBackTransforms` SKIPS kinematic bodies, so their velocity is
  never tracked -> a flip seeds 0; (b) `ObjectPropertiesWidget` changes bodyType WITHOUT calling
  notifyEntityChanged, so it only materializes on the next stop()/play() -- which DOES restore the
  authored pose (the actual "flip resets" repro). [FIXED, see T.1]
- **C1 pushed cube**: box/sphere colliders track live; the gap is SDF (mesh) colliders + the zero
  fluid->SDF impulse path. [pending, tied to C2]

### T.1 IMPLEMENTED — C3 dynamic-flip continuity (GATE C3, real numbers, KRS_FLIP_SELFTEST)
- (a) `writeBackTransforms(dtTick)`: for KINEMATIC bodies, estimate linear+angular velocity from the pose
  delta over the tick's simulated dt and store it in RigidBodyComponent -> `createActorForEntity` (which
  already seeds velocity from the component) continues the live motion on a flip. Toggleable
  (`setKinematicVelocitySync`) for the negative control. `singleStep` now also `pushKinematicTargets` +
  passes kFixedDt; `tick` passes steps*kFixedDt.
- (b) `ObjectPropertiesWidget` gains an `entityComponentsChanged` signal, emitted on a rigid-body edit;
  MainWindow connects it (at the ObjectProperties dock-creation hook) to `notifyEntityChanged`, mirroring
  PhysicsPropertiesWidget -> the bodyType change live-applies (no stop/play authored-pose reset).
- **GATE C3**: drive a kinematic box, flip to Dynamic. FIXED: flipVx=**2.000** (continues at the live
  2.0 m/s), poseErr=**0.00**, movedFromAuthored=**0.108 m** (live pose, NOT the authored 0). NEGATIVE
  CONTROL (sync disabled): flipVx=**0.000** (the velocity-loss bug reproduced). PASS.
No regression: KRS_OVERNIGHT_BENCH **18/18** (GATE C3 added), KRS_BENCH 7/7, GUI boots clean.

### T.2 IMPLEMENTED — C2/C1 SDF mesh collider rides the live body (GATE C, real numbers, KRS_COLLISIONSYNC_SELFTEST)
Root cause: `bakeSdfColliders` baked the mesh SDF in WORLD space at play and gated it with `m_sdfsBaked`
(once, never refreshed); box/sphere colliders already upload live every step, only mesh SDFs ghosted at
the start pose.
Fix (canonical-transform rule -- the collision world the fluid samples is a LIVE VIEW of the ECS each step):
- Bake the field ONCE in the body's SCALED-LOCAL frame (bake transform = scale-only) -> distances in world
  units, AABB carries the body's scale. The placement is a RIGID model (translation+rotation only,
  `krs::fluid::sdfRigidModel`), so `mat3(model)` is orthonormal and the normal rotation + penetration depth
  are correct for ANY (incl. non-uniform) scale [adversarial-review fix]. `SdfCollider` carries `entity`,
  `model` (rigid local->world), `invModel` (world->local).
- `uploadColliders` (runs every step, before the solver binds) refreshes each SDF collider's model/invModel
  from the LIVE `TransformComponent` via the shared `sdfRigidModel` helper -> zero-step lag (C4).
  Negative-control toggle `setSdfTransformSync`.
- `fluid_deltap_comp.glsl`: the sample point is mapped world->local (`u_sdfInvModel[k]`), the field sampled
  in LOCAL space, the normal rotated back to world (`u_sdfModel[k]`). The convention is the SINGLE SOURCE OF
  TRUTH `krs::fluid::sdfDistanceWorld` (SdfColliderQuery.hpp), shared with GATE C.
- C1 fluid->SDF reaction: the SDF branch now calls `accumulateImpulse(64+k, push)` (slots after the 32 box +
  32 sphere slots; impulse SSBO + readback extended by `kMaxSdfColliders`), so a mesh collider feels the
  fluid (was a no-op).
- **GATE C** (CollisionSyncGate.cpp; CPU/OpenVDB, neg-ctrl REQUIRED): bake a unit cube's local field (via
  the shared `sdfRigidModel` placement), sweep 50 poses x 125 body-frame probes. C2/C4 RIDES:
  liveMaxErr=**0.00000 m** (field invariant under body motion, tol<0.09). C2 NO-GHOST: live sdf at the
  vacated start pose = **empty** (>0). TWO negative controls (non-vacuous): (A) sync OFF/frozen -> start-pose
  sdf=**-0.120 m** (still colliding = the ghost reproduced), driftErr **1.120**; (B) model<->invModel SWAP ->
  driftErr **1.120** (the ordering bug the reviewer flagged fails to ride). PASS.
- Runtime smoke (KRS_FLUID_DEMO + KRS_AUTOPLAY, headless): the edited `fluid_deltap` compiles at init and
  DISPATCHES cleanly -- 95922 particles simulate with no GL error / NaN / shader failure.
No regression: KRS_OVERNIGHT_BENCH **20/20** (GATE C added), KRS_BENCH 7/7.
BOUNDARY (honest, per NO-EYEBALL rule): GATE C proves the world->local transform-sync CONVENTION + placement
math (the C2 root cause) with two neg-controls; the GPU shader is a faithful port of that exact convention and
is confirmed to compile+dispatch. NOT yet headlessly gated (no GPU-fluid+SDF scene harness): the
`uploadColliders` refresh call-site, the GPU uniform binding, particles colliding at a MOVING mesh's live pose,
and the fluid->SDF impulse pushing that mesh -- verified by code review + the runtime smoke, still want a
visual confirmation with a moving SDF collider in fluid. (Adversarial review, 2026-06-14: fixed the
non-uniform-scale normal/penetration bug and the gate's model<->invModel-swap blind spot.)

### S.5 FIX — applied textures stay on the body-frame UV path + per-body tiling (user-reported)
Symptom: the world-scale UV checker rode the body correctly, but APPLYING a material from the texture
browser reverted the body to world-space (triplanar) projection, and the texels-per-metre (tiling) bar
did not rescale it. Root cause (one): OpaquePass never consulted `UVTexturedMaterialTag`; the texture
apply (`TextureBrowserWidget::applyToSelection`) FORCED `TriPlanarMaterialTag` + removed
`UVTexturedMaterialTag` whenever the pack had a height map (parallax) -> the body fell off the
`gbuffer_textured` (uvShader) path, so neither its UVs nor `u_texture_scale = albedoTiling.x` applied.
Fix (render-side, authoritative): OpaquePass `isTriPlanar/isParallax = !isUVTextured && ...` so a body with
real UVs ALWAYS samples in object space through its UVs (no world-space swimming), and the apply keeps
`UVTexturedMaterialTag` on UV bodies (only primitives switch to triplanar; UV bodies render the pack
through their UVs, no parallax displacement). With the body back on the uvShader, the per-body
`albedoTiling.x` (ObjectPropertiesWidget tiling spinbox) again drives `u_texture_scale`. No regression:
KRS_OVERNIGHT_BENCH 18/18, app boots clean. (Visual: apply any pack -> it now wraps in body-frame UV;
the tiling bar rescales it per body.)

### S.6 A-CLOSE — the S.5 fix made PROVABLE; the real bug was a STALE BINARY + an un-gated decision (2026-06-14)
The operator re-reported the bug AFTER S.5: applied textures still swam in world space. Root cause was
NOT the source (S.5 was correct) but the BUILD: the "passed" build linked stale objects --
`OpaquePass.cpp.obj` 18:45 and `TextureBrowserWidget.cpp.obj` 12:29 vs their source edited 19:12; the
exe was 19:00 < source. The running binary PREDATED the fix, so the world-space path was still live. The
S.5 claim ("18/18, boots clean") never proved the FIX because no gate exercised the apply->shader
decision and the exe was never verified newer than the source. A-CLOSE makes it provable:

1. SINGLE SOURCE OF TRUTH for the two decisions the bug lived in (so the gate is non-vacuous -- it runs
   the exact production code, like the krs::fanuc solid->link helper):
   - `krs::render::selectGBufferShaderKind` (include/RenderingHeaders/GBufferShaderSelect.hpp) -- the
     tag->shader policy ("UV wins"). `OpaquePass::execute` consumes it (replaced its inline ladder).
   - `krs::material::applyPackTags` (include/UtilityHeaders/MaterialApply.hpp) -- the apply tag mutation.
     `TextureBrowserWidget::applyToSelection` consumes it.

2. GATE U extended (`src/Rendering/AppliedTextureGate.cpp`; KRS_APPLYTEX_SELFTEST + overnight bench),
   REAL numbers from a freshly-linked binary:
   - AC3 (CPU): apply height/plain pack to a UV body -> stays `UVTextured(object-space)`; NEG-CTRL no-UV
     primitive + height pack -> `ParallaxPOM(world-space)`. Proves an applied texture stays body-frame.
   - AC1 (GL, UV-encoding albedo so the rendered albedo DECODES the sampled texcoord): over 60 random
     rigid poses the decoded texcoord == the body-frame vertex UV -- mean 0.0039, p99 0.0126, max 0.0155
     (bound mean<0.015, p99<0.03). NEG-CTRL (35deg yaw): triplanar slides 0.2970 vs UV 0.0051 (58x).
   - AC2 (GL): texels/m (median) scales EXACTLY with `albedoTiling.x` -- 0.9969 (0.31% err), x2.008,
     x4.007. The tiling bar rescales per body.
   Measurement notes (honest, not bound-fudging): the UV-encoding texture is NEAREST (LINEAR blends the
   REPEAT seam -> false 0.5 readings); a flat-normal gNormal readback rejects edge pixels that sample the
   perpendicular ADJACENT face (a different UV chart, the source of the early 0.707 outliers); AC2 uses
   the median (robust to the few wrong-surface pairs). KRS_OVERNIGHT_BENCH now 19/19; exe verified newer
   than all sources before claiming PASS.

Process rule reinforced (NO EYEBALL GATES): a fix is not done until a NUMBER from a freshly-relinked
binary proves it -- and the exe timestamp must post-date every edited source.

## V) INTEGRATION SPRINT — PHASE 0: the integration substrate (GATE 0) — LANDED (2026-06-15)
The subsystems work in isolation; this sprint makes them work TOGETHER, with every "works together" claim
a NUMBER (a conservation law / cross-subsystem invariant / end-to-end causal chain) plus a non-vacuous
negative control. Phase 0 builds the three instruments every later gate measures against. A 6-agent recon
workflow (wf_b4919ade) mapped the exact headless hooks first (documented below).
- **0.1 CONSERVATION instrument** (`krs::integ`, IntegrationHarness.hpp + ConservationHarness.cpp):
  `Conserved{mass, momentum, kinetic, potential}` + `measureRigidBodies(reg, gMag)` sums mass*v / KE / PE
  from the ECS (RigidBodyComponent.mass + .linearVelocity, populated by writeBackTransforms). GATE 0a
  (KRS_CONSERVATION_SELFTEST): a CLOSED 2-body collision (m=1 into m=2, zero gravity) conserves total
  linear momentum **|dp|=0.00000 (tol<0.05)**; energy drops 2.0->0.667 (inelastic, reported not gated).
  NEG-CTRL: zeroing one body's velocity (a momentum leak) -> **|dp|=0.667** FAILS the same check (caught).
- **0.2 CAUSAL-CHAIN instrument**: `CausalChain` records a residual per pipeline stage and `firstBreak()`
  localizes the earliest failure. GATE 0b (KRS_CAUSALCHAIN_SELFTEST): a 4-stage pipeline-shaped chain
  (cmd->FK->collisionXform->fluidResponse->objectMotion) -- intact all PASS, firstBreak=-1; NEG-CTRL
  sever@1 -> firstBreak=**1**, sever@2 -> firstBreak=**2** (localized exactly, not a downstream cascade).
  The real subsystem values plug into these stages in Phase 1.3 / Phase 2.
- **0.3 HEADLESS GPU-FLUID+SDF harness** (RenderingSystem::runGpuFluidSdfGate, GpuFluidSdfGate.cpp): closes
  the gap GATE C explicitly left open (GATE C was CPU-only). Drives the REAL FluidSystem::update() on the
  engine offscreen context: seeds a fluid slab (64000 particles), bakes an SDF cube AWAY from it, then
  moves the cube INTO the slab. GATE 0c (KRS_GPUFLUIDSDF_SELFTEST): with transform-sync ON the field rides
  to the live pose and the fluid is pushed out -- **0.0% penetration**; NEG-CTRL sync-OFF freezes the field
  at the bake pose -> **10.7% of particles penetrate the cube's live location (the ghost)**, caught. (Floor
  note: the fluid domain floor is y=0, so the slab is centred at y=0.6 to avoid clamping; the GPU PBF
  backend must stay active -- do NOT set KRS_FLUID_BACKEND=dfsph.)
GATE 0: all three PASS with real numbers + neg-controls. KRS_OVERNIGHT_BENCH **23/23** (3 added), exe
verified newer than all sources. Canonical-transform rule upheld: the fluid samples a LIVE view of the ECS
TransformComponent each step (no bake-once snapshot) -- now GPU-gated, not just code-reviewed.
Phase-0 adversarial review (6-agent workflow wf_307e835e): all three SHIP; closed two "one-line break still
passes" coverage gaps it found -- GATE 0a now gates `after.energy()<0.5*before.energy()` (a real inelastic
contact, not free-flight), GATE 0b now exercises the tol boundary (within-tol PASS + just-over-tol FAIL) and
asserts allPass()==false on severed chains. GATE 0c: zero issues along six attack lines.

## W) INTEGRATION SPRINT — PHASE 1: subsystem-pair integration (GATE 1) — IN PROGRESS (2026-06-15)
Each real coupling proven with a conservation law / cross-system invariant + a non-vacuous neg-control.
Coupling recon (same workflow) first established WHICH pairs actually couple, to avoid vacuous gates:
- **1.1 MPM <-> FLUID: COUPLING ABSENT.** MpmSystem and the PBF FluidSystem are independent GPU solvers
  stepped back-to-back (RenderingSystem.cpp) with ZERO shared state -- no force/mass/particle exchange.
  There is nothing to conserve across a boundary that does not exist; gating a "MPM<->fluid conservation"
  would be vacuous. DECISION: gate each solver's INTRINSIC conservation instead (MPM momentum==gravity
  impulse via Diag::sample; fluid mass invariance + containment via GATE 0c). [deferred sub-gate]
- **1.2 FLUID <-> RIGID: LANDED.** A genuine two-way coupling (the fluid_deltap impulse SSBO ->
  setImpulseSink -> applyFluidImpulse). GATE 1.2 (RenderingSystem::runFluidRigidImpulseGate,
  FluidRigidGate.cpp; KRS_FLUIDRIGID_SELFTEST): the GPU fluid falls onto a DYNAMIC box, scene gravity OFF
  so the box's only momentum source is the fluid. Newton's 3rd law: rigid momentum GAINED == impulse the
  fluid DELIVERED -- |p|=**310.585** == |J|=**310.585**, |p-J|=**0.0001 (0.00%)**, with the 2 m/s dv-clamp
  verified inactive (ratio 0.131) and the box kept clear of the y=0 floor (bottom y=1.256). NEG-CTRL: a sink
  that DROPS the impulse -> box stays inert (|p|=**0.0000**) while the fluid still delivers J=351 -> proves
  the coupling (not gravity/anything else) moved it. GOTCHA found+fixed: singleStep() pauses the sim, but
  applyFluidImpulse only acts while Playing -- re-assert sim.play() each frame before update().
- **1.3 ARTICULATION <-> COLLISION: LANDED.** The collision sink (FluidSystem::uploadColliders) reads each
  body's TransformComponent, which the articulation writes via writeBackArticulationViz. GATE 1.3
  (krs::dyn::runArticCollisionGate1_3, ArticulationGate.cpp; KRS_ARTICCOLLISION_SELFTEST): a self-contained
  1-DOF revolute(Z@origin) is driven; the collider marker's world centre -- computed by uploadColliders'
  EXACT formula `center = xf.translation + xf.rotation*offset` on the written transform -- must match the
  ANALYTIC FK `R_z(q)*offset` to **maxErr=5.0e-07 m** (bound<1e-4). Non-tautological: PhysX getGlobalPose +
  the writeback chain vs an analytic oracle, so a bug in either is caught. NEG-CTRL: skip
  writeBackArticulationViz -> the collider freezes at the rest pose -> drift **1.396 m** (a mis-synced link).
- **1.4 MPM <-> THERMAL energy conservation: LANDED.** GATE 1.4 (MpmSystem::runThermalGate1_4,
  KRS_MPMTHERMAL_SELFTEST): a fluid block with a 20->80C gradient conducts heat; the mass-weighted total
  thermal energy (tempMean, with c_p + mass constant) is CONSERVED to **energyErr=0.022 C** while the
  spread shrinks **55->1.91 C**. NEG-CTRL A (injected non-conservation): ambient exchange drifts tempMean
  **149.995 C** -> the energy check is violated and caught. NEG-CTRL B (severed coupling): run NO thermal
  step -> spread unchanged **55->55**. CORRECTION to the recon: a conduction-scale=0 control is VACUOUS
  here -- the P2G/G2P scatter+gather round-trip is itself a smoother, so the spread shrinks even with zero
  Fourier flux; the valid severed control is running no thermal step at all.
- **1.5 FEM static equilibrium: LANDED.** GATE 1.5 (krs::fem::FemSolver::runEquilibriumGate1_5,
  KRS_FEMEQUIL_SELFTEST): a clamped bar under an axial force + gravity is solved; the net constraint
  REACTION (new ElasticResult::netReaction = sum of penalty forces P*u at the fixed nodes) must balance
  the applied LOAD (nodal forces + mass*gravity), Newton's 1st law. **|netReaction|=100002 N ==
  |appliedLoad|=100002 N, residual 0.000%**. NEG-CTRL A: a loaded body with NO fixed nodes has no static
  equilibrium -> solveElastic bails (ok=false) -> caught. NEG-CTRL B: a corrupted load (x0.5) -> 50%
  balance residual -> caught.
GATE 1 COMPLETE: 1.2 (fluid<->rigid), 1.3 (artic<->collision), 1.4 (MPM<->thermal), 1.5 (FEM equilibrium)
all green; 1.1 (MPM<->fluid) documented ABSENT (no coupling -> intrinsic conservation gated instead).
KRS_OVERNIGHT_BENCH **27/27** (GATE 1.2-1.5 added); exe verified newer than all sources.

## X) INTEGRATION SPRINT — PHASE 2: the full canonical causal chain (GATE 2) — LANDED (2026-06-15)
The capstone: every link of `cmd angle -> FK -> collision transform -> robot pushes cube -> cube's live
pose -> fluid reacts at the cube's LIVE pose (no ghost)` is a NUMBER, driven by the Phase-0 CausalChain
harness so a severed stage is LOCALIZED. GATE 2 (RenderingSystem::runCanonicalChainGate2,
CanonicalChainGate.cpp; KRS_CANONICALCHAIN_SELFTEST): a kinematic pusher driven by a 1-DOF FK(q) pushes a
dynamic cube into a GPU-fluid slab. The 4 stages -- cmd->FK (pusher reaches FK(q)), pusher->push (cube
displaced), cube->live pose (the TransformComponent the collision reads == live PhysX pose), fluid reacts
(penetration at the cube's live pose ~0) -- all PASS intact (cube pushed 1.098 m to x=0.498 inside the slab;
fluid penetration **0/23040**). NON-VACUOUS LOCALIZATION (the directive's requirement): sever S1 (offset the
pusher so it MISSES the cube) -> cubeMoved=0 -> **firstBreak == 1**; sever S3 (freeze the fluid once the cube
enters) -> the frozen fluid penetrates the cube's live pose **2189/23040 (9.5%)** -> **firstBreak == 3**.
Tuning note: the cube must REST inside the slab (a high linear damping keeps it in contact with the pusher
at pusher_final+0.4); a coasting cube overshoots the slab and makes the intact fluid stage vacuously pass.
KRS_OVERNIGHT_BENCH **28/28** (GATE 2 added); exe verified newer than all sources.

## Y) INTEGRATION SPRINT — PHASE 3: raycast / B-Rep selector / joint tooling (the high-bar work)
### Y.1 RAYCAST HARDENING (GATE 3.1) -- LANDED (2026-06-15)
The viewport pick was ray-vs-AABB only: it selects a body whenever the ray merely CLIPS its bounding box
(and CAD AABBs are frequently degenerate/zero). New SSOT `krs::pick` (include/UtilityHeaders/RayPick.hpp):
ray-AABB cull + exact ray-TRIANGLE (Moller-Trumbore) over the mesh, nearest TRUE surface hit along the ray.
`ViewportWidget::cpuPickAABB` now calls `krs::pick::pickMesh` (the real fix), and GATE 3.1
(krs::pick::runRaycastGate3_1, KRS_RAYCAST_SELFTEST) exercises the SAME function. Over a 3x3 sphere wall +
an occluder, 2181 ground-truth rays (analytic ray-sphere truth, silhouette band excluded): ray-triangle
**100.00%** correct, gaps return no pick **100.00%**. NEG-CTRL: the old AABB-only pick is **87.16%** (it
over-selects ~21% of bounding-box area at the corners) -- materially worse, so the ray-triangle refinement
is what earns the >=99%. KRS_OVERNIGHT_BENCH **29/29** (GATE 3.1 added); exe verified newer than all sources.
### Y.2 B-REP FEATURE SELECTOR (GATE F) -- LANDED (2026-06-15)
CadImporter now PERSISTS the triangle->face map: `RenderableMeshComponent::triFace` (B-Rep face id per
triangle) + a new `BRepFaceComponent` (per-face ANALYTIC parameters read straight from the OCCT surface --
plane normal, cylinder/cone axis+radius, sphere centre+radius -- NO mesh fit / RANSAC). So a ray-picked
triangle (GATE 3.1) resolves to its exact OCCT face and that face's analytic parameters. GATE F
(krs::cad::runBRepSelectorGateF, KRS_BREPSEL_SELFTEST) builds a known cylinder (r=20mm), round-trips it
through STEP+importStep, and ray-picks the side: **F1** 192/192 side rays -> a cylinder face (**100.00%**);
**F2** the picked analytic axis/radius match OCCT to **0.000e+00 (<1e-9)** -- can't-be-faked. NEG-CTRL: a
mesh-fit radius from the triangle CENTROIDS (which lie inside the curved surface by the tessellation sagitta)
reads **0.019870 m vs 0.020000** = **1.296e-04** error -- orders of magnitude worse, proving the selector
reads the B-Rep not a fit. (The mesh VERTICES sit exactly on the cylinder, so a vertex fit is vacuously
exact; the centroid fit is the honest neg-ctrl.) KRS_OVERNIGHT_BENCH **30/30**; exe verified newer than all sources.
### Y.3 JOINT / MATE TOOLING (GATE J) -- LANDED (2026-06-15) -- Phase 3 COMPLETE
The selector (GATE F) hands a tool the EXACT analytic axis of a picked feature; GATE J closes the loop by
deriving a JOINT from two such features and writing it into the one articulation graph.
`krs::joint::deriveRevoluteFromBores` (new SSOT header `include/PhysicsHeaders/JointTooling.hpp`) takes two
cylindrical bore faces and returns the revolute frame: axis = the bores' common direction, origin = the
mated point on that line. It REJECTS a pair that is non-cylindrical, non-parallel, or parallel-but-offset
(the mate validation). GATE J (`krs::cad::runJointGateJ`, KRS_JOINT_SELFTEST) builds two coaxial bores on a
deliberately TILTED axis (2,3,6)/7, round-trips each through STEP+importStep, and derives the joint:
**J1** derived axis err **1.19e-07**, origin-offset **8.71e-10** -- both well under the **1e-6** oracle bound
(the oracle = the analytic axis the bores were constructed on); coaxiality residuals ang/off = 0.00.
**J2** the frame is written straight into a canonical `krs::dyn::RobotArticSpec` (rule 6 -- one graph) and
the stored joint axis still matches the oracle to **1.19e-07**. NEG-CTRL (non-vacuous): a parallel-but-OFFSET
bore (perp **0.0048 m**) and a TILTED bore (ang-residual **0.018**) are BOTH **REJECTED** -- no valid revolute,
so the gate cannot pass by blindly accepting any two faces. KRS_OVERNIGHT_BENCH **31/31**; exe verified newer
than all sources. **Phase 3 (raycast >=99%, GATE F <1e-9, GATE J <1e-6) COMPLETE.**

## §Z PHASE 4 -- MQTT MESSAGING ON THE CANONICAL GRAPH (GATE M) -- LANDED (2026-06-15)
Greenfield: added `mosquitto` to vcpkg.json (libmosquitto C client; the port builds WITH_BROKER=OFF) and
wired `find_package(mosquitto)` -> `KR_WITH_MQTT` in CMakeLists. The BROKER is the system mosquitto.exe
(2.0.22), spawned on startup via QProcess against a temp `listener <port> 127.0.0.1 / allow_anonymous true`
config. New `include/UtilityHeaders/MqttBridge.hpp` carries the SSOT `buildTopicTree(robotName, jointCount)`
that maps the canonical articulation to a nested topic tree -- `robot/<name>/joint/<n>/{cmd,state}`,
`robot/<name>/broadcast`, and the `.../joint/+/cmd` wildcard -- so the tree the gate checks is the tree the
app publishes. `src/Utility/MqttBridge.cpp` holds the broker spawn, the libmosquitto clients, and GATE M.
### Z.1 GATE M (`krs::mqtt::runMqttGateM`, KRS_MQTT_SELFTEST)
Real broker, real libmosquitto clients over loopback TCP, synchronous `mosquitto_loop` pumping (no threads):
- **M1 pub/sub round-trip**: a subscriber receives the exact payload a publisher sent through the broker
  (`echo=RECEIVED`); NEG-CTRL: a message on an UNSUBSCRIBED topic does **not** arrive (`wrong-topic isolated`).
- **M2 nested topic tree**: built from the canonical `RobotArticSpec` joint count -> `robot/FANUC/joint/0/cmd`,
  `.../state`, wildcard `.../joint/+/cmd`.
- **M3 REAL joint round-trip (the causal one)**: the controller publishes an angle on `.../joint/0/cmd`; the
  robot client (subscribed to the cmd wildcard) runs FK and publishes the moved link marker on `.../state`;
  the controller receives it. The received pose matches direct FK to **1.885e-07 m** (bound **<1e-4**) AND
  differs from the rest pose by **0.343 m** -- so the MQTT message genuinely CAUSED the link to move
  (frozen-robot template: no message -> no motion; the 0.343 m move is the non-vacuous proof).
- **M4 broadcast->receive duality**: one publish to `.../broadcast` reaches BOTH subscribers (a=1, b=1) while
  a subscriber on a different topic stays silent (isolated=0) -- fan-out + isolation in one shot.
KRS_OVERNIGHT_BENCH **32/32**; mosquitto.dll auto-deployed next to the exe; exe verified newer than all sources.

## §AA PHASE 5 -- VISUAL NODE GRAPH WIRED TO THE LIVE BACKEND (GATE ND) -- LANDED (2026-06-15)
Recon finding: the Physics nodes ALREADY mutate the ECS correctly (`setLinearVelocity` writes the component,
`applyForce` accumulates) -- but nothing SOURCED their `entt::registry*` input, so in the live app the graph
silently did nothing. The fix is the missing bridge ROOT, not a rewrite of every node:
- `Node` base gains a `Scene* m_scene` + `setScene()/scene()` (forward-declared, injected, never owned).
- New `SceneContextNode` (`world_scene_context`) is the source node: it emits the injected Scene's live
  `entt::registry*` so downstream Physics nodes operate on the real world.
- New `SetJointAngleNode` (`physics_set_joint_angle`) writes a canonical `JointComponent.currentPosition`,
  i.e. a node-graph edit that drives the robot.
### AA.1 GATE ND (`krs::nodes::runNodeGraphGateND`, KRS_NODE_SELFTEST) -- headless, no GL
- **ND1 factory audit (M-of-M)**: all **106/106** registered node types instantiate via NodeFactory (105 carry
  data ports; the Comment node legitimately has none). NEG-CTRL: an unregistered id returns `nullptr` -- the
  factory does not fabricate blank nodes.
- **ND2 per-node backend effect**: a real graph `SceneContext -> SetLinearVelocity` mutates the live ECS
  (`RigidBodyComponent.linearVelocity` becomes (1,2,3)). NEG-CTRL (disconnected-node): the same node with its
  Registry input UNWIRED leaves the backend untouched (velocity stays 0).
- **ND3 graph -> robot round-trip**: a `SetJointAngle` node writes the canonical joint angle (0.700 rad); FK on
  that angle moves the link marker **0.343 m** (>0.1). NEG-CTRL: a disconnected command never reaches the
  robot -> the joint stays at rest (0.000).
- **ND4 type safety**: feeding the Velocity port a `float` instead of a `glm::vec3` is rejected by
  `getInput<T>` (bad_any_cast -> nullopt) -- no mutation, no crash.
KRS_OVERNIGHT_BENCH **33/33**; exe verified newer than all sources. **Phases 0-5 COMPLETE; all gates green.**

## §AB ADVERSARIAL HARDENING OF GATES F/J/M/ND -- LANDED (2026-06-15)
A 4-agent adversarial review (one hostile skeptic per new gate, each tasked to make the gate PASS while the
feature is BROKEN) found two MAJOR could-pass-while-broken holes and several honesty gaps. All fixed; bench
stayed **33/33**.
- **GATE J -- vacuous neg-control path (MAJOR, fixed)**: the reject flags `rejOffset/rejTilt` were init TRUE
  and only flipped inside an import guard, so a SILENT import failure of the degenerate bore would pass the
  neg-control without ever calling the derivation -- and printed a fabricated `perp=0.0000 REJECTED` line.
  Fixed: flags init FALSE, require import success (`imports=ok` printed) AND derive-rejects AND the residual
  exceeds tol (`off=0.0048>tol`, `ang=0.018>tol`) -- rejection must be for the RIGHT reason. Also: J1 axis
  match is near-tautological by construction (both bores share the axis), so the load-bearing number is the
  ORIGIN (posErr 8.7e-10); J2 now independently checks the canonical `ptree` position channel (not just the
  axis it shares with J1); both bounds use `std::abs`.
- **GATE ND -- ND3 robot-motion was tautological (MAJOR, fixed)**: the old "FK moved 0.343 m" was a formula
  in the test file fed by the scalar the node just wrote; nothing in the engine consumed the joint angle.
  Fixed: a real engine node `RevoluteLinkFkNode` (registered, in the graph) now CONSUMES
  `JointComponent.currentPosition` and writes the moving link's `TransformComponent` via the shared FK; the
  gate reads `moved` straight off the link's ECS transform (0.343 m), so the graph genuinely moves a robot
  link in the ECS. ND4 type-safety now isolates the cause (same wiring writes with a vec3, only the float is
  rejected); ND2/ND3 disconnected controls assert the Registry input is explicitly unset.
- **Shared FK SSOT**: the revolute FK is now one engine definition `krs::kin::revoluteApply`
  (include/PhysicsHeaders/RevoluteFK.hpp), consumed by the MQTT bridge, the FK node, and the gates -- not a
  per-test formula.
- **GATE M (minor)**: M3's displayed `moved` is now derived from the RECEIVED telemetry (`tipRx - rest`), not
  a local constant, so the motion number is causally load-bearing; cmd/state use QoS 1 (no silent drop).
- **GATE F (minor)**: F2 now also checks the axis-LINE position (xy offset <1e-9), so "axis" means the full
  line; the neg-control is relabeled honestly as a centroid-average radius biased inward by the facet sagitta
  (not a least-squares fit). F1's ray-triangle path confirmed to use no AABB fallback (attack refuted).

## §AC HARD-FEATURE / STRESS / FUZZ / ROBUSTNESS GATES (F3/F5/J4/M5) -- LANDED (2026-06-15)
The make-or-break items from the original F/J/M spec that the first pass had NOT run -- now run, with real
numbers. KRS_OVERNIGHT_BENCH **37/37** (rigid KRS_BENCH still 7/7). TWO latent bugs were caught by these
gates and fixed.
### AC.1 GATE F3 -- hard-feature disambiguation (KRS_DISAMBIG_SELFTEST)
A 200mm box with a **3mm bore** (~0.07% of a face area). F3a bore-aimed rays -> the cylinder face (radius
3mm) **32/32** and large-face rays -> a plane **80/80** (the tiny feature is never lost to the dominating
face, nor vice-versa). F3b adjacent faces stay separated up to 90% of the way to their shared edge **38/38**.
F3c rays aimed exactly at edges/corners return a valid face or clean miss, **0 corrupt ids / 12/12** -- no crash.
### AC.2 GATE F5 -- dense-scene pick stress (KRS_DENSE_SELFTEST)
**25 bodies / 115,200 triangles**, 2327 ground-truth rays: **100.00%** correct at scale; picking latency
**avg 0.482 ms, max 1.888 ms** per pick. NB pickMesh is brute-force O(all triangles) with NO BVH/AABB
prune -- the reported latency is honest, and a spatial acceleration structure is the obvious scale upgrade
(documented, not yet built).
### AC.3 GATE J4 -- joint validation fuzz (KRS_JOINTFUZZ_SELFTEST) -- FOUND + FIXED A BUG
20,000 random feature x type x extreme-value cases (zero/huge/tiny axes, huge positions, zero radius, mixed
types), seeded for reproducibility. **First run: 1,473 CORRUPT graphs** -- a zero-length axisDir made
`glm::normalize` NaN, and since every `NaN > tol` early-out is false, `deriveRevoluteFromBores` RETURNED a
NaN frame (a corrupt canonical joint). FIX: guard non-finite / sub-`1e-6` axes (reject) at the top of the
derivation. Re-run: **0 corrupt, 0 bogus accepts**, 6354 accepted / 13646 rejected (both paths exercised).
### AC.4 GATE M5 -- MQTT robustness (KRS_MQTTROBUST_SELFTEST) -- FOUND + FIXED A BUG
**M5a** the broker is KILLED mid-run -> the engine survives (no crash) and **reconnects**, round-trip works
again. **M5b** a telemetry consumer services **128 distinct topics** (wildcard sub) in **2.87 ms** total --
bounded, so a physics tick interleaving MQTT isn't starved. **M5c** 9 malformed cmd payloads (`""`,
`not_a_number`, `1e999`, `0.5xyz`, binary, ...) are all **rejected, 0 non-finite poses**, then one valid
command still acts. The handler originally used `atof` (turns `"1e999"`->inf -> a non-finite link pose);
FIX: `strtod` with full-consumption + `isfinite` validation, reject otherwise. **All M5 PASS.**

## §AD NODE-SYSTEM SPRINT -- Phase 1 (in-node UI) + Phase 3 (library behavioral gates) -- LANDED (2026-06-15)
The existing node library has ~90 registered types but the only gate was "107 types instantiate" -- the loose
gate this sprint tightens (completeness is BEHAVIORAL: a node is done when its OUTPUT matches a reference).
- **Node params**: `Node` gained a `std::map<string,any> m_params` + `setParam/getParam/hasParam` + a numeric
  `getInputD()` -- tunable internal state an in-node widget writes and compute() reads (the headless-gateable
  layer of the in-node UI).
- **In-node UI framework** (`include/NodeHeaders/NodeWidgets.hpp` + `src/Nodes/NodeWidgets.cpp`):
  `buildControlWidget(node, controls)` turns a list of ControlSpec (dial/slider/spinbox/readout, each bound
  to a param) into a compact, FIXED-SIZE widget; every control writes `node->setParam(param,v); node->process()`.
  Sized to `estimateFootprint()` -- the SSOT the sizing gate asserts is bounded, so the gated number IS the
  rendered widget's bounds (fixes the "nodes expand massively" via setFixedSize + a hard cap).
- **Param-driven signal generators** (`src/Nodes/GeneratorNodes.cpp`): `gen_sine/square/triangle/saw` with
  freq/amp/phase/offset PARAMS + in-node dials (fills the Phase 3.2 gap -- the pre-existing signal_waveform
  has an internal type and no phase/offset/triangle).
### AD.1 GATE NODE-UI (`krs::nodes::runNodeUiGate`, KRS_NODEUI_SELFTEST)
NU1: a gen_sine's dial PARAM drives the output -- over 180 (freq,amp,phase,t) samples the output matches
amp*sin(2pi*f*t+phase) to **9.81e-07**. NEG-CTRL: a fresh gen on DEFAULT params, and changing another gen's
dial, leave this one's output unchanged (disconnected-widget inert). NU2: footprint bounded -- the real Qt
gen widget is **169x240 <= cap 220x300**; NEG-CTRL the uncapped (pre-fix) footprint for a 12-control node is
**h=414 > cap** (the cap is load-bearing). [render pixels = visual-confirm; the widget SIZE is gated on the
real widget.]
### AD.2 GATE NODE-LIB (`krs::nodes::runNodeLibraryGate`, KRS_NODELIB_SELFTEST)
Every EXISTING math/signal/logic node's output asserted vs closed-form over sampled inputs: **25 checks, 0
FAIL, worst err 1.69e-06** -- math_add/sub/mul/div/mod/pow/min/max/atan2/abs/sqrt/log/sin/cos/clamp/lerp/
greater_than/less_than/equals, the 4 new generators, and signal_waveform. NEG-CTRL (wrong-typed-input): a
math_add fed a STRING for A produces NO output (the node guards getInput<float>). Calculus nodes
(math_derivative/integral) take timestamped ProfiledData -- NOT gated here (flagged). KRS_OVERNIGHT_BENCH 39/39.

## §AE NODE-SYSTEM SPRINT -- Phase 2: auto-populating MQTT topic nodes (GATE NODE-MQTT) -- LANDED (2026-06-15)
`src/Nodes/MqttNodes.cpp`: `MqttSubscribeNode` (owns a libmosquitto client, pumps it in compute(), emits the
latest numeric value of its topic) + `MqttPublishNode` (publishes its input value to its topic). They are
AUTO-GENERATED from the canonical topic tree -- `autoRegisterMqttTopicNodes(robot, jointCount)` registers one
sub-node per `tt.jointState[i]` + one pub-node per `tt.jointCmd[i]` (rule 6: the same `buildTopicTree` the bus
uses), so new joints -> new nodes with no code. A startup registrar auto-populates the palette from the
canonical FANUC spec.
### AE.1 GATE NODE-MQTT (`krs::nodes::runMqttNodeGate`, KRS_NODEMQTT_SELFTEST) -- the keystone
A publish-NODE's INPUT value (0.6 rad) is published to `robot/FANUC/joint/0/cmd` over a REAL spawned
mosquitto broker; a robot handler applies it to the LIVE PhysX articulation (`setArticJointPositions` +
`singleStep`); the moved link's ACTUAL rotation from rest (read from `articLinkPoses`) is published on
`.../joint/0/state`; a subscribe-NODE receives it and emits **0.6000**, matching the commanded angle to
**err 1.0e-06 (<1e-4)**. This is the robot MOVING (live link angle 0.6000 rad), not a value sitting in the
editor. NEG-CTRL (severed node): with the publish-node not publishing, the new command (0.9) never reaches
the bus -> the subscribe-node never sees it. **NM2 coverage**: a runtime 3-joint robot auto-generates exactly
**6 nodes** (3 cmd + 3 state), each `createNode`-able. KRS_OVERNIGHT_BENCH **40/40**.

## §AF NODE-SYSTEM SPRINT -- Phase 4: controller knobs + tracking-lag fix + glass robot -- LANDED (2026-06-15)
- **The fix** (`include/PhysicsHeaders/ComputedTorque.hpp`): `krs::ctrl::computedTorque` -- inverse-dynamics
  feedback linearization `tau = M(q)[qdd_des + Kd*edot + Kp*e] + biasForces(q,qd,g)` using the CRBA mass
  matrix + RNEA bias. The soft PD only reacts to instantaneous error so it trails a fast command; computed
  torque feeds the desired accel/velocity forward and cancels the arm dynamics, so the closed-loop error
  obeys e''+Kd*e'+Kp*e=0 and tracks tightly. SSOT, reusable by any live tracker.
- **Knob nodes** (`src/Nodes/ControllerNodes.cpp`): `ctrl_goal_knob` (a goal joint-angle digital
  potentiometer, dial -> commanded angle) + `ctrl_vel_knob` (velocity setpoint), built on the dial framework.
### AF.1 GATE C-track (`KRS_CTRACK_SELFTEST`) -- the must-not-relax dynamic-tracking gate
Under a brisk cycling setpoint (80-step smoothstep ramps qA<->qB), peak DYNAMIC tracking error (commanded vs
achieved DURING motion): **soft PD (before) = 0.6077 rad** vs **computed torque (after) = 0.0174 rad** (bound
0.05) -- a 35x improvement. NEG-CTRL: the soft PD is the negative control and FAILS the bound (0.61 >= 0.05).
[The deferred GATE-D number was 0.23 on its specific move; this brisker move lags the soft PD 0.61 -- same
dynamic-lag class, clearly failing; computed torque fixes it.] PROVEN (live PhysX articulation, torque-driven).
### AF.2 GATE C-knob (`KRS_CKNOB_SELFTEST`)
A `ctrl_goal_knob` node's dial (0.5 rad) -> the node emits 0.5000 -> drives the LIVE joint, whose link reaches
the commanded angle: **FK err 4.89e-07 (<1e-4)**. NEG-CTRL: a disconnected knob (default 0) emits 0 -> no
command. PROVEN.
### AF.3 GATE C-glass (`KRS_CGLASS_SELFTEST`)
The glass robot's link transforms are driven by FK of the PLANNED joint config: glass vs planned-FK err
**0.0 (<1e-5)**, and planned differs from live by **1.003 m** (it is genuinely showing the PLAN, not the
current pose). NEG-CTRL: fed the LIVE values instead, the glass collapses to the current pose (plan==current,
err 0.0) -- proving it reads the plan. PROVEN for the transform DATA; the GlassComponent/GlassPass pixel
render is visual-confirm. KRS_OVERNIGHT_BENCH **43/43**.

## §AG NODE-SYSTEM SPRINT -- Phase 5: end-to-end canvas control program (GATE NODE-E2E) -- LANDED (2026-06-15)
A real canvas program built from the new node types drives the live robot:
`t -> gen_sine -> math_affine (potentiometer offset) -> joint command -> live PhysX link`. Every stage's
value is asserted (the causal-chain discipline): sine=0.3804 (= amp*sin(2pi*f*t+phase)), affine=0.5804
(= sine*gain+offset), live link=0.5804 (the robot reached the commanded angle), glass=0.5804 (the planned
config) -- **max err 6.68e-07**. NEG-CTRL (severed-chain): cutting any node breaks the chain at EXACTLY that
stage -- cut the sine input -> firstBreak=1, cut the sine->affine wire -> firstBreak=2, cut the affine->robot
command -> firstBreak=3. A new `math_affine` node (out = in*gain+offset, double, in-node sliders) is the
all-double adapter that lets the generator chain wire cleanly. KRS_OVERNIGHT_BENCH **44/44**.
**Node-system sprint Phases 1-5 COMPLETE.**

## §AH NODE-SPRINT ADVERSARIAL HARDENING -- closed the vacuous-gate class (2026-06-15)
A 7-agent adversarial review (one skeptic per node-sprint gate, tasked to "pass while broken", hunting
"node instantiates but does nothing" + "widget renders but drives nothing") found real holes in 6 of 7
gates. All fixed; bench stayed 44/44.
- **NODE-LIB (blocker: do-nothing node passed)**: `std::max(e, NaN)` drops the NaN, so a node whose
  compute() never calls setOutput ended with err 0 -> "ok". FIX: NaN-poison (`acc` sets err=9e9 on a
  non-finite output) + require **24/24 checks produced a finite output**. A dead node now FAILS.
- **NODE-UI + C-knob (blocker: dial->param binding untested)**: NU1/knob set the param in C++,
  bypassing the Qt dial. FIX: controls are tagged `krs_param`; the gate builds the REAL widget and calls
  `setValue(toTick(...))` on the bound QDial -> fires the connected lambda -> fromTick -> setParam ->
  process. NU1 freq-dial err 0.0, output err 8.8e-08. C-knob drives the live joint over {0.3,0.9,0.6}
  through the dial (FK err 8.96e-07), and severing knob->joint leaves the live link at rest.
- **NODE-MQTT (blocker: vacuous severed neg-ctrl + constant "moved")**: `moved=qCmd>0.1` read nothing;
  the severed control "rejected" via a stale 0.6 != 0.9. FIX: `moved` is the MEASURED live link rotation
  (0.6000); the severed control drives the SAME value with the node cut and asserts the **live robot
  stayed at 0.0000** (severing genuinely stops the motion).
- **C-glass (vacuous: FK(x)==FK(x) tautology, no product code)**: the gate compared a test-local writer's
  output to itself. FIX: added a real product method **`SimulationController::plannedLinkPoses(plannedQ)`**
  (FK at a planned config, independent of the live state) -- the glass/ghost robot consumes it. Gate: glass
  = plannedLinkPoses(planned) vs an INDEPENDENT oracle FK err 1.33e-07, vs the LIVE articulation 1.003 m;
  NEG-CTRL feeding the live config collapses glass->live; INDEPENDENCE: moving the live robot leaves
  plannedLinkPoses(planned) unchanged (0.0) -- proving it reads the plan, not the present.
- **NODE-E2E (major: tautological glass stage)**: stage 4 round-tripped a quaternion. FIX: it now reads
  the glass link angle from the product `plannedLinkPoses` (real FK), independent of the live robot.
KRS_OVERNIGHT_BENCH **44/44**; rigid 7/7. The "instantiates/renders but does nothing" class does not survive.

## §AI NODE-EDITOR FRONT-END WIRING SPRINT -- recon (2026-06-15)
Operator root cause: node bodies never render input widgets; nodes look like massive empty blocks. RECON
(QtNodes 3.0.11 under build/release/_deps/qtnodes-src; NodeDelegate bridges):
- **THE MOUNT BUG (ordering)**: QtNodes builds NodeGraphicsObject and calls `embedQWidget()` SYNCHRONOUSLY
  at construction (NodeGraphicsObject.cpp:55), which queries `NodeRole::Widget` -> `NodeDelegate::
  embeddedWidget()`. But the backend node + its widget are attached LATER, in MainWindow's `nodeCreated`
  handler (MainWindow.cpp:922-928 `createNode`+`setBackendNode`). So embeddedWidget() returns nullptr at
  mount time -> NO `QGraphicsProxyWidget` is created. MainWindow then emits `nodeUpdated`, but
  `BasicGraphicsScene::onNodeUpdated` (line 275) only `recomputeSize()` + `update()` -- it NEVER re-runs
  embedQWidget. recomputeSize now reads the (existing) widget's size and GROWS the node, but the widget is
  never mounted -> a big EMPTY box. Exactly the operator's report. FIX: the delegate must create its
  backend node + widget LAZILY on first access (nPorts/dataType/embeddedWidget), so the widget exists when
  embedQWidget runs at graphics construction (scene's onNodeCreated fires before MainWindow's handler).
- **No per-input-port widgets**: QtNodes has only ONE `embeddedWidget()` per node (PortRole has no widget
  role). Per-input editing = one container with a field per input port, hidden when the port is connected
  (`AbstractGraphModel::connections(nodeId, In, idx)` -> empty set = unconnected).
- **Unconnected-input data gap**: `getInput<T>` reads only `port.packet` (set by a connection). An
  unconnected port has no literal value a widget can write. NEED: a per-port literal value that getInput
  falls back to (the widget writes it).
- **Type compat**: `DataFlowGraphModel::connectionPossible` requires `dataType().id_out == id_in`, where id
  = `Port.type.name`. New nodes use "double", old use "float" -> they CANNOT connect. NEED a consistent
  numeric type tag.
- **No graph tick**: evaluation is push-only (setInData->process). No timer re-evaluates time nodes. NEED a
  live time-source node + a tick.

## §AJ NODE-EDITOR WIRING -- mount fix + per-input widgets + type/time (2026-06-15)
- **THE MOUNT FIX**: `NodeDelegate` now creates its backend node + embedded widget LAZILY on first access
  (`ensureBackend()` called from nPorts/dataType/embeddedWidget), so the widget exists when QtNodes runs
  `embedQWidget()` at NodeGraphicsObject construction -> the `QGraphicsProxyWidget` is actually created and
  the widget MOUNTS. MainWindow's handler no longer re-creates the node (it would have replaced the mounted
  one); it just injects the Scene. This is why nodes previously rendered as big empty boxes (size grew to
  the never-mounted widget). [OPERATOR VISUAL-CONFIRM]
- **PER-INPUT WIDGETS + literal model**: `Port` gained a `literalValue`; `Node::setPortLiteral<T>` writes it;
  `getInput<T>` falls back to it when the port is unconnected. `populateEmbeddedWidget` now mounts a typed,
  labelled control per editable input port (double/float->spinbox, int->spinbox, bool->checkbox, vec3->3
  fields), tagged `krs_input_port`, bound to the literal. An Int/float node now HAS a field. [VISUAL-CONFIRM]
- **TYPE unification**: `NodeDelegate::dataType` maps double/float/int/bool -> id "number", glm::vec3 ->
  "vec3", so signal/math/logic nodes interconnect (QtNodes connects on id match).
- **LIVE TIME**: `time_source` node emits std::chrono wall-clock; a 30Hz QTimer in MainWindow re-evaluates
  time-source nodes each frame so downstream time-parametric nodes move. [VISUAL-CONFIRM the animation]
### Gates
- **GATE INPUT-BIND** (`KRS_INPUTBIND_SELFTEST`): **62/62** input-bearing node types MOUNT all their input
  widgets (built via the delegate's body path); **30/30** fully-editable nodes with outputs: driving the
  mounted widget changes the output; curated formula proofs (add/mul/sub) **3/3**. NEG-CTRL: an unbound bare
  widget changes nothing. `util_script` reported as a no-op-stub (compute() is empty by design). PROVEN at
  the binding layer; the on-screen MOUNT is OPERATOR VISUAL-CONFIRM.
- **GATE TYPE** (`KRS_TYPE_SELFTEST`): **6/6** -- number->number connects, number->vec3 and number->handle
  blocked.
- **GATE TIME** (`KRS_TIME_SELFTEST`): live time advanced 0->0.150s; sine 0->0.81 tracks sin(2pi*t); NEG-CTRL
  disconnected -> constant. KRS_OVERNIGHT_BENCH **47/47**.

## §AK NODE-EDITOR -- GATE CONNECT-AND-CONTROL + numeric coercion (2026-06-15)
- **Numeric coercion**: `getInput<T>` for arithmetic T now coerces a "number" port's payload (double/float/
  int/bool interchange) -- so a float-reading node (math_add) accepts a double-carrying wire (gen_sine.Out).
  Without this the unified type id let nodes CONNECT but the any_cast failed and no data flowed. `Node::
  clearInputPacket` models a severed wire.
- **GATE CONNECT-AND-CONTROL** (`KRS_CONNECTCTRL_SELFTEST`): a robot-control program built by WIRING nodes
  -- `time_source -> gen_sine -> math_add(.A wired, .B = 0.20 set THROUGH the mounted spinbox) -> joint
  command -> live PhysX link`. Driven by the LIVE time source. Result: time 0.121 -> sine 0.1488 ->
  add 0.3488 -> live link **0.3488** (max err **2.27e-07**). NEG-CTRL (severed-wire): cut time->sine
  breaks at stage 2, cut sine->add at stage 3, cut add->robot at stage 4 (localizes exactly). The end-to-end
  proof the editor is wireable AND inputs are settable through the body. KRS_OVERNIGHT_BENCH **48/48**.

## §AL NODE-EDITOR ADVERSARIAL HARDENING -- real QtNodes paths + bool/Trigger fix (2026-06-15)
A 4-agent adversarial review (pass-while-broken, hunting widget-never-mounted + node-wont-connect) confirmed
the MOUNT FIX is SOUND ("a non-null container with tagged children genuinely becomes the proxied widget on
screen; no hidden reject branch -- cannot pass while the widget is absent-on-screen") but found:
- **REAL BUG (bool->Trigger)**: `dataType` mapped bool->"number", and every node has a base-class bool
  `Trigger` input -- so a number output could be wired into any Trigger (a float-into-bool confusion).
  FIX: bool gets its own id "bool"; number->Trigger now BLOCKS, bool->Trigger (a logic signal triggering)
  still connects.
- **gates re-implemented connectionPossible instead of calling it**: TYPE/TIME/CONNECT faked connections
  with raw packet copies and a hand-rolled id== check. FIX: all three now drive a REAL
  `QtNodes::DataFlowGraphModel` (same registry MainWindow uses) -- `connectionPossible` (id + vacancy +
  policy) and `addConnection` -> `setInData` propagation through the actual editor code path.
### Hardened gates
- **GATE TYPE**: **8/8** via the real `DataFlowGraphModel::connectionPossible` -- number->number connect;
  number->vec3, number->handle, number->Trigger(bool) BLOCK; bool->Trigger connect.
- **GATE TIME**: a REAL edge time_source->gen_sine; ticking the time source (the 30Hz path) propagates the
  live time through the model into the connected sine (s 0.0003 -> 0.8096, tracks sin(2pi*t)); a
  DISCONNECTED sine stays constant.
- **GATE CONNECT-AND-CONTROL**: the chain is built with REAL `addConnection` edges (time->sine->add),
  `connectionPossible` asserted; add.B set THROUGH the mounted spinbox; ticking propagates through the edges
  to drive the live robot (link 0.3475, err 6.75e-07); omitting an edge localizes the break (2,3,4).
KRS_OVERNIGHT_BENCH **48/48**. The mount itself remains OPERATOR VISUAL-CONFIRM.

## §AM NODE-ECOSYSTEM SPRINT -- RECON (2026-06-15, before editing)
Four-area recon mapping the system this sprint touches. file:line evidence, no edits.

### Boot demo / joint-command OWNERSHIP (the two writers)
- **Writer #1 (the hardcoded demo, to REMOVE)**: `SimulationController::tick()` lines 544-554 -- the
  `if (m_articDemoDrive && articDofCount()==4)` block computes 3 hardcoded sines and calls
  `setArticJointPositions(q)` every tick. Enabled at `MainWindow.cpp:712`
  (`m_simulation->setArticulationDemoDrive(true)`) inside the `bootFanuc` path.
- **Writer #2 (the node path)**: `SetJointAngleNode` (`physics_set_joint_angle`, BridgeNodes.cpp:43-53)
  writes `JointComponent.currentPosition` in the ECS registry.
- **THE GAP (confirmed)**: the FANUC articulation is driven ONLY by `setArticJointPositions(q)` (a 4-vector,
  SimulationController.cpp:874). `writeBackArticulationViz()` (SimulationController.cpp:1288) goes the OTHER
  way (live articulation pose -> viz solids). NOTHING reads `JointComponent.currentPosition` back into the
  articulation. The FANUC has NO per-joint `JointComponent` entities (FanucArticulation.cpp drives a raw
  4-vector + a viz mapping). So a node graph does NOT currently drive the live robot.
- **DESIGN**: add a registry-singleton `ArticulationCommandComponent` (entt ctx) holding per-DOF target +
  driven flags; a `physics_articulation_drive` node (Angle + Joint-index inputs) writes it via its injected
  `m_scene`; `SimulationController::applyArticulationCommands()` reads it each tick and calls
  `setArticJointPositions`. This bridge REPLACES the demo block as the SOLE writer. The boot spawns a REAL
  editor graph (`graphModel->addNode/addConnection`: time_source -> gen_sine -> physics_articulation_drive)
  -- the same path a user builds. FANUC J1 limit ~+-pi (real data); the demo amplitude stays well within.

### Half-mounted FRAME (the screenshot bug)
- The caption + node rect + ports are ALWAYS painted (DefaultNodePainter.cpp:18-35); caption defaults visible
  (NodeDelegateModel::captionVisible()=true, not overridden). `recomputeSize`
  (DefaultHorizontalNodeGeometry.cpp:31-60) always reserves caption height, and `widgetPosition` (147-165)
  places the embedded widget BELOW the caption -- so a widget cannot cover the caption.
- The base `Node()` ctor (Node.hpp:241-243) adds a `Trigger` input to EVERY node, so any node that does not
  `m_ports.clear()` has >=1 port and renders framed+wireable. The ONLY confirmed 0-port type is
  `util_comment` (UtilityNodes.cpp: `m_ports.clear()` with nothing re-added). Static nodes clear then add a
  `Value` OUTPUT (1 port -> framed). So "no ports / can't be wired" == nPorts==0.
- **APPROACH**: GATE FRAME instantiates EVERY registered type through the real `DataFlowGraphModel` and
  asserts in+out port count > 0 (report N of M; neg-ctrl: a deliberately 0-port node is CAUGHT). Pruning the
  category-C `util_comment` removes the one frameless type. The embedded container is also set transparent
  (`WA_NoSystemBackground`/translucent) so an opaque body can never paint over the frame. Rendered frame =
  OPERATOR VISUAL-CONFIRM.

### Node-draft inventory (for CLASSIFY/PRUNE)
~97 registered types across src/Nodes/*.cpp. Fully-implemented: all math/trig/linalg/state-space/signal,
physics+kinematics bridges, time_source, MQTT pub/sub, controller knobs, generators. STUBS: perception_*
(5, need PCL/OpenCV -> B), sensor_pose/sensor_float (B), ai_run_inference (B), util_comment/util_reroute/
util_script (no-op -> C), static_* (17, output port but no value widget). Visualization viz_*/mon_* are
real but display-only. Full A/B/C table lands in the CLASSIFY commit.

### UI->physics THREADING
- Physics ticks on the MAIN/GUI thread: `m_masterRenderTimer` (33ms) + the viewport `frameSwapped` signal
  both call `m_simulation->tick()` (MainWindow.cpp ~1007-1029).
- A dial edit's `valueChanged` lambda (NodeDelegate.cpp:276-298) calls `recomputeAndPropagate()`
  SYNCHRONOUSLY on that same GUI thread (process() the node + emit dataUpdated) -- while it runs the QTimer
  cannot fire, so the physics tick is delayed. No mutex; the stall is the shared-thread inline work.
- Async idioms to mirror: SLAM `moveToThread`+queued signals (SlamManager.cpp), HilClock `std::thread`+
  atomics. **DESIGN**: post dial edits to a lock-free/std::mutex command queue drained at the top of
  `tick()`, so the UI handler returns immediately. GATE THREAD measures tick rate idle vs under hammering;
  neg-ctrl is the old synchronous path.

## §AN NODE-ECOSYSTEM SPRINT -- gates + node classification (2026-06-15)
Bench 48/48 -> **56/56**. New gates: FRAME, VIS, DEMO-GRAPH, OWNERSHIP, PID, FILTER, THREAD, DRAGDROP.

### SPINE: the default demo IS a real node graph (one joint writer)
The hardcoded kinematic demo sweep (SimulationController::tick) is GONE. The FANUC is driven by the default
editor graph `time_source -> gen_sine -> physics_articulation_drive` (spawnDefaultRobotGraph, the same helper
MainWindow + the gates use). A registry-ctx ArticulationCommandComponent is the bus; applyArticulationCommands()
reads it each tick. No second writer -> no conflict. GATE DEMO-GRAPH: editing the canvas sine's amp DIAL
changes the live robot's motion (follows the new amp, not the old). GATE OWNERSHIP: joint==node command
(FK<1e-4), no-graph->rest neg-ctrl, rapid DOF-switch robust.

### FRAME / VIS / control library / THREAD / DRAGDROP
- FRAME: 127/127 registered types expose connection-usable ports via the real model (clickable frame =
  visual-confirm). VIS: 7-seg readout (digits/decimals) + arc gauge displayed value matches input.
- Control library: control_pid (vs reference step response, P-only retains offset), filter_kalman_1d (vs
  reference recursion + RMSE 0.66->0.016), signal_lowpass (vs EMA), filter_moving_average (vs boxcar).
- THREAD: NodeEditQueue defers+coalesces UI edits, drained once/frame; async keeps 96% of tick rate, old
  synchronous path drops to 48%.
- DRAGDROP: catalog event-filter drag start + shared instanceDroppedNode; a drop instances the correct
  typed node with ports+widgets (6/6).

### Node-draft classification (A = implementable+gated this/prior sprint, B = needs ML/subsystem -> kept as
###  explicit ungated stub or deferred, C = doesn't fit the control canvas -> removed)
- **A (functional + gated):** math_* (arithmetic/trig/compare/calculus), linalg_* (vector/matrix/solvers/
  RK4), control_* (controllability/observability/pole-placement/LQR), statespace_* (LTI/sim/regulator/
  analysis/monitor), signal_* (lowpass/dot/transform/waveform), gen_sine/square/triangle/saw, math_affine,
  util_switch/switch_case/latch/delay/reroute, ai_set/get_blackboard, ai_bt_sequence/selector, time_source,
  ctrl_goal_knob/vel_knob, mqtt_pub/sub, world_scene_context, physics_get_velocity/apply_force/
  set_velocity/check_overlap/set_joint_angle, kinematics_revolute_link_fk, sensor_imu_unpacker, AND the
  sprint's NEW: control_pid, filter_kalman_1d, filter_moving_average, viz_numeric_readout, viz_dial_gauge
  (now widget-backed), physics_articulation_drive. Gated by NODE-LIB/PID/FILTER/VIS/FRAME/INPUT-BIND/etc.
- **B (real project -- needs a subsystem; kept as labeled stub / deferred, NOT claimed functional):**
  perception_downsample/remove_outliers/segment_plane/to_grayscale/detect_edges (PCL/OpenCV),
  sensor_pose/sensor_float (data-manager hookup), ai_run_inference (trained model), ai_bt_action (BT
  runtime), util_script (scripting backend), the matrix control_kalman_filter + EKF/UKF (too complex to
  gate this sprint -> the gated estimator is the scalar filter_kalman_1d), static_* constants (functional
  source but lack a value-editor widget -> deferred enhancement), interaction_* draw/raycast/key/print
  (editor/debug tooling, needs the interaction subsystem).
- **C (doesn't fit a wireable control canvas -> REMOVED):** util_comment (a 0-port "non-functional box"
  that rendered as the screenshot's frameless bare panel).
RULE held: a node ships as functional ONLY if gated; B nodes are reported as stubs/deferred, never as done.

## §AO NODE-UI + PERF DEBUG SPRINT -- RECON/PROFILE (2026-06-15, before fixing)

### BUG 1 (frameless nodes): NOT reproduced -- the graphics frame IS built + rendered
Built the REAL NodeGraphicsObject via a CustomDataFlowScene and dumped geometry for every node type
(KRS_NODEPROF_SELFTEST). For ALL types (incl. the "suspect-frameless" gen_sine / ctrl_goal_knob / control_pid):
GO=yes, captionRect.height=16, valid boundingRect, ports positioned on the node boundary (inPort0=(0,39),
outPort0=(width,39)), and the embedded widget sits BELOW the caption (wpos.y=26..37, caption baseline ~21).
Rendered time_source.png + gen_sine.png to PNG: BOTH show the full frame -- title ("Sine Gen"), ports
(Hold/t/Out), and body. So the directive's hypothesis ("heavy nodes fail to build the NodeGraphicsObject /
frame") is refuted by the evidence; the prior sprint's transparent-container fix already resolved the
widget-covering-the-frame issue. The real remaining UX cost is SIZE: heavy nodes are large (gen_sine
286x435) because every non-source node mounts the 180x120 ExecutionControlWidget ("Execution Mode" combo).
GATE FRAME-GFX still lands as the missing "one layer up" assertion (graphics object + caption + boundary
ports exist for every type), so a future regression that drops the frame is caught.

### BUG 2 (catastrophic eval): PROVEN -- the per-eval scene cascade
tickRobotGraph -> NodeDelegate::recomputeAndPropagate -> Q_EMIT dataUpdated -> DataFlowGraphModel
onOutPortDataUpdated -> setInData (downstream) -> inPortDataWasSet -> BasicGraphicsScene::onNodeUpdated
-> recomputeSize (queries the proxy widget height -> layout) + node->update() (REPAINT the heavy proxy
widget) + moveConnections. This fires PER downstream node PER eval, with NO dirty-tracking. Measured eval
cost over a chain of N gen_sine nodes:
  N= 5: WITH scene 0.206 ms/eval, WITHOUT 0.003 ms  (scene overhead 0.203 ms)
  N=15: WITH scene 0.664 ms/eval, WITHOUT 0.011 ms  (scene overhead 0.652 ms)
  N=30: WITH scene 1.150 ms/eval, WITHOUT 0.024 ms  (scene overhead 1.126 ms)
The scene cascade is ~50x the pure eval and GROWS with N; headless this is recomputeSize-only (no real
paint), so the live app's actual proxy-widget repaints push it to the reported ~45ms. The math itself is
free (0.024 ms for 30 nodes). FIX: evaluate the graph QUIET (process + backend data propagation, NO QtNodes
dataUpdated -> no scene repaint) at the control rate; refresh display widgets only when their value changed
AND at a capped ~30-60Hz UI rate (separate from eval). Viz nodes must stop calling widget update() inside
compute(). FEATURE: an eval-rate control (30Hz..20kHz) with the UI repaint hard-capped independently.

## §AP NODE HOVER BUGS -- ONE root cause (2026-06-16)
RECON (before fixing): neither hover bug lives in a hover handler.
- QtNodes NodeGraphicsObject::hoverEnterEvent (NodeGraphicsObject.cpp:298) only raises z-order, sets the
  hovered flag, and calls update(); hoverLeaveEvent restores z + clears the flag. DefaultNodePainter::
  drawNodeRect (DefaultNodePainter.cpp:53) uses hovered() ONLY for the boundary PEN width -- the gradient
  FILL brush is drawn every paint regardless of hover. So the painter never makes the background vanish.
- ExecutionControlWidget (ExecutionControlWidget.cpp:71) setVisible toggles only the EDGE combo when
  policy==Triggered; the Execution-Mode (policy) combo is always added, never hover-gated.
- The ONLY transparency mechanism was `WA_TranslucentBackground` on the embedded container (NodeDelegate.cpp
  populateEmbeddedWidget, the prior FRAME-VIS sprint). In a QGraphicsProxyWidget the ARGB-backing-store path
  produces compositing artifacts on the hover-triggered update(): (A) the node's frame fill behind the
  widget gets alpha-eaten and is not restored on hover-leave; (B) child combo boxes do not render until a
  hover repaint forces them. ONE root cause -> BOTH symptoms. The prior fix conflated the NODE frame
  background with the EMBEDDED WIDGET background.
FIX: drop WA_TranslucentBackground; make ONLY the container background transparent via a scoped
`QWidget#krsNodeBody { background: transparent; }` stylesheet (does not cascade to the opaque child controls,
no ARGB compositing). Verified by render: gen_sine still shows title + ports + Execution-Mode combo + dials
through the transparent gaps, no opaque blob. GATE HOVER-INTEGRITY asserts the container is NOT
WA_TranslucentBackground (the frame-eating attribute) + the exec control is visible, before/after a
synthetic hoverEnter/Leave; NEG-CTRL: a WA_TranslucentBackground container + a hidden combo are both caught.
Live hover persistence = OPERATOR VISUAL-CONFIRM.

## §AQ NODE FRAME BUG A (the real cause) + clean-math fields (2026-06-16)
The §AP transparency fix fixed the combo (Bug B) but NOT the blank frame (Bug A). Operator evidence:
small nodes (time_source/drive_joint) render fine; the LARGE nodes (sine gen, PID) show a blank FRAME --
until you zoom ALL the way out, then they paint. TWO causes, not one.
- ROOT CAUSE (Bug A): NodeGraphicsObject.cpp:34 `setCacheMode(DeviceCoordinateCache)` + a drop-shadow
  QGraphicsEffect (lines 40-47). Both render the item to an offscreen DEVICE pixmap (node size x dpr x
  zoom). For large nodes on a hi-DPI (4K) screen that pixmap exceeds the max pixmap/GL-texture size -> the
  FRAME paints blank (the embedded proxy widget, a separate child item, still renders); zooming out shrinks
  the device size under the limit -> the frame appears. Earlier headless scene.render() bypassed the cache,
  so the prior renders never reproduced it.
- FIX: CustomDataFlowScene forces every node's NodeGraphicsObject to NoCache + setGraphicsEffect(nullptr)
  (on nodeCreated AND for any pre-existing nodes in a loaded graph). Direct, unbounded paint -> the frame
  renders at every zoom. GATE ZOOM-VISIBLE: 127/127 NoCache+no-effect GUARANTEE (no device pixmap to
  overflow) + EVERY node's frame title painted at both terminal-zoom bounds 0.3x/2x (254/254). NEG-CTRL: an
  un-fixed (base-scene) node carries DeviceCoordinateCache + the effect.
- CLEAN-MATH FIELDS: the static constant nodes had createCustomWidget = nullptr (un-settable value). Now
  float/double->spinbox, int->spinbox, bool->checkbox, string->lineedit, glm vec2/3/4->N spinboxes; editing
  posts to the NodeEditQueue (per-component keys for vectors so deferred edits don't coalesce), sets value,
  re-evaluates. Static nodes override needsExecutionControls()->false (a constant must always emit; no
  Triggered footgun; smaller). GATE STATIC-CONST: 6/6 + a deferred-path test; matrix/quat/Eigen deferred.
Bench 60/60 -> 62/62.

## §AR SYNTHETIC SENSOR PIPELINE -- architecture (2026-06-16, recon before coding)
A self-contained, headlessly-gateable module: src/Sensors/*.cpp + include/SensorHeaders/*.hpp (CMake glob
SENSOR_SOURCES + include dir added). Distributional/statistical fidelity (variance curves, conservation,
reprojection error) -- NOT pixels. Reuses ImuDataPoint/ImuCalibration (components.hpp), Eigen+GLM, the
env-trigger + KRS_OVERNIGHT_BENCH gate harness. No global param registry exists -> the SensorProfile IS the
provenance-tagged param substrate (data separate from model code, per 0.1).

### Three layers (generated in causal order)
- L1 TRUE WORLD: clean ground truth. Physics/interaction act on this. Sensors never read it directly.
- L2 RECONSTRUCTION UNCERTAINTY (world-space, baked, static per scene): a GPIS (mu, sigma^2) field = L1
  corrupted by the sensor SPATIAL error model (Phase-2 depth structured error integrated over a recon). The
  robot's BELIEF reads this. High sigma where poorly observed; holes where depth holes occur.
- L3 LIVE SENSOR NOISE (camera-space, per-frame, fresh): shot->read->quantize (RGB) / structured depth noise,
  applied at render time from the current viewpoint onto the L2 belief surface.
- SHARED MATERIAL FIELD (the CORRELATION requirement): L2 sigma and L3 dropout are BOTH conditioned on the
  same per-surface material field (reflectivity/specularity, angle, range) -- a black/specular patch is BOTH
  a recon hole (L2 high sigma) AND a live dropout (L3). Not independent draws. MaterialSample is the shared
  cause both layers consume.
- Composable + toggleable: a SceneConfig declares active layers (L1-only / L1+L3 / all three).

### Module map (built phase by phase, each gated)
- SensorProfile.hpp -- Param<T>{value, Provenance(datasheet|second_source|defaulted_pending_calibration)};
  D456 datasheet numbers + tagged defaults for RGB/depth/IMU. The substrate the model code consumes.
- Stats.{hpp,cpp} -- mean/variance, variance-vs-signal slope (linear fit), histogram, RMS-vs-range fit,
  Allan variance, hole rate, autocorrelation. Self-tested on analytic inputs.
- CameraModel.{hpp,cpp} -- pinhole K (from focal length+sensor) + Brown-Conrady (k1,k2,k3,p1,p2); project +
  round-trip.
- Layer3Noise -- RGB shot(Poisson, signal-dependent) -> read(Gaussian, signal-independent) -> quantize.
- DepthModel -- stereo geometry (baseline 95mm, depth-FOV(Z), invalid-left-band, per-res min-Z) + STRUCTURED
  error: quadratic(~Z^2) range, material-conditioned holes, edge flying pixels, IR-speckle.
- ImuModel -- STATEFUL: white(noise density)+bias instability(1st-order Gauss-Markov)+rate random walk+
  scale/axis-misalignment+temp bias; state carried frame-to-frame.
- Layer2Gpis -- the (mu,sigma^2) field from L1 + depth spatial error + the shared material field.
- Composition + TransferHarness.
### Gate plan (each = measured number + the NAIVE model as the FAILING neg-control)
GATE 0 (harness self-test), INTRINSICS (<0.5px; neg pinhole-no-distortion), NOISE-STATS (Poisson slope; neg
fixed-Gaussian), DEPTH-STRUCT (Z^2 fit + holes + flying pixels; neg clean+Gaussian), IMU-ALLAN (white slope
+ bias floor + drift; neg per-sample-Gaussian), L2-UNCERTAINTY (sigma contrast + hole co-location; neg
uniform-sigma), COMPOSE (true-vs-belief divergence + L2/L3 material correlation; neg independent-draw), E2E
(in-context + conservation), REAL-TRANSFER. HONESTY: every gate above is INTERNAL-CONSISTENCY or
CONSERVATION and fully checkable tonight; the REAL-TRANSFER gate needs a real D456 capture (not available)
-> the transfer-validation harness is BUILT but gated only vs a SECOND SYNTHETIC instance, labeled
"self-consistent, NOT validated against real hardware -- awaiting operator capture." Never claim transfer.

### Phase 0 -- DONE (commits 83c3b18, hardened cacdd41). SENSOR GATE 0 green; bench 63/63.
SensorProfile (provenance-tagged), SensorStats harness, GATE 0 (self-test + neg-ctrl + round-trip). The
adversarial review found 5 ways to pass-while-broken (vacuous conservation, slope-only Allan, inline neg-ctrl,
round-trip theater, unverified Bessel) -- all closed. Conservation now uses a real harness primitive; Allan
asserts level+slope; one isPoissonByVarMean() discriminator serves both accept+reject; round-trip is a 5-point
var=slope*sigma^2 regression (slope 1.0+-0.02 catches a 3% bug). Provenance/arithmetic audited clean.

## §AR-1 PHASE 1 -- RGB CAMERA (intrinsics + Layer-3 noise), design before coding (2026-06-16)
Pure CPU math, no GL. New files: include/SensorHeaders/CameraModel.hpp + src/Sensors/CameraModel.cpp
(intrinsics+distortion), Layer3Noise in the same or RgbNoise.{hpp,cpp}, and SensorGate1.cpp with the two
gates. Recon confirmed: no existing CPU pinhole+distortion (V.2 FanucRenderGate projects via GL P*V -- a
cross-check, not reuse); reuse SensorProfile (RgbProfile already has focalMm/pixelUm/k1..p2/noise stack), plain
std::vector for buffers, raw <random>, GLM available.

### Intrinsics K (from focal length + sensor, NOT hand-set fx)
fx = focalMm / (pixelUm*1e-3)  [px];  fy = fx (square pixels assumed; D456 RGB);  cx=(W-1)/2, cy=(H-1)/2.
With focalMm 1.93, pixelUm 3.016 -> fx = 640.0 px, matching (W/2)/tan(fovH/2) for 90deg -- the provenance
cross-check (pixelUm was DEFAULTED to make these agree). K = [[fx,0,cx],[0,fy,cy],[0,0,1]].

### Brown-Conrady distortion (k1,k2,k3 radial; p1,p2 tangential)
Forward (normalized x,y -> distorted): r2=x^2+y^2; radial = 1 + k1 r2 + k2 r2^2 + k3 r2^3;
xd = x*radial + 2 p1 x y + p2 (r2 + 2 x^2);  yd = y*radial + p1 (r2 + 2 y^2) + 2 p2 x y.
Pixel = (fx*xd + cx, fy*yd + cy). Inverse (pixel -> ray, undistort) by iterative fixed-point (Newton not
needed at these coeffs): start from distorted normalized, iterate x = (xd - tangential)/radial ~10 iters.

### GATE INTRINSICS (reprojection round-trip <0.5px; neg-ctrl pinhole-no-distortion fails at edges)
Sample >=50 normalized points spanning the FOV incl. corners. Forward-distort -> pixel -> undistort ->
re-project. Assert max round-trip error < 0.5px across all >=50 (tight: corners are where distortion is
largest). Cross-check fx==640 vs the FOV formula (<0.1px). NEG-CTRL: a pinhole model (k*=p*=0) reprojecting
the SAME distorted pixels has error that GROWS toward the edges and EXCEEDS a threshold at the corners
(>2px) -- proving distortion is doing real work and the gate is sensitive to it (a clean pinhole FAILS where
a real lens bends). Center error stays ~0 for both (distortion vanishes on-axis) -- so the discriminator is
specifically the edge/corner behavior.

### Layer-3 noise: shot (Poisson, signal-dependent) -> read (Gaussian, signal-independent) -> quantize
For a pixel with clean signal S in DN [0..fullScaleDN]: electrons e = S*photonsPerDN; shot ~ Poisson(e)
-> back to DN (/photonsPerDN) gives shot variance = S/photonsPerDN (scales WITH signal). Then add read
~ Gaussian(0, readNoiseDN) (constant floor, signal-INDEPENDENT). Then quantize to bitDepth and clamp.
Total per-pixel variance(S) = S/photonsPerDN + readNoiseDN^2 -- an AFFINE function of S: slope 1/photonsPerDN,
intercept readNoiseDN^2.

### GATE NOISE-STATS (shot variance scales with signal; read = constant floor; neg fixed-Gaussian fails)
For a sweep of constant signal levels S_i, draw many noisy samples per level, measure variance(S_i). Linear-fit
variance vs S: slope ~ 1/photonsPerDN (>0, significant), intercept ~ readNoiseDN^2 (the read floor). Assert
slope matches 1/photonsPerDN within tol AND intercept matches readNoiseDN^2 within tol AND r2>0.98 (affine).
At S=0 (dark frame) variance ~ readNoiseDN^2 (pure read). NEG-CTRL: a fixed-Gaussian model (same total
variance at mid-signal but signal-INDEPENDENT) has variance-vs-signal slope ~0 -> FAILS the Poisson
signal-dependence test (slope not significantly >0). This is the discriminator: real sensor noise grows with
light; the naive fixed-Gaussian does not. Both can match at one operating point -- the SLOPE separates them.

### Phase 1 -- DONE (commit 4855de3, hardened 7f6009b). GATE INTRINSICS + NOISE-STATS green; bench 65/65.
The adversarial review (exploits proven by simulation) found 3 CRITICAL pass-while-broken holes: the
intrinsics round-trip was self-consistency only (a wrong lens passed at 0.0px); NOISE proved variance affine
but never Poisson (a signal-dependent Gaussian passed). All closed: an INDEPENDENT hand-computed forward-
distortion anchor pins the lens to Brown-Conrady; the raw shot stage is probed for the defining Poisson
property (integer electrons + Fano~1); NEG-CTRL B (signal-dependent Gaussian) now demonstrably passes the
affine PTC yet fails discreteness. fy/vertical-FOV constrained; slope tol 15%->5%.

## §AR-2 PHASE 2 -- STEREO DEPTH (geometry + structured error), design before coding (2026-06-16)
Pure CPU. New: include/SensorHeaders/MaterialField.hpp (MaterialSample + stereoDropProb -- the SHARED cause
L2 sigma + L3 dropout will both consume in Phase 4/5), DepthModel.{hpp,cpp}, SensorGate2.cpp. Recon: build
fresh (DepthProfile is data-only); MaterialComponent (components.hpp:98-200, metallic/roughness/specular/
albedo) is what the shared field maps from later; SdfBaker/VoxelMap reserved for Phase-4 L2. Units: meters
internally, mm at boundaries.

### Stereo geometry (Z from disparity -- the noise is DERIVED, not a free knob)
Depth intrinsics fx_d = (W/2)/tan(fovH/2) = 640 px (W=1280, fovH=90). disparity d = fx_d * baseline_m / Z_m.
Sub-pixel matching error sigma_d = subpixelErr px (Gaussian on disparity). Propagating d -> Z = fx_d*b/d:
sigma_Z(Z) = Z^2 * sigma_d / (fx_d * baseline_m)  -- the Z^2 law falls out of the geometry, coefficient
= subpixelErr/(fx_d*b) ~ 0.00164 /m (sigma_Z_mm ~ 1.64 * Z_m^2). min-Z per resolution (520mm @1280x720)
enforced: true Z < minZ -> invalid. IR speckle = a small additive Z floor (speckleStdMm), kept separable.

### Structured error (the differentiators -- each tested, each with the clean/Gaussian model as neg-ctrl)
- QUADRATIC range: sigma_Z ~ Z^2 (above), the defining stereo signature. NEG: a constant-Gaussian depth
  (sigma_Z = const) has power-fit exponent ~ 0.
- MATERIAL-CONDITIONED HOLES: stereoDropProb(material) high for specular (~holeRateSpecular 0.35) and dark
  (low albedo), ~holeRateLambert 0.01 for bright Lambertian. NEG: clean model -> ~0 holes everywhere.
- EDGE FLYING PIXELS: at a depth discontinuity (zNear|zFar), a fraction (flyingPixelRate) of edge pixels
  interpolate to INTERMEDIATE depths (strictly between near and far). NEG: flat region -> 0 intermediates;
  clean model -> 0 flying pixels at edges too.

### GATE DEPTH-STRUCT (measured numbers + the NAIVE clean+Gaussian as the FAILING neg-control)
(1) RMS-vs-range power-fit exponent ~ 2 (not constant) AND coefficient ~ derived subpixelErr/(fx*b);
(2) hole rate nonzero on specular AND dark, ~holeRateLambert on bright Lambertian; (3) flying-pixel fraction
~ flyingPixelRate at edges, 0 on flats; (4) min-Z enforced (true Z<minZ -> invalid). NEG-CTRL (clean+fixed-
Gaussian depth): power-fit exponent ~ 0 (FAILS quadratic) AND ~0 holes (FAILS material test). Discriminators:
the Z^2 slope and the material-conditioned dropout -- a clean Gaussian sensor has neither.

### Phase 2 -- DONE (commit 2791bc3, hardened 9bf3b52). GATE DEPTH-STRUCT green; bench 66/66.
Adversarial review (exploits simulated) found 2 CRITICAL self-referential holes: the quadratic COEFFICIENT
check was a tautology (gate + model both computed subpixelErr/(fx*b)); the material-hole test only proved "a
Bernoulli yields ~p" (a boolean impostor passed). Closed: DepthModel now TRIANGULATES Z=fx*b/d so the Z^2 law
EMERGES (validated vs an independent hand-computed literal); a dark-knee MAPPING check (albedo 0.4/0.3/0.2 ->
0.077/0.145/0.214) pins the material map; flying-pixel mean tracks the far surface (interpolation); exponent
band tightened; depthNoiseCoeff reconciled (subpixelErr is the single source of truth).

## §AR-3 PHASE 3 -- STATEFUL IMU (BMI085), design before coding (2026-06-16)
Pure CPU. New: ImuModel.{hpp,cpp} + SensorGate3.cpp. Recon: ImuDataPoint/ImuProfile exist (reuse types); no
prior noise model; time in seconds; gravity Y-down -9.81. The defining feature is STATE carried frame-to-frame.

### Per-axis stateful inertial channel (the noise stack, in causal order)
- WHITE: discrete std = noiseDensity * sqrt(rateHz) (from the continuous density), signal-independent, IID.
- BIAS INSTABILITY (the VIO-killer): 1st-order Gauss-Markov  b[k] = phi*b[k-1] + N(0, biasInstab*sqrt(1-phi^2)),
  phi = exp(-dt/biasTau). Steady-state std = biasInstab. This is what creates the Allan-variance FLOOR.
- RATE RANDOM WALK: biasRW += N(0, randomWalk*sqrt(dt)) -- integrated white -> the +1/2 Allan slope at long tau.
- output = trueVal + white + biasGM + biasRW. State (biasGM, biasRW) persists across samples (that is the point).
- SYSTEMATIC (deterministic): 3-axis scale-factor (diagonal), axis-misalignment (off-diagonal cross-coupling),
  temperature bias (tempBiasCoeff * dTemp). Applied to the true vector before noise.
NOTE: real BMI085 biasTau ~ 200-300 s; the gate uses a COMPRESSED tau so the Allan floor is observable in a
tractable series (the real tau needs hours of data). The SIGNATURE (slope + floor + drift) is what is gated;
the exact floor tau is a parameter. Disclosed, not hidden.

### GATE IMU-ALLAN (Allan signature + integrated drift; per-sample-Gaussian as the FAILING neg-control)
(1) Allan deviation of a long STATIONARY series has the white-noise -1/2 slope at short tau AND a
bias-instability FLOOR -- an INTERIOR minimum after which it rises (random walk), not monotone. (2) Integrated
output DRIFTS: RMS |integral| grows and is >> the white-only model's (the bias is a real non-zero offset; a
VIO dead-reckon diverges). (3) Systematic errors deterministic: a known input recovers scale on the diagonal,
misalignment cross-coupling, temp offset. NEG-CTRL (per-sample Gaussian, NO state): Allan is monotone -1/2
(NO interior floor); integrated drift is small (zero-mean). The discriminators: the Allan floor + the drift,
both of which require carried STATE a memoryless Gaussian cannot have.

### Phase 3 -- DONE (commit 17c8d89, hardened 805ecf4). GATE IMU-ALLAN green; bench 67/67.
Adversarial review (impostors compiled + run) found 2 CRITICAL holes: the floor test verified "interior min +
rise" which RANDOM WALK alone produces (a no-GM model passed); fromProfile (shipped path) had zero coverage.
Closed: the Allan floor is now proven by SUBTRACTION (min(full) > 1.5*min(white+RW-no-GM); the no-GM impostor
NEG-B fails); fromProfile covered (BMI085 unit conversions + physical white std); clusters capped at K>=16;
systematic tested on a general vector (off-diagonal coupling).

## §AR-4 PHASE 4 -- LAYER 2 reconstruction-uncertainty field, design before coding (2026-06-16)
Pure CPU. New: Layer2Gpis.{hpp,cpp} (ReconField fusion) + SensorGate4.cpp. The shared MaterialField now drives
BOTH the Phase-2 holes AND per-observation match noise (DepthModel.matchNoisePenalty: effSubpix *= 1+penalty*
drive) -- specular/dark give noisier disparity, the physical basis for high recon uncertainty there. drive==0
(bright) is unchanged, so the Phase-2 quadratic gate stays green.

### L2 = L1 truth observed through the Phase-2 sensor, fused (mu, sigma^2)
L1: a known true depth map Z_true(u,v) (slanted plane) with a per-pixel MaterialSample map (bright Lambertian,
a specular patch, a dark patch) and an OCCLUDED strip (seen by 0 frames -> a true recon hole). Observe K frames:
each pixel -> DepthModel.sample (quadratic noise scaled by material, material holes). Fuse per cell with Welford
-> mu (mean depth), n (valid obs), sigma = sqrt(obsVar/n) (standard error of the estimate); n==0 -> recon hole,
sigma = sigma_prior (max). GPIS-style: a (mu, sigma^2) belief field, high sigma where poorly observed. NOTE:
this is per-cell Bayesian depth fusion (mean + calibrated uncertainty), not a full kernel GP -- the gated
BEHAVIOR (sigma contrast, hole co-location, calibration) is the GPIS essence; kernel smoothing is a later refinement.

### GATE L2-UNCERTAINTY (measured; uniform-sigma as the FAILING neg-control)
(1) CONTRAST: mean sigma over poorly-observed cells (specular/dark/occluded) >> well-observed (bright); the
shared material drives it. (2) CO-LOCATION: recon holes (n==0) lie in the occluded/high-drive region; sigma
correlates with materialDrive (positive, significant). (3) CALIBRATION (round-trip): for observed cells,
|mu - Z_true| < 2*sigma for >=90% (the uncertainty is honest, not arbitrary). NEG-CTRL (uniform sigma = median
everywhere): CONTRAST ratio ~1 (FAILS) AND calibration fails (over-confident in noisy regions, under-confident
in clean ones). The discriminator: a real recon KNOWS WHERE it is uncertain; a uniform field does not.

### Phase 4 -- DONE (commit cb481d6, hardened 40de483). GATE L2-UNCERTAINTY green; bench 68/68.
Adversarial review (impostors compiled + run) found 2 CRITICAL holes + the directive's CORE untested: the
|mu-Z|<2sigma calibration was tautological (standard error covers its own mean ~95% for ANY unbiased model);
a per-region-CONSTANT sigma passed; the SHARED material field was never structurally tested. Closed: reduced
chi-square (two-sided, 1.12); 1/sqrt(n) Bayesian test (NEG-B per-region-const fails at slope 0); standalone
mu-accuracy; and corr(L3 dropout, L2 sigma) r2=0.986 with an ignored-channel control -- the shared cause
proven directly. The shared MaterialField now drives BOTH holes AND match noise in DepthModel.

## §AR-5 PHASE 5 -- COMPOSITION, design before coding (2026-06-16)
Pure CPU. New: Composition.{hpp,cpp} + SensorGate5.cpp. Wires the three layers: physics reads L1 TRUE; the
robot's BELIEF reads L2 (mu,sigma); the CAMERA's live image is the belief surface observed through L3 (fresh
per-frame noise + material dropout). Composable/toggleable (L1-only / L1+L3 / all three), per-seed deterministic
with INDEPENDENT per-layer RNG substreams (toggling one layer does not shift another's stream).

### The composition seam
- physicsDepth(i) = L1 trueDepth (what physics acts on).
- beliefMu/Sigma(i) = L2 field if useL2 else L1 truth (the robot's internal estimate).
- cameraRead(i, rngL3) = (useL2 ? L2.mu : L1.true) observed through DepthModel.sample if useL3 else clean.
  So the camera sees the BELIEF surface with LIVE noise; the SHARED material drives both L2 sigma (built into
  the belief) AND L3 dropout/noise (in sample()). Determinism: seedL2, seedL3 drawn in fixed order from the
  master seed (toggle-stable).

### GATE COMPOSE (measured; independent-draw as the FAILING neg-control)
(1) TRUE-vs-BELIEF differ CORRECTLY: physics(L1) != camera(L2+L3) (live divergence > 0) yet the belief mu is
within k*sigma of L1 (honest, calibrated). (2) SPECULAR correlation: across the composed scene corr(L2 sigma,
L3 dropout rate) > 0.9 -- specular cells are BOTH uncertain in belief AND dropping live. (3) TOGGLES: L1-only ->
camera == L1 clean; L1+L3 -> camera base == L1 (no belief divergence) + live noise; all -> camera base == L2 mu.
(4) DETERMINISM: same seed -> identical stream; seed+1 -> different draws, same statistics. NEG-CTRL
(independent-draw): L3 dropout from an INDEPENDENT fixed-rate field (not the material) -> corr(L2 sigma, L3
dropout) ~ 0 -> FAILS. The shared cause -- not coincident independent draws -- is what makes them correlate.

### Phase 5 -- DONE (commit 440cc6c, hardened 5686848). GATE COMPOSE green; bench 69/69.
Adversarial review (honesty CLEAN -- no sim-to-real overclaim) found the "shared cause" test was just
co-elevation in 3 rectangles (different-channel impostors passed). Closed: a 2D material sweep (specular x
albedo INDEPENDENT) -> corr(L2 sigma, L3 dropout) r2=0.982 only because both read the same materialDrive; the
specular-only-channel NEG scatters to 0.622. Plus a camera-reads-belief test (mean live read tracks the belief
mu 0.00032 << truth 0.00134) and an occlusion strip exercising the belief-hole path.

## §AR-6 PHASE 6 (FINAL) -- E2E + HONEST TRANSFER HARNESS, design before coding (2026-06-16)
Pure CPU. New: TransferHarness.{hpp,cpp} + SensorGate6.cpp (two gates). THE HONEST LIMIT: the transfer gate is
gated ONLY vs a second SYNTHETIC instance and labeled "self-consistent, NOT validated against real hardware --
awaiting operator capture." It NEVER claims sim-to-real transfer.

### GATE E2E (one scene/profile/seed -> RGB+depth+IMU, each passing its gate IN-CONTEXT)
Split the master seed into RGB/depth/IMU substreams (fixed order, toggle-stable). (1) RGB stream: variance-vs-
signal slope ~ 1/photonsPerDN (the Phase-1 shot signature, re-checked on the E2E stream). (2) Depth stream:
sigma-vs-Z power-fit exp ~ 2 (the Phase-2 stereo signature). (3) IMU stream: white-noise short-tau slope ~ -0.5
+ stateful integrated drift >> white-only (the Phase-3 signature; the bias-instability FLOOR specifically stays
a Phase-3 compressed-tau gate). (4) CONSERVATION: the noise is UNBIASED, so the ensemble mean preserves the
true signal/depth/rate (energy/signal conservation) -- residual ~ 0; NEG (a biased model with a DC pedestal)
fails. (5) DETERMINISM: same seed -> byte-identical streams.

### GATE REAL-TRANSFER (the transfer-validation harness -- SELF-CONSISTENCY ONLY, by honest necessity)
A statistical FINGERPRINT (RGB slope/floor, depth Z^2 exp + hole rate, IMU white std + drift, an RGB histogram)
computed from a profile+seed. (1) SELF-CONSISTENT: instance A (profile P, seed s1) vs instance B (profile P,
seed s2) MATCH within tolerance -- two independent synthetic draws of the same sensor are statistically
indistinguishable. (2) DISCRIMINATING POWER (the NEG that proves the comparison is not vacuous): instance C
(profile P' with perturbed photonsPerDN + holeRate, seed s3) MISMATCHES A -- a genuinely different sensor is
detected. PASS = (A~B match) AND (A!~C mismatch). The harness PRINTS, every run, the honest label: it is gated
against a second SYNTHETIC instance, NOT real hardware; real-transfer remains UNVALIDATED awaiting an operator
D456 capture. This is the proven/proxy + internal/real-transfer boundary, made explicit and un-fakeable.

## §GR GRASP PLANNER vs REAL OBJECT LIBRARY -- architecture (2026-06-16, recon before coding)
A self-contained, headlessly-gateable module: src/Grasp/*.cpp + include/GraspHeaders/*.hpp (CMake glob +
include dir, mirroring Sensors). RIGID-BODY parallel-jaw grasping (PhysX 5 contact+friction), NOT MPM. Deliverable
= an HONESTLY-MEASURED success RATE that improved through PLANNER tuning + a failure catalog. The metric is
LOCKED; only the planner is tuned. Recon (8-agent workflow) confirmed GO: PhysX headless works (ArticulationGate
pattern: PxGetPhysics() singleton + throwaway PxScene), V-HACD installed (KR_WITH_VHACD), YCB google_16k live on
S3 (HTTP 200), no grasp code exists (build fresh; reuse MeshUtils/Assimp, CollisionCookingService, RayPick).

### THE LOCKED PHYSICS / SUCCESS CRITERION (constexpr GraspPhysicsConfig; configHash printed; runtime guard) -- UNCHANGED across the sprint
| param | locked value | physical justification + anti-cheat |
| g | 9.81 m/s^2 down | matches the engine's validated bench suite (SimulationController.cpp:604); guard asserts live scene gravity == this |
| mu_s = mu_d | 0.70 (jaw<->object) | rubber gripper pad on rigid plastic dry friction ~0.6-0.9; 0.70 is conservative low-mid -- holds true force-closure, slips marginal/cheating grasps. NOT >1.0 (would mask bad geometry). |
| F_grip | 40 N constant per jaw after contact | a 0.3 kg object needs >= m*g/mu ~ 4.2 N; 40 N = ~10x margin, within real parallel-jaw range (20-140 N); FINITE + bounded so a bad-normal grasp still squirts out |
| density | 1000 kg/m^3 (engine default) x DECOMPOSED collider volume | hollow objects (mug) get ~shell volume -> physical light mass; mass asserted finite + in a physical band; NOT reducible to ease lifting |
| h_lift | 0.15 m at <= 0.25 m/s | standard pick clearance; >> object size so a grasp that slips under lift acceleration is exposed |
| t_hold | 2.0 s (480 steps @ 240 Hz) | slow creep/slip + solver relaxation surface over 2 s that a 1-frame snapshot misses |
| success | object center within d_max=30 mm of (start + 0.15 m up) AND no ground contact after liftoff AND continuous jaw contact >= 90% of lift+hold steps | 30 mm << 150 mm lift, so a dropped/slipped object fails unambiguously; triple condition closes the thrown-object + one-frame loopholes |
| solver/dt | TGS, dt=1/240, CCD+stabilization, seeded mt19937(seedBase^objIdx) | engine standard; deterministic -> a re-run is bit-identical |
The grasp scene is built ONLY from kLockedPhysics with NO env overrides (unlike the diagnostic KRS_PGS/KRS_NO_CCD
hooks). assertPhysicsLocked(scene, jawMat, appliedForce) re-reads the LIVE gravity/friction/force and returns
false (gate FAIL) on any mismatch. SOFTENING ANY of these is a HARD FAILURE, not a path to green.

### Gripper model (honest by construction)
Two PxRigidDynamic jaw boxes (NOT kinematic -> no interpenetration-clamp cheat; NOT the FANUC), each D6-locked
to a "palm" frame on all axes except the closing axis, driven inward by a finite F_grip. The palm is moved up
for the lift; the jaws follow, and the object is held ONLY by the friction cone -- a bad grasp slips out. Contact
tracked via the BenchContactLogger/PxSimulationEventCallback pattern.

### Phase plan -> gates (measured number + the FAILING negative control; the neg-ctrls are the anti-cheat)
- GATE IMPORT: N YCB objects load, real-meter scale vs published dims (+-15%), finite mass/inertia, 0 NaN.
  NEG: a x1000 (mm-as-meters) mesh -> a 100 m mug -> scale check FAILS (the check has teeth).
- GATE COACD: a point inside a known cavity (bowl/mug/cup) is OUTSIDE the collider -> a ball RESTS INSIDE the
  bowl. NEG: the cavity-FILLING collider (single convex hull; and low-res V-HACD if it fills) -> ball ROLLS OFF.
  CoACD attempted (vendoring, MIT, Eigen-only, infra ready); if infeasible, high-res V-HACD is the concavity-
  preserving good case + convex hull the definitive filler -- determined EMPIRICALLY, reported honestly.
- GATE SUCCESS-CRITERION (anti-cheat heart): a known-GOOD grasp PASSES + a known-BAD grasp FAILS under the SAME
  locked physics. NEG: a no-gravity / sticky / loosened world would FALSELY pass the bad grasp -- assert the
  LOCKED world (guard re-confirms g/mu/F/d_max unchanged, prints configHash) does NOT. This makes every later
  number trustworthy.
- GATE PLANNER: success RATE over the library under locked physics; must IMPROVE baseline -> tuned (report the
  trajectory). NEG: a random-jaw-pose planner scores LOW under the same criterion (heuristic adds real signal).
  NOT 100% unless every success survives the unchanged test AND adversarial review confirms no softening.
- GATE FAILURE-CATALOG: 100% of failing objects classified (no "unknown") into {slip-out, never-closed,
  crush/ejection, tipped-then-dropped, ground-collision, thin, slippery, awkward-CoM, unreachable-concavity,
  scale-vs-jaw}; report taxonomy + counts. NEG: inject a known synthetic failure of each class -> correct label.

### Honesty boundary
Everything here is REAL rigid-body PhysX under LOCKED physics -- it is PROVEN grasp success under a stated,
physically-justified criterion, NOT a claim about a specific real robot/gripper. The friction/force/density are
physically-reasonable STATED values (a representative rubber-jaw small parallel gripper), not calibrated to one
hardware unit. The rate is honest FOR THIS LOCKED CRITERION; a different gripper/criterion would give a different
number. Target = a HIGH honest rate with hard failures characterized, NOT a suspicious 100%.

### Phase 0 -- DONE (commits 8fbbe06, hardened 054e503). GATE IMPORT green; bench 72/72.
20 YCB objects, real-meter scale (per-object anchors), watertight (0.00% boundary on all 20; holed-mesh
neg-ctrl rejected), finite mass. Adversarial review found + closed: no watertight check (CRITICAL), vacuous
scale band (17/20), loose mass. locked-cfg e4615ee25c65d98f.

### Phase 1 -- DONE (commit pending). GATE COACD green; bench 73/73.
DECISION: CoACD vendoring DEFERRED, NOT a blocker. The submodule doesn't exist, the CMake hook is mis-wired
(project CoACD vs targets coacd/_coacd), and CoACD's FetchContent of OpenVDB 8.2 + Boost + its own TBB would
risk bricking the currently-green build (a hard-rule violation). The in-house CollisionCookingService already
exposes the three colliders the gate needs. GATE COACD proves the directive's INTENT (grasp-faithful cavity-
preserving collider vs cavity-filling collider) via a drop test, independent of which algorithm decomposes.
EMPIRICAL FINDING (drop a 1cm ball into 024_bowl, opening-up, settle 2s, measure rest-Y vs rim 0.055m):
  exact trimesh  -> ball -0.0436 m (caught INSIDE, near floor)
  V-HACD decomp  -> ball -0.0421 m (caught INSIDE -- V-HACD 4.1 PRESERVES the cavity, it does NOT fill it;
                    the directive's "V-HACD fills concavities" premise is OUTDATED for this version/resolution)
  convex hull    -> ball +0.0094 m (ball ON TOP -> cavity FILLED) -- so the SINGLE CONVEX HULL is the
                    cavity-filling FAILING neg-control, not V-HACD. Separation 0.0515 m.
CoACD remains a one-function swap behind the decomposition backend (it must clear this identical drop test).

### Phase 2 -- DONE (commits 0b1166a, 0484e29, 7d817e2). GATE SUCCESS-CRITERION green.
The anti-cheat heart. 036_wood_block, three runs under the gripper + locked physics:
  GOOD grasp (centre, identity approach, span 0.13) -> SUCCESS (centerErr 0.0008 m, peak jaw force 38.7 N).
  BAD grasp (grip near the block's end, off-centre 0.08 m) -> FAIL: it grips, then the overhang torque slips it
    out under mu=0.70 and it re-touches the ground.
  BAD grasp in a SOFTENED world (sticky mu=5.0) -> the three success clauses FALSELY read PASS (the softening
    rescues the slip), BUT the live-physics guard (assertPhysicsLocked re-reads gravity/friction/force) trips on
    mu != 0.70 -> verdict FAIL. This proves the criterion is non-trivially passable AND softening is un-fakeable.
FORCE-BOUNDEDNESS (the kinematic-clamp anti-cheat, added in Phase 3 review): the Tracker reads
PxContactPairPoint.impulse on jaw<->object pairs (impulse/dt = normal force) across LIFT+HOLD; the good grasp's
peak is 38.7 N ~ gripForceN -- proof the grip is a bounded friction grip, NOT an unbounded clamp.

### Phase 3 -- DONE (commit 7d817e2). GATE PLANNER green. Two LOCKED-criterion corrections + a tuned planner.
ARCHITECTURAL DECISION (user-approved): the original ONE-SIDED kinematic-vise gripper was replaced by a
SYMMETRIC two-finger gripper. WHY: the vise's single moving finger SHOVED a grounded object sideways, so
success tracked tip-resistance-under-one-sided-shove, not grasp QUALITY -- tuning could not honestly raise the
rate (stuck ~45% regardless), and a wedged object made the KINEMATIC anvil impose an UNBOUNDED reaction
(1000-17000 N -> a phantom rigid clamp that carried anything). The symmetric gripper: a kinematic palm (no
collision shape) carries TWO force-limited fingers (addForce gripForceN each) that close toward the grasp
centre; a pure-damper (stiffness 0) on the closing axis kills the free assembly's X-drift WITHOUT adding any
steady-state holding force. Both jaws bounded => the phantom clamp is STRUCTURALLY impossible and the balanced
close does not tip the object. CRITERION TIGHTENED (not loosened): a success now also requires the peak jaw
force <= maxGripForceFactor*gripForceN (3x = 120 N) -- this ANDs in, only ever removes a success, and rejects
any transient-spike grasp. locked-cfg e4615ee25c65d98f -> 868489ff8e07da5f (logged; the change is a tightening).
SUCCESS gate RE-VALIDATED against the new gripper (good/bad/softened/force-bound all green).
PLANNER (the only tuned thing): antipodal -- sample surface points, ray-cast the opposing face
(krs::pick::rayTriangle), keep near-opposed normals, score by normal alignment + CoM proximity + a VERTICALITY
penalty (a tabletop parallel-jaw gripper cannot slide its lower jaw UNDER a grounded object, so a top/bottom
grasp never seats -- it must grip from the sides). The verticality term was the genuine refinement that made
quality drive success. GATE PLANNER (20 objects x 3 grasps, fixed denominator; a planner that finds < 3 grasps
fails the remainder), success-rate TRAJECTORY under the LOCKED criterion:
  random   = 10.0% (6/60)   -- uniform-random grasp neg-control
  baseline = 23.3% (14/60)  -- loose tolerance, table-blind
  tuned    = 53.3% (32/60)  -- tight antipodal + CoM- and side-aware
PASS = random<=30% AND beaten by >=10% AND (tuned-baseline)>=10% AND tuned<100% (a real library is NOT fully
solvable). The improvement is grasp QUALITY, not a softened test. Adversarial review (a595b181): "no blocking
issues -- the improvement is honest" (hash independently recomputed; damper adds zero holding force; c4 is a
genuine tightening; verticality is a real reachability constraint; one identical locked world across variants).

### Phase 4 -- DONE (commit pending). GATE FAILURE-CATALOG green. Bench 76/76.
Classify 100% of the tuned planner's 28 failures (of 60 attempts) into an exhaustive taxonomy; an INCOMPLETE-
taxonomy neg-control proves the coverage metric has teeth (67.9% vs 100%). Distribution:
  GRIP_NOT_SEATED      11  jaws never both closed (tall/awkward objects; best face needs an unreachable axis)
  NO_ANTIPODAL_GRASP    6  no antipodal pair found -- 024_bowl, 065_cups (thin shells below the min jaw span)
  CONTACT_INTERMITTENT  4  gripped but jaw contact dropped below 90% -- 010_potted_meat_can
  DRIFT_ROTATE          4  held to the end but the centre missed target -- 036_wood_block off-centre grasps
  UNBOUNDED_GRIP        2  a transient spike exceeded the 120 N bound; the force-bound clause REJECTED it
                          (anti-cheat working -- never a success, cannot inflate the rate)
  SLIP_FELL             1  gripped + lifted, then slipped out and re-touched the ground
These are REAL grasp failures (unreachable concavities, thin shells, awkward CoM, slippery cans), not artifacts.

### Honesty boundary (final)
PROVEN: a tuned antipodal heuristic raises the success rate 23.3% -> 53.3% over 20 real YCB objects, under a
LOCKED, physically-justified, force-bounded criterion (configHash 868489ff8e07da5f asserted unchanged every
run; softened worlds structurally un-passable; grip bounded by construction). The 53.3% is honest FOR THIS
gripper + criterion -- NOT a claim about a specific real robot. PROXY/LIMIT: a representative rubber-jaw
parallel gripper with STATED (not hardware-calibrated) friction/force/density; the convex-decomposition collider
and the tabletop seating are sim choices. The number would differ for another gripper. Target met: a high HONEST
rate (53.3%, not a suspicious 100%) with every failure characterized by physical root cause.

## §GR-CoACD COLLIDER SWITCH + RE-MEASURE (2026-06-16, recon before coding)
The prior sprint shipped V-HACD colliders despite the CoACD requirement. This swaps the OBJECT COLLIDER to CoACD
and RE-MEASURES the rate under the UNCHANGED locked criterion (hash 868489ff8e07da5f) -- the collider is the
ONLY variable. RECON: CoACD (github.com/SarahWeiii/CoACD) is **MIT** (OSI-approved, verified via package
metadata) -> ships freely. Feasibility: `pip install coacd` provides a prebuilt Windows wheel (coacd 1.0.11) --
it RUNS here (mug -> 78 convex parts). DECISION: rather than vendor CoACD's C++ build deps (the prior sprint
deferred it for OpenVDB/TBB build-brick risk), generate colliders OFFLINE via the Python wheel and ship only the
OUTPUT convex parts (derivatives of the YCB meshes, MIT-permitted) -- the C++ build is untouched.
- scripts/gen_coacd.py: CoACD (threshold 0.04, max_hull 64, seed 0 -> deterministic) on all 20 YCB meshes ->
  assets/ycb/<id>/coacd.bin (magic 'COAC' | u32 numParts | per part u32 nVerts + float32[3]*nVerts; each part's
  convex-hull vertices). Box-like objects ~5 parts; concave (mug) ~78. coacd.bin is a derived asset (gitignored
  with the meshes; regenerable from the committed script).
- CoacdCollider.hpp: loadCoacdParts() reads the .bin into per-part glm::vec3 clouds.
- GraspSim::runGripperSim gains a trailing `coacdPath` arg: if it loads, the collider is the CoACD parts cooked
  via requestConvexHull (same 64-vert cap as the V-HACD path); else V-HACD fallback. The planner is geometry-only
  (ray-casts the VISUAL mesh, never the collider), so the SAME grasps are scored under either collider.
- All production gates (SUCCESS / PLANNER / FAILURE-CATALOG) switched to CoACD (the new default). Locked physics
  UNCHANGED -> assertPhysicsLocked still passes with hash 868489ff8e07da5f (collider is not in the config struct).

GATE COACD-REAL (the discriminating test the prior bowl-only drop missed): a "cavity point" = a grid point that
is EMPTY (outside the watertight mesh by ray-parity) yet INSIDE the V-HACD collider (PxGeometryQuery::pointDistance
== 0) -- i.e. a point V-HACD WRONGLY fills. V-HACD preserves 0% of these BY DEFINITION (the failing neg-control);
CoACD must leave most empty. EARLY RESULT (3 of 4 concave objects; cups generating):
  019_pitcher_base: 404 cavity pts, V-HACD preserve 0%, CoACD preserve 82.4%  (handle gap)  -> discriminates
  024_bowl        : 1158 cavity pts, V-HACD preserve 0%, CoACD preserve 73.4% (interior)
  025_mug         : 1498 cavity pts, V-HACD preserve 0%, CoACD preserve 70.4% (handle+interior) -> discriminates
CONFIRMS half the hypothesis: V-HACD WAS filling the mug handle/interior, pitcher handle, cup interior (404-1498
cavity pts each, V-HACD preserve 0%, CoACD preserve 70-83%, CoACD solid-coverage 91-99.9% so the colliders are
faithful, not gappy -- the bowl's 83% solid-coverage is thin-shell grid noise). GATE COACD-REAL PASSES (guard
LOCKED, 3 grasp-relevant discriminators). The discriminating handle/interior test the prior bowl-only drop missed.

GATE REMEASURE (apples-to-apples, same tuned grasps, same hash 868489ff8e07da5f asserted in every run): V-HACD
(before) 53.3% (32/60) -> CoACD (after) 48.3% (29/60), **delta -5.0%**; random under CoACD stays low at 8.3%.
3 objects improved (019_pitcher_base 0->1: handle now reachable; 010_potted_meat, 048_hammer), 5 regressed
(003_cracker 3->1, 007_tuna 3->2, 011_banana 3->2, 025_mug 1->0, 035_drill 2->1). HONEST FINDING -- the
motivating hypothesis is REFUTED: V-HACD's concavity-filling was NOT artificially depressing the rate. The
faithful CoACD colliders (preserved hollow interiors, thin walls, open handles) make grasping slightly HARDER
and MORE honest -- V-HACD's coarser, slightly-inflated, cavity-filling hulls gave more forgiving solid contact.
The 48.3% is the more physically-honest number; the collider was NOT the bottleneck.

GATE FAILURE-CATALOG re-run under CoACD (31 failures vs V-HACD's 28): NO_ANTIPODAL_GRASP UNCHANGED at 6 (bowl,
cups) -- proving those are PLANNER-side (the planner ray-casts the VISUAL mesh, never the collider), NOT collider
artifacts, so CoACD cannot fix them. GRIP_NOT_SEATED 11->13 and DRIFT_ROTATE 4->7 (the faithful collider grips
less forgivingly -> more held-but-drifted and unseated failures); CONTACT_INTERMITTENT 4->2; UNBOUNDED_GRIP 2,
SLIP_FELL 1 unchanged. 100% classified (incomplete-taxonomy neg-ctrl 64.5%). The remaining failures are
genuinely hard (thin shells defeat the antipodal planner; hollow/concave objects grip poorly), not artifacts.

VERDICT: CoACD is now the production collider (all gates default to it; hash unchanged 868489ff8e07da5f). The
honest re-measured rate is 48.3% (was 53.3% on V-HACD). Bench 78/78. The collider swap made the pipeline more
physically faithful (CoACD preserves the concavities a jaw must enter, confirmed by GATE COACD-REAL) and the
rate slightly lower -- an honest re-measurement, not a number inflated by the change.

## §GR-V2 HEURISTIC COMPLEXITY + LARGE-DATASET GENERALIZATION (2026-06-17)
Two variables measured SEPARATELY under the UNCHANGED locked criterion (hash 868489ff8e07da5f): (1) an improved
heuristic on the KNOWN YCB set; (2) that FIXED heuristic over a large filtered library. The collider is CoACD
throughout (the prior sprint's production default).

### Phase 1 -- DONE (commit a60d6c5). GATE HEURISTIC-V2 green. YCB rate 48.3% -> 51.7% (+3.3%).
Apples-to-apples V1(tuned) vs V2 on the SAME 20 YCB x3=60 attempts (planner is geometry-only -> only the planner
params differ). KEPT: an ABOVE-CoM stability term -- penalise grasps BELOW the resting CoM (a lifted object
gripped above its CoM hangs as a stable pendulum; below it is top-heavy and tips/drifts). Flips 010_potted_meat
and 035_power_drill 1/3->2/3 with ZERO regressions; GRIP_NOT_SEATED 14->12. TESTED AND DROPPED as dead
complexity (reported honestly, not kept): rim/thin-wall pinch grasps (GENERATE proposals for bowls/cups,
NO_ANTIPODAL 6->1, but never SEAT -- a parallel-jaw pinch on a wide shallow rim tips the object); snug-fit
widthWeight (made GRIP_NOT_SEATED worse); stronger CoM-line term (DRIFT +0); tighter tolerance (fixed DRIFT 7->3
but broke seating, net +0). The thin shells remain hard -- an honest strategy limit, not a tuning failure.

### Phase 2 -- DONE (commit 338f5d7). GATE FILTER green. 500 raw GSO -> 299 valid (40.2% rejected).
Dataset: Google Scanned Objects, Creative Commons Attribution 4.0 International (CC-BY 4.0), real METRIC scale,
downloaded from Gazebo Fuel (scripts/download_gso.py; 500-object subset). The C++ GATE FILTER classifies every
raw object BEFORE grasping: VALID, or rejected (load-fail / degenerate / non-watertight>10% / out-of-scale
[0.02-0.40 m] / non-graspable [shortest axis > 0.12 m, too fat for the jaw]). Result 500 -> 299 valid; rejected
96 non-graspable, 84 non-watertight, 21 out-of-scale. The rejected fraction is a DATASET-QUALITY statistic, NOT
grasp failures. NEG-CTRLS (a valid mesh x1000 / x0.001 / holed) all REJECTED -> the filter discriminates and is
not trivially passing. CoACD colliders generated for the 299 survivors (scripts/gen_coacd_gso.py, threaded;
needs Pillow for GSO textured OBJs). GSO assets + colliders gitignored (derived, regenerable from the scripts).

### Phase 3+4 -- DONE. GATE GENERALIZE+TAXONOMY-SCALE green. (numbers below are the DE-CONTAMINATED re-measurement)
FIXED Heuristic-V2 (not re-tuned) over the FILTER-valid GSO objects, CoACD + locked criterion (hash
868489ff8e07da5f asserted every run). GENERALIZED RATE = 33.2% (250/753 over 251 valid objects); random
neg-control 4.8% (the heuristic adds real signal). The rate drops from YCB's 51.7% -> 33.2% at scale: a diverse
uncurated library is genuinely harder. PER-OBJECT: 3/3 16.3%, 2/3 15.1%, 1/3 20.3%, 0/3 48.2%. HARDNESS =
SPREAD: the 0/3 population is LARGE (48.2%), a real ceiling, NOT a small pathological tail (only 16% fully solved).
TAXONOMY AT SCALE (503 failures, 100% classified; incomplete-taxonomy neg-ctrl 53.3%): GRIP_NOT_SEATED 38.8%
(dominant, MATCHES YCB), UNBOUNDED_GRIP 27.2% (a NEW mode that emerges at scale -- irregular objects wedge the
symmetric force-driven gripper into an over-squeeze the force-bound criterion rejects), NO_ANTIPODAL_GRASP 14.5%,
DRIFT_ROTATE 9.9%, SLIP_FELL 6.2%, CONTACT_INTERMITTENT 3.4%.

ADVERSARIAL REVIEW (2-lens workflow) found + drove fixes: (R2/R3) the FILTER was too lenient on watertightness
(boundaryFrac<=10%, non-manifold UNCHECKED). FIXED: added the non-manifold check; set a data-justified 8%
boundary bar (the strict IMPORT 1% rejects 100% of real GSO scans -- measured boundaryFrac is <1%=0, 1-2%=29,
2-4%=82, 4-10%=261, >10%=128). 500 raw -> 251 valid (49.8% rejected), down from a too-lenient 299.
(R1) the review hypothesised UNBOUNDED_GRIP (27.5%) was a COARSE-COLLIDER ARTIFACT (blocky hulls spiking the
contact solver) and predicted a finer re-run would lift the rate to ~40-50%. I TESTED it: regenerated all
colliders at fidelity (no decimation, max_hull 16->40, median 27 parts vs <=16). RESULT: UNBOUNDED_GRIP barely
moved (27.5% -> 27.2%) and the rate rose only +1.7% (31.5% -> 33.2%). The artifact hypothesis is EMPIRICALLY
REFUTED: UNBOUNDED is a REAL over-squeeze failure mode at scale, robust to collider fidelity (and to the stricter
filter, which selects cleaner objects -- both changes would have reduced an artifact but did not). 33.2% is the
honest generalized rate, NOT artifactually low. (Caveat unchanged: this is one rigid-body gripper + criterion, a
representative proxy, not a specific hardware unit; a compliant force-controlled gripper would back off instead
of spiking, so UNBOUNDED partly reflects this sim's rigid force-driven jaws.)

### Phase 5 -- FORCE-LIMITED GRIPPER (fix the UNBOUNDED_GRIP fidelity red; recon before coding, 2026-06-17)
MECHANISM (what the harness proved): UNBOUNDED_GRIP (27.2% of GSO failures) is a KINEMATIC-LIFT ARTIFACT, not
real over-squeeze. GraspSim's lift was a KINEMATIC palm (eKINEMATIC + setKinematicTarget): it imposes position
regardless of reaction force, so a wedged object (mug, drill) transmits UNLIMITED force up the Y/Z-locked
finger joints. The fidelity UNBOUNDED-DIAGNOSIS measured it: the contact solver + friction are FAITHFUL
(sustained firm grip 40.6 N vs the 40 N actuator, 1.5%; friction 0% error), but the wedged cases sustain a
MEDIAN 116-173 N (mug 173/4.3x, drill 116/2.9x) -- physically impossible for a 40 N finger; the force is
manufactured by the force-unlimited kinematic lift. (GraspPhysicsConfig.hpp:27-29 already named this: "the
lift jaw is KINEMATIC and can otherwise impose UNBOUNDED reaction".)

REMEDY (the KEY INSIGHT, from the architectural decision 2026-06-17): the INVARIANT is the ACTUATOR force, NOT
the contact force. A rigid palm holds exactly 40 N AND manufactures the wedge force because those are the SAME
property (rigidity) -- no lever separates them. Force-limiting the LIFT alone fails: a kinematic palm can't do
stable force control (advancing into a stiff wedge spikes; tried, got 5 kN oscillations), and the wedge
geometrically AMPLIFIES any bounded lift (~2.3x) and reacts it through the rigid FINGER Y/Z locks regardless of
the lift. The fix is at the FINGERS: FORCE-LIMIT each finger's Y/Z joint to the palm. The fingers are FREE in
Y/Z, held to the palm by a controller whose restoring force is HARD-CLAMPED (in our code, a guaranteed cap --
PhysX's PxD6 drive forceLimit did NOT reliably cap) to +/- the rating. A wedge that pushes a jaw off-track by
more than the rating SLIPS the jaw (the actuator YIELDS) -- relieving the amplified contact AT ITS SOURCE (the
jaw gives instead of transmitting a manufactured force). The palm stays KINEMATIC (rigid grip frame), so a GOOD
grasp (jaw load = friction <= rating) is held firmly and the firm contact stays ~40 N. Contact on a compliant
jaw is correctly MODULATED DOWN to respect the limit -- that is faithful, not a softened grip.

LOCKED CONSTANT (asserted, OUTSIDE the criterion hash): kLiftActuatorForceRatingN = 56 N = 2*mu*gripForceN =
2*0.70*40 -- EXACTLY the vertical load the friction grip can hold (a real physical limit: a jaw pushed harder
than this just slips the object), so the rating is principled, not a magic number tuned to these two objects.
It is NOT a field of GraspPhysicsConfig (the struct is a static_assert'd 11-float packed image whose FNV-1a
hash MUST stay 868489ff8e07da5f) -- a separate constexpr in the same namespace. This changes gripper PHYSICS,
NOT the criterion: the hash, the 4 graspSucceeded clauses, and assertPhysicsLocked (gravity/friction/gripForceN)
are all UNCHANGED.

NEG-CONTROL: WorldOverride.legacyRigidGripper (default false = compliant jaws). GATE GRIP-COMPLIANT runs the
SAME wedge experiment both ways. RESULT: compliant jaws SLIP the wedges -- mug sustained 173->31 N, drill
116->43 N (the 4.3x/2.9x manufactured spikes GONE, contact <= rating); GOOD grasps still HOLD (graspSucceeded
29->33, NOT dropped -- artifact failures now grasp cleanly); firm-contact grip stays 42.5 N; the 40 N actuator
asserted every run. OLD rigid jaws reproduce the >120 N spike and FAIL. The compliant gripper is now the
DEFAULT, so the YCB HEURISTIC-V2 rate re-baselined (V1 48.3->55.0%, V2 51.7->56.7%; V2's marginal gain shrinks
to +1.7% as compliant jaws and the above-CoM term overlap -- gate now asserts V2 strictly beats V1). GRIP-
REMEASURE re-runs the 251-object GSO generalized rate (same V2 heuristic, same hash) with the compliant gripper
and reports the true rate vs 33.2% + UNBOUNDED before/after (honest in either direction).

### GRIP-REMEASURE RESULT (2026-06-17, compliant gripper, same V2 heuristic + locked hash 868489ff8e07da5f + 251 GSO)
GENERALIZED RATE = 24.0% (181/753), DOWN from the rigid gripper's 33.2%. The honest corrected number: the
rigid rate was INFLATED by grasps that depended on the rigid hold -- with compliant jaws those objects
correctly DRIFT out of pose (DRIFT_ROTATE 9.9% -> 23.1% of failures, the dominant rise). UNBOUNDED_GRIP
dropped 27.2% -> 21.3%: the SUSTAINED over-squeeze is gone (wedge median 173 -> 31 N on YCB), but the locked
criterion's force-bound clause reads the PEAK (maxJawForceN), and transient one-substep contact impulses
during the jaw-SLIP still spike >120 N, so peak-classified UNBOUNDED only partially falls. random neg-control
stays low (4.4%). The generalize gate PASSES (structural: random << tuned, 100% taxonomy coverage). This is
the directive's anticipated "rate may go DOWN -- the honest corrected number, not an improvement to chase."
A FUTURE refinement (not done here): score the grip on the SUSTAINED (median) jaw force, not the per-substep
peak, so a clean compliant slip is not flagged UNBOUNDED by a transient impulse -- but that touches the locked
criterion (hash), so it is deferred as a deliberate, separate decision.

### Phase 6 -- MPM GRANULAR (fix the angle-of-repose red; 2026-06-17). TWO bugs, split + addressed:
BUG A (SETTLING) -- FIXED + bench-safe. The explicit MLS-MPM sand column SKATED (medSpeed ~15 m/s, never
settled) because the AFFINE modes inject energy faster than the weak velocity-Coulomb floor (friction ~
normal VELOCITY, ~0 at rest) removes it. Added (SAND-only, default OFF so the bench is unchanged; the repose
gate turns them on): a per-substep APIC->PIC affine blend (u_picBlend), a dt-SCALED velocity bleed
(u_velDampRate [1/s]), and a sticky basal floor (u_floorStick, bleeds the floor-cell tangential velocity so
the deposit cannot coherently skate). Together: medSpeed 15 -> ~1.2 m/s.

BUG B (FRICTION / phi-DEPENDENCE) -- ROOT CAUSE FIXED in the plasticity, but the repose ANGLE still does not
emerge. The old SAND return map was Hencky-STRAIN Drucker-Prager: dg = ||eps_dev|| + (3L+2u)/(2u)*trEps*alpha
-- the friction term scales with the VOLUMETRIC STRAIN trEps, which is ~0 for (near-isochoric) granular flow,
so phi was STRUCTURALLY ABSENT (phi=0/35/45 gave bit-identical deposits). REWROTE it to STRESS-space DP: the
trial Kirchhoff stress tau = 2u*eps + L*trEps, pressure p = tr(tau)/3, deviatoric s = tau - p; the cone
radius -3*alpha*p grows with the confining PRESSURE, so friction resists shear even under isochoric flow.
This is the correct Klar-2016-style cone and is bench-safe (MPM suite 14/14 + adjoint gradient check stay
green; configHash work N/A here).

REMAINING (GATED RED, honest): even with the correct stress-space friction + the settling dissipation, the
COLUMN-COLLAPSE test has NO intermediate pile regime -- WEAK damping lets the column over-flatten to a
monolayer (the residual ~1.2 m/s churn generates transient shear that exceeds the friction cone -> yields ->
flows flat), STRONG damping FREEZES the seeded pillar before any slump. Neither holds a phi-dependent pile, so
phi=0/35/45 stay ~identical. The explicit MLS-MPM lacks stable granular shear strength under slow flow.
UPGRADE SPEC: a solver-level granular upgrade (implicit/semi-implicit MPM for stable slow flow, or a gradual
DEPOSITION test instead of a violent collapse). The stress-space plasticity is the correct foundation; the
angle-of-repose red is NOT closed -- reported, not faked. (Sets up the dusty<->brown-sugar cohesion work:
once phi drives the angle on a stable solver, cohesion is the second parameter.)

## OMPL SPRINT — Phase 1: MOTION PLANNING + COLLISION CHECKING (2026-06-17)
**Goal:** plan over the live articulation config space (`krs::dyn::SerialChain`, the same Eigen oracle the
controller/FK gates use), collision-checked vs the scene + self, with RRT-Connect AND an optimizing planner
(RRT*). OMPL 1.7.0 integrated via vcpkg (BSD-3-clause, license ships at
`vcpkg_installed/x64-windows/share/ompl/copyright`). `find_package(ompl CONFIG REQUIRED)` -> `ompl::ompl`.

**Collision world (`krs::plan`, header-only, pure CPU/Eigen — NO GL/PhysX/OpenVDB):** the planner's state
validity checker must be deterministic (for PLAN-DETERMINISM) and independent of the live stateful PhysX scene,
so it is an ANALYTIC capsule-robot-vs-primitive-obstacle model (the textbook motion-planning collision model;
what FCL would compute, but closed-form and reproducible). Robot links = capsules in body-local frames; at a
config q, `SerialChain::fk` places them in the world. Obstacles = Sphere / HalfSpace / Box. All distances are
closed-form (point-segment, segment-segment, point-OBB + convex 1-D line-search for segment-OBB), so every
"penetration" is a REAL measured metre value, not a flag. Penetration(capsule,obstacle) = max(0, r - segDist);
self-collision = segment-segment distance on non-adjacent links (adjacency read from `joint(b).parent`).

**Planner (`krs::plan::MotionPlanner`):** `RealVectorStateSpace(nq)` bounded by per-dof `qLower/qUpper`;
StateValidityChecker = FK + `CollisionWorld::maxPenetration(q) < tol`; `setStateValidityCheckingResolution`
so OMPL densely checks motion segments. Determinism via `ompl::RNG::setSeed(seed)` BEFORE planner construction
AND termination on a DETERMINISTIC condition (not wall-clock): `plannerOrTerminationCondition(exactSoln,
IterationTerminationCondition(maxIters))` — success terminates on exact solution at the same iteration each run;
unreachable goal exhausts the deterministic iteration cap and returns FAILURE.

**Test robot (gate-controlled, hand-verifiable):** 3R arm — J1 yaw(Z), J2/J3 pitch(Y), two 0.5 m links
(r=0.05) + a 0.3 m shoulder post. At q=0 it points along +X at height 0.3 reaching x=1.0. Scenarios:
- **Open** (sphere obstacle at (0.8,0,0.3) r=0.25 + floor halfspace z=-0.2): start yaw=-1.2, goal yaw=+1.2.
  The straight-line config interp sweeps yaw through 0 -> forearm passes through the sphere centre
  (penetration ~0.30 m); a planned path lifts pitch over the sphere (penetration 0).
- **Boxed-in** (same sphere + pitch limits tightened to [-0.05,0.05]): the yaw=0 hyperplane is FULLY blocked
  for all reachable pitch, and RealVectorStateSpace (no SO(2) wrap) severs the long-way-around at the +/-pi
  yaw limit -> start and goal are in genuinely DISCONNECTED c-space components -> planner MUST return FAILURE.

**GATES (env `KRS_PLANNING_SELFTEST`; folded into KRS_OVERNIGHT_BENCH):**
- **PLAN-COLLISION-FREE** — RRTConnect AND RRTstar paths, re-checked INDEPENDENTLY at >=256 dense waypoints:
  max penetration == 0 (< 1e-6 m). NEG-CTRLS: (a) the naive straight-line "planner" path penetrates the sphere
  (measured ~0.30 m > 0 -> the independent checker FAILS it, proving the checker is load-bearing); (b) RRTstar
  path length <= RRTConnect length (the optimizing planner is exercised and no worse). Report both penetrations
  and both path lengths.
- **PLAN-LIMITS** — every planned waypoint within [qLower,qUpper] (min margin reported); time-parameterized at
  per-dof vMax respects velocity limits. NEG-CTRLS: a waypoint forced to qUpper+0.5 -> position-checker flags
  margin<0; a 2x-fast re-parameterization -> velocity-checker flags ratio>1.
- **PLAN-CONNECTIVITY** — solved path's first state == start and last == goal to < 1e-6. NEG-CTRL: the boxed-in
  (disconnected) scenario returns FAILURE (status != EXACT_SOLUTION, no path), NOT a fabricated path; the SAME
  planner on the open scenario SUCCEEDS (non-vacuous contrast).
- **PLAN-DETERMINISM** — same seed twice -> bitwise-identical waypoints (< 1e-9). NEG-CTRL: two DIFFERENT seeds
  -> paths differ (> 1e-6 at some waypoint), proving the seed actually drives the RNG (not a trivially-constant
  planner).
- **Profile:** plan time vs scene complexity (1/4/16 obstacles), reported as `[plan-profile]`.

### OMPL PHASE 1 RESULT (2026-06-17, KRS_PLANNING_SELFTEST + bench GATE PLAN) — ALL PASS
- **PLAN-COLLISION-FREE PASS**: RRTConnect path pen=0.000000, len=3.2611; RRTstar (optimizing) pen=0.000000,
  len=2.5803 (21% shorter — the optimizer is exercised and strictly improves). NEG straight-line interp
  start->goal penetrates the sphere by **0.2962 m** (matches the hand-computed 0.30) -> the INDEPENDENT
  256-waypoint re-check FAILS the naive path, proving the checker is load-bearing.
- **PLAN-LIMITS PASS**: posMargin=1.0005 (all waypoints in [qLower,qUpper]); velRatio=1.0000 at the constant-
  speed parameterization. NEG out-of-range waypoint -> margin -0.5000; NEG 2x-fast -> velRatio 2.0000 (flagged).
- **PLAN-CONNECTIVITY PASS**: solved path startErr=0, goalErr=0 (first==start, last==goal exactly). NEG boxed-in
  (pitch frozen to [-0.05,0.05] so the yaw=0 slice is fully blocked and the +/-pi limit severs the long way)
  -> solved=0 after the full 20001-iteration cap = FAILURE, not a fabricated path. Same planner on the open
  scene SUCCEEDS (non-vacuous contrast).
- **PLAN-DETERMINISM PASS**: same seed -> maxdiff 0.00e+00 (bit-identical waypoints); different seed -> maxdiff
  9.21e-01 (paths genuinely differ -> the seed drives the RNG, not a trivially-constant planner). Achieved via
  a locally-seeded RealVectorStateSampler (OMPL's global RNG::setSeed is one-shot per process and cannot make
  two in-process plans reproducible; RRTConnect's only randomness is uniform sampling, so the seeded sampler
  fully determinizes it).
- **Fuzz**: 8/8 random valid endpoint pairs solved, worst path penetration 0.000000 (no solved path collides).
- **Profile**: plan time ~0ms at 1/4/16 obstacles (off-corridor obstacles don't change the easy solve).
- **License**: OMPL 1.7.0, BSD-3-clause, ships at vcpkg_installed/x64-windows/share/ompl/copyright (Rice Univ).
- **BUILD-INFRA NOTE**: this sprint's builds initially never compiled because Git Bash mangles `cmd.exe /c`
  (POSIX path conversion turns `/c` into `C:\`), so run_configure.bat never ran and the stale exe lacked the
  new code. Fix: invoke the build from PowerShell with absolute paths --
  `cmd /c 'C:\Users\kurtz\KRStudio\KRStudio\run_configure.bat > C:\...\build.log 2>&1'`. Also: new source files
  need a FRESH cmake configure (CONFIGURE_DEPENDS glob) to enter build.ninja.

### OMPL PHASE 2 RESULT (2026-06-17, KRS_EXECUTE_SELFTEST + bench GATE EXECUTE) — ALL PASS
Execute the Phase-1 planned path through the computed-torque controller (krs::ctrl::computedTorque) via pure-CPU
forward dynamics (SerialChain::forwardDynamics + semi-implicit Euler) under gravity (0,0,-9.81). The arm carries
realistic link mass + COM-at-midpoint so the pitch joints bear a genuine gravity load. Plan with a 0.10 m safety
margin (sphere R 0.25 -> plan against 0.35) so the achieved trajectory stays collision-free despite tracking error.
- **EXECUTE-TRACK PASS**: peak ||q_achieved - q_commanded|| DURING motion -- computed torque **0.0154 rad**
  (after) vs OLD soft PD **0.6352 rad** (before). The soft PD (tau = Kp*e - Kd*qd, NO model/gravity feedforward)
  sags+lags the moving setpoint and FAILS the 0.10 bound; computed torque feeds back M*a + biasForces(gravity)
  and tracks -- a 41x improvement, matching the C-track class (recon soft-PD 0.61).
- **EXECUTE-COLLISION-FREE PASS**: the achieved computed-torque trajectory has max penetration **0.0000** vs the
  TRUE sphere (the 0.10 m plan margin absorbs the 0.015 rad tracking error). NEG executing a straight-line
  reference that drives the arm THROUGH the sphere -> achieved penetration **0.2999** (the executed-trajectory
  collision check is load-bearing).
- **EXECUTE-LIMITS PASS**: achieved posMargin 1.0017 (in bounds), velRatio **0.9982** (rides at vMax). NEG a
  3x-fast re-timing -> achieved velRatio **1.5850** (> 1, flagged; the arm can't reach 3x vMax but clearly
  exceeds the limit).

### OMPL PHASE 3 RESULT (2026-06-17, KRS_SUBFEAT_SELFTEST + bench GATE SUBFEAT) — ALL PASS
Sub-feature SELECTION BACKEND (krs::sel, SelectionService.hpp): a ray resolves to a SPECIFIC OCCT B-Rep face +
its EXACT analytic parameters (read from BRepFaceComponent, NOT a mesh/RANSAC fit; transformed to world via the
entity TransformComponent) + a selection-indicator GEOMETRY computed as DATA (a ring on the cylinder cross-
section, a square outline on a plane). Rendering is DEFERRED to a supervised UI session -- this is the gateable
backend only. The backend is OCCT-free (reads the ECS CadImporter populated) and reuses the production pick
(krs::pick::pickMesh), so the gate exercises the real selection path.
- **SUBFEAT-SELECT PASS**: 192/192 rays -> cylinder (100%); radius/axis-dir/axis-pos error all **0.000e+00**
  (<1e-9 vs OCCT -- the analytic read is exact). A ray pointing away -> **no selection** (miss=1). The mesh
  centroid-approximation radius is off by **1.296e-04** (>1e-6) -- the analytic B-Rep read is load-bearing.
- **SUBFEAT-INDICATOR-GEOMETRY PASS**: every indicator ring point lies on the analytic cylinder surface to
  **1.863e-09 m** (<1e-6).
- **SUBFEAT-HARD PASS**: a 5 mm bore on a 100 mm box -> the bore ray resolves the **small Cylinder**
  (radius 0.005000 m exactly, not the 0.1 m part), and a face ray resolves a **Plane** -- the small feature is
  disambiguated from the large planar faces.
- **DEFERRED**: edge selection (TopoDS_Edge endpoints) -- BRepFaceComponent is face-only today; faces
  (plane/cylinder/cone/sphere) are fully gated. The interactive picking UI + 3D indicator rendering remain for
  the supervised session (pixel gates verify geometry-of-elements, not usability).

### OMPL PHASE 4 RESULT (2026-06-17, KRS_ROBOTCHAIN_SELFTEST + bench GATE ROBOT-CHAIN) — ALL PASS
Robot entity + kinematic-chain DATA MODEL (krs::robot, RobotModel.hpp): a Robot owns links + joints + a rigid
base placement + a typed end-effector mount port. toChain() builds the krs::dyn::SerialChain the planner/executor
use (Phases 1-2). UI deferred to a supervised session.
- **ROBOT-CHAIN PASS**: nq=**3** == the 3 member joints; the base-to-floor transform is a rigid PLACEMENT, NOT a
  DOF (rule 6). The buggy control that wrongly treats the non-member joint as a DOF yields nq=**4** -> proves the
  chain EXCLUDES non-members. The Phase-1 planner plans over the owned DOFs (solved).
- **JOINT-FROM-FEATURE PASS**: a revolute frame derived from two coaxial bores (krs::joint::deriveRevoluteFrom
  Bores) matches the oracle to axis-dir err **0.0** / axis-pos err **0.0** (<1e-6). A non-coaxial pair (axis line
  offset 0.1 m) is **REJECTED** (no fabricated joint). Engineering fields (limits/effort/velocity/control-mode/
  node-id) are USER-SUPPLIED + provenance-tagged; the geometry derives only the FRAME.
- **MOUNT-PORT PASS**: a type-"flange-A" tool lands at the port frame to err **0.0** (<1e-9); a same-type tool with
  a different tip SWAPS (tip moves 0.10 m) WITHOUT redefining any joint (nq stays 3); a type-"flange-B" tool does
  **NOT attach** (the port is a one-sided TYPED frame).
- **CHAIN-EXPORT-ROUNDTRIP PASS**: serialize -> deserialize is lossless (4 joints, base placement, mount, and all
  provenance + engineering fields preserved to 1e-12). A truncated/corrupted export is **REJECTED** (ok=false, no
  fabricated robot); a flipped engineering field is **DETECTED** by the round-trip compare.

### OMPL PHASE 5 RESULT (2026-06-17, KRS_E2E_SELFTEST + bench GATE E2E) — ALL PASS
End-to-end closure: a robot is DEFINED via the Phase-4 chain data model -> toChain() -> PLANNED (Phase-1
MotionPlanner) -> EXECUTED (Phase-2 computed torque under gravity). Every stage's number is asserted, and
severing any single stage localizes the break to THAT stage (upstream stages stay healthy) -- the causal-chain
instrument.
- **E2E-PLAN-EXECUTE PASS**: DEFINE nq=**3** | PLAN solved, path penetration **0.0000** | EXECUTE tracking error
  **0.0154 rad**, achieved penetration **0.0000**. The pipeline closes with every stage's number healthy.
- **SEVER-DEFINE -> localized@DEFINE**: an empty robot -> nq=0, define unhealthy; downstream cannot run.
- **SEVER-PLAN -> localized@PLAN**: define healthy, but disconnected (tight-pitch) bounds -> the planner returns
  FAILURE -> plan unhealthy; the break is at PLAN, not DEFINE.
- **SEVER-EXECUTE -> localized@EXECUTE**: define + plan healthy, but the soft PD (no model/gravity feedforward)
  -> tracking error **0.5984 rad** -> execute unhealthy; the break is at EXECUTE.

### OMPL SPRINT COMPLETE (2026-06-17) — all 5 phases gated green, branch ompl-phase1 (PR-ready)
KRS_OVERNIGHT_BENCH now includes GATE PLAN / EXECUTE / SUBFEAT / ROBOT-CHAIN / E2E. OMPL 1.7.0 BSD-3-clause
license ships. INTERACTIVE UI (selection picking, 3D indicator rendering, joint-property HUD) DEFERRED to a
supervised session per the directive -- the backends (planning, execution, selection, chain, mount port) are
proven; the pixel-level UI needs the operator's eyes.

## DYNAMICS-DRIVEN AVOIDANCE FIELD SPRINT (2026-06-17, branch avoidance-field)
Builds the gateable FIELD foundation; the VISUALIZER (revive FieldVisualizerPass arrow field) and the MPC
CONTROLLER are DEFERRED. Recon mapped the substrate: an existing effector vector field (FieldSolver CPU
sampler + PointEffector/DirectionalEffector components + FieldSourceTag + FieldVisualizerPass) is the
canonical field (rule 6) -- the avoidance field places effectors and is sampled by FieldSolver. There is NO
runtime ECS->MQTT publisher and NO property catalog; acceleration is not stored (finite-diff velocity, the
FidelityRigid pattern); no particle->SDF path exists (net-new min|x-p|-r splat reusing SdfColliderView /
sdfDistanceWorld trilinear + sign<0=inside). Nodes: combo->param is the headless-gateable SSOT; entt::entity
handle ports already wire; node gates drive setParam+process (no GUI).

ARCHITECTURE: new `krs::twin::PropertyCatalog` (introspection + value + stale-aware frequency, broker-free
gateable); Object/Property/Emitter NODES (src/Nodes/) reading the catalog + placing effectors; new
`krs::field` module (dynamics amplitude law + particle grid-SDF + uncertainty/temporal) in src/Physics +
include/PhysicsHeaders. All gates headless/CPU, folded into KRS_OVERNIGHT_BENCH; no-broker by design.

### PHASE 1 — ECS->catalog publishing + generalized Object/Property nodes
PropertyCatalog (include/UtilityHeaders/PropertyCatalog.hpp, krs::twin): a singleton introspectable registry
keyed by object id -> {name, list of PropertyEntry{name,type,value[4],stamp-ring}}. `publishSceneState(cat,
registry, simTime, accelTracker)` populates it from reg.view<RigidBodyComponent,TransformComponent>(): per
body publishes position/orientation/linearVelocity/angularVelocity/linearAcceleration/angularAcceleration/
mass (acceleration = (v - prevV)/dt via the AccelTracker prev-velocity map -- accel is NOT a stored field).
Object name via the debugNameFor 3-tier (TagComponent.tag). Introspection: propertiesOf(objId) = the catalog
query. Frequency: stale-aware Hz = min(measured_from_stamp_ring, 1/(now - lastStamp)) so a stalled stream's
reported Hz DROPS as now advances past lastStamp (the naive measured-only value freezes -> the neg-control).
Object node (combo of scene bodies via TagComponent -> entt::entity handle output), Property node (entt::
entity input -> combo of THAT object's catalog properties -> live value output + publish-frequency output).
Combo selection is a node PARAM (the SSOT the gate drives via setParam; no GUI). Nodes in src/Nodes/TwinNodes.cpp.
GATES (KRS_TWIN_SELFTEST + bench), each measured number + non-vacuous neg-control:
- PUBLISH-CATALOG: querying object X returns its exact published-property list (7 props); a non-existent
  object -> empty (not garbage); an unpublished property name is NOT listed.
- OBJECT-PROPERTY-NODES: wired Object+Property nodes read the selected property's live value to <1e-6 of the
  ECS value; changing the Object param re-populates the Property node's property list for the new object;
  disconnected Object input -> no valid property / no output.
- FREQUENCY-OUTPUT (stale guard): the reported Hz matches a known publish rate to <tol; after the stream
  STALLS, the reported Hz drops toward 0 (assert it falls below a fraction of nominal); the naive
  measured-only Hz FREEZES at nominal (the neg-control proving staleness is detected).

### AVOIDANCE PHASE 1 RESULT (2026-06-17, KRS_TWIN_SELFTEST + bench GATE TWIN) — ALL PASS
PropertyCatalog (krs::twin) + Object/Property nodes (src/Nodes/TwinNodes.cpp), broker-free, headless.
- **PUBLISH-CATALOG PASS**: 'alpha' lists exactly 7 canonical props; ALL 7 VALUES match the live ECS (not just
  names -- position/orientation(w,x,y,z)/linearVelocity/angularVelocity/mass + acceleration=0 on first
  observation); the two-step acceleration finite-diff reads dv/dt=480.0 (want 480.0); a non-existent object ->
  empty; a phantom property name -> absent; objects=3.
- **OBJECT-PROPERTY-NODES PASS**: wired Object+Property nodes read mass=4.0 (exact), position and
  linearVelocity vec3 to <1e-6 of the ECS; changing the Object re-populates the property list for the new
  object (identity + distinguishing value pinned); a disconnected Object input -> no output.
- **FREQUENCY-OUTPUT PASS**: catalog reportedHz fresh=200, after 1s stall=1.00 (-> 0), while the naive
  measuredHz freezes at 200 (the neg-control); AND the NODE path: PropertyNode Frequency output fresh=60,
  stalled=1.00 (drops) -- the live node reads frequency at the catalog's real now().
- **ADVERSARIAL REVIEW (3 skeptics) caught + fixed 3 real bugs + 1 vacuous spot**: (1) value-fidelity covered
  only 2/7 properties -> added all-7 value coverage + two-step accel; (2) the live PropertyNode frequency was
  stale-BLIND (m_now hardwired 0) -> the node now reads catalog.now() + a NODE-level stall test that would have
  failed the old code; (3) reportedHz froze when now<=lastStamp (clock skew) -> clamp since>=0; (4) stale output
  packet could mask a non-firing compute -> PropertyNode resets its output packets on entry.

### AVOIDANCE PHASE 2 RESULT (2026-06-17, KRS_EMITTER_SELFTEST + bench GATE EMITTER) — ALL PASS
Emitter node (krs::field, src/Nodes/EmitterNodes.cpp): one node, two emitted TYPES by combo param. AVOIDANCE-
FIELD places/updates an engine PointEffectorComponent (+FieldSourceTag) on the object, sampled by FieldSolver
(rule 6 -- one field); strength = amplitude(INPUT, so Phase 3 can drive it) * sign; Linear falloff. SUBSTANCE
emits a particle stream FROM the object (krs::field::SubstanceEmitter) at a node-driven rate.
- **EMITTER-FIELD PASS**: |field| at d=2 is **6.0** and at d=4 is **2.0** (two samples pin the Linear slope +
  radius, not one point); repulsive points away (+X), attractive points toward (-X); a NEGATIVE wired
  amplitude (sign +1) also gives attractive (signed amplitude works for Phase 3); zero amplitude REMOVES the
  source (no field, no ghost effector); a disconnected object emits nothing.
- **EMITTER-SUBSTANCE PASS**: 50 particles in 0.5 s (= rate 100 * T 0.5); every particle originates AT the
  object; moving the object -> 50 NEW particles at the new position (proves it's a MODIFIER on the object, not
  a pre-placed body); rate<=0 -> no spawn.
- **EMITTER-TYPE-SWITCH PASS**: field type -> effector present, no substance; field->INVALID directly tears
  down the field source (not pre-satisfied); field->substance tears down the field + emits.
- **ADVERSARIAL REVIEW (2 skeptics): no real-bugs, no vacuous gates ("SOLID")**; applied the hardening it
  suggested (second field sample for slope, signed-amplitude test, literal zero-amp teardown, explicit
  field->invalid transition, rate-0 guard).

### AVOIDANCE PHASE 3 RESULT (2026-06-17, KRS_FIELDLAW_SELFTEST + bench GATE FIELD-LAW) — ALL PASS
Dynamics-driven field-amplitude law (RL-free): amplitude = base + wV*max(0,vApproach) + wA*aApproach, clamped
>=0, where vApproach/aApproach are the velocity/acceleration components TOWARD the protected point. DynamicsField
node (src/Nodes/DynamicsFieldNodes.cpp) reads the object's velocity+acceleration from the Phase-1 catalog and
outputs the amplitude, which pipes into the Phase-2 emitter.
- **FIELD-DYNAMICS PASS (the can't-fake ordering)**: amplitude ordering accel=**3.5** > const=**3.0** >
  decel=**2.5** > static=**1.0** for the SAME geometry+speed (only acceleration sign differs); static ==
  baseline. The full chain runs THROUGH THE NODE (5 catalog bodies published together: accel/const/decel/static
  + receding). A RECEDING object reads ~baseline (1.0) < const -- "where it's GOING, not where it IS". Two
  genuine failing neg-controls: the geometry-only model (ignores acceleration) gives accel==const==decel (loses
  the ordering), and the NODE with AccelWeight=0 also gives accel==decel.
- **FIELD-AUTHORABLE PASS**: increasing wA 0.5->1.0 raises the amplitude 3.5->4.0 (predictable, by exactly
  dwA*aApp); a CONNECTED AccelWeight (0.2->2.0) drives the amplitude (3.2->5.0); an UNCONNECTED weight uses the
  default (no effect from an unwired value).
- **PIPE law->emitter PASS**: the dynamic amplitude (3.5) flows through a real wire into the Phase-2 emitter and
  becomes the PointEffector field strength (match <1e-4).
- **ADVERSARIAL REVIEW (2 skeptics) caught a GATE-VACUOUS tautology**: the original geometry-only neg-control
  compared base==base (always true, zero discriminating power). FIXED: geometryOnlyAmplitude now takes the
  dynamics but drops the acceleration term, so it genuinely loses the ordering; also added the full node chain,
  the receding case, and the node-level wA=0 control (closing the minor "only accel>decel proven" gaps).

### AVOIDANCE PHASE 4 RESULT (2026-06-17, KRS_SDF_SELFTEST + bench GATE SDF) — ALL PASS
Particle grid-SDF (krs::field::GridSdf, AvoidanceField.hpp): splats particles to a signed-distance grid
(d = min|node-p|-r, <0 inside, engine SdfColliderView convention) with a configurable band (band>=grid = exact
brute force; small band = fast near-field). Provides trilinear distance + central-diff gradient. NOT a per-
frame mesh. substanceFieldMagnitude = dynamicAmplitude(streamSpeed,...) * falloff(SDF distance) -- the Phase-3
dynamics scaling applied to a substance stream.
- **SDF-DISTANCE PASS**: trilinear sample vs analytic |x-cloud|-r, max err **0.00106 m** (<0.02); an INTERIOR
  point reads d<0 (the <0-inside convention, err 0.0); an empty SDF reads 1e9 (neg-ctrl).
- **SDF-GRADIENT PASS**: the gradient points AWAY from the nearest substance, dot(grad, analytic-away)=**1.0000**
  (>0.99), |grad|~0.99; an empty (flat) SDF gives |grad|=0 (no dodge direction, neg-ctrl).
- **SDF-DYNAMICS PASS**: a fast-moving stream field=**2.940** vs a still pool=**0.840** of the SAME geometry
  (the dynamics scaling applies to the substance); a geometry-only model (wV=wA=0, fed the same moving/still
  inputs) gives identical 0.840 (a real failing model, not base==base).
- **SDF-PERF PASS**: the BANDED splat (band=8 ~0.51 m, the avoidance-relevant near field that is actually
  CONSUMED) runs at **18.2 ms/frame** at 48^3 / 1000 particles (<33 ms interactive); it is CORRECT within the
  band (err 0.0068 m) and degrades GRACEFULLY to 0 beyond (sentinel -> falloff 0, no avoidance far away).
- **ADVERSARIAL REVIEW (2 skeptics) caught a GATE-VACUOUS tautology + a REAL-BUG**: (1) the geometry-only
  neg-control was again base*falloff==base*falloff (tautology) -> fixed to a real zero-weight model fed the
  actual inputs; (2) the original perf gate timed a band=4 field that was 92% empty and NEVER sampled, while
  the validated (band=big) field actually cost ~235 ms -> fixed: perf now times the CONSUMED band-8 near-field,
  asserts it is correct in-band, and proves graceful sentinel degradation beyond; added the interior <0 probe.

### AVOIDANCE PHASE 4.5 RESULT (2026-06-17, KRS_UNCERTAINTY_SELFTEST + bench GATE UNCERTAINTY) — ALL PASS
SDF uncertainty + reaction tempering + temporal coherence (krs::field, AvoidanceField.hpp + UncertaintyGate.cpp).
- **UNCERTAINTY PASS**: a fresh region is high-variance (1.0); after 10 Kalman observations the variance drops to
  **0.029** monotonically. NEG a no-update model (observed the same 10 times but variance never fuses) stays at
  the prior 1.0 -> fails the drop contrast.
- **REACTION-TEMPER PASS**: for the SAME magnitude, a high-uncertainty region gets a GENTLER reaction (gain 0.200,
  react 1.00) than a low-uncertainty one (gain 0.833, react 4.17). The TWO COUPLINGS are demonstrated ORTHOGONAL:
  a fast+unsure object has react **0.70** < a slow+sure object's **1.08** DESPITE a higher magnitude (3.5>1.3) --
  dynamics drives magnitude, uncertainty drives reaction-sharpness, independently. NEG an uncertainty-blind gain
  model can't temper.
- **TEMPORAL-STABLE PASS**: with a noisy obstacle the raw gradient frame-delta is **0.257** (chatter) while the
  EMA-filtered is **0.044** (5.8x smoother); the filtered direction tracks away-from-M0 (+X); and after a
  PERSISTENT maneuver (mean shifts to M1) the filtered direction FOLLOWS (dot=1.000) -- so a FROZEN filter (stable
  but stale/wrong) FAILS. The filter is proven stable-yet-responsive-yet-correct.
- **ADVERSARIAL REVIEW (2 skeptics) caught a REAL-BUG + 2 vacuous sub-controls**: (1) the original TEMPORAL gate
  was one-sided -- a FROZEN filter (maxFiltDelta=0) passed while giving a stale 18.6-deg-wrong direction; FIXED by
  adding the M0-tracking + maneuver-following (M1) requirements a frozen filter cannot meet; (2)+(3) the UNCERTAINTY
  and REACTION-TEMPER neg-controls were prior==prior / baseGain==baseGain tautologies -> replaced with real failing
  models that LOSE the property; demonstrated the orthogonal cross-coupling instead of asserting it.

### AVOIDANCE FIELD SPRINT COMPLETE (2026-06-17) — all phases 1-4.5 gated green, branch avoidance-field
KRS_OVERNIGHT_BENCH now includes GATE TWIN / EMITTER / FIELD-LAW / SDF / UNCERTAINTY (89/89 gate groups PASS).
The gateable FIELD FOUNDATION is built and trustworthy: ECS->catalog introspection + Object/Property/Emitter/
DynamicsField nodes, the dynamics-driven amplitude law, the particle grid-SDF (distance+gradient), and the
uncertainty/reaction-tempering/temporal-coherence safety layer. DEFERRED (as directed): the field VISUALIZER
(revive FieldVisualizerPass arrow field, a supervised/visual session) and the MPC/reactive CONTROLLER that
consumes this field. Every gate carries a measured number + a non-vacuous negative control; adversarial review
per phase caught + fixed 2 gate-vacuous tautologies and 2 real-bugs (the node frequency stale-freeze, the perf
phantom-field, the temporal frozen-filter) that would otherwise have passed-while-broken.

## LIVE FLUID SDF + VISUALIZER + ORB SPRINT (2026-06-18, default branch)
### GATE SDF-LICENSE (Phase 0 -- reporting/decision gate) — nvblox license OK, but HARDWARE pivot to GPU EDT
nvblox license: **Apache-2.0** (permissive, ships fine license-wise -- confirmed by the operator). HOWEVER
nvblox is a **CUDA library** (its ESDF/TSDF core is CUDA kernels compiled by nvcc, run on an NVIDIA GPU). This
build machine is an **AMD Radeon 780M iGPU** with **NO NVIDIA GPU and NO CUDA toolkit** (nvidia-smi absent,
nvcc absent, CUDA_PATH unset, no toolkit dir). nvblox therefore **cannot build** (no nvcc) and **cannot run**
(no CUDA runtime / NVIDIA GPU) here -- a hardware/toolchain blocker, NOT a license one. Surfaced as a genuine
architectural decision; the operator chose to **PIVOT to the GPU EDT** (Felzenszwalb-Huttenlocher separable
Euclidean Distance Transform) on the engine's existing GL-4.3 compute pipeline: no CUDA, no license, exact
Euclidean SDF, single-digit ms, AMD-compatible -- meeting the same SUCCESS METRICS (SDF-SPEED/CORRECT/LIVE-
SDF/LIVE-PERF). nvblox remains the intended backend on NVIDIA hardware; the EDT is the portable equivalent.
PATH TAKEN: **GPU EDT (FH separable)** over a particle occupancy grid. NEG-CTRL: n/a (decision/report gate).

### PHASE 1 — GPU EDT SDF (Jump Flooding), gated on the REAL live fluid
GPU distance transform via JUMP FLOODING (Rong&Tan 2006 -- the GL-feasible GPU EDT; exact FH separable needs
per-thread size-N arrays impractical in GLSL). Three compute shaders (shaders/edt_*_comp.glsl, GL 4.3): (a)
edt_seed -- one thread/particle reads the live fluid SSBO, writes the particle world-pos into a vec4 seed grid
at its cell (seed.w=1 valid; grid pre-cleared via glClearBufferData); (b) edt_jfa -- one thread/cell, for step
= N/2..1 (log2 N passes) adopts the nearest valid seed among 26 neighbours at +/-step (ping-pong two grids);
result: seed.xyz = nearest particle world-pos per cell. Distance = |cellPos - seed| - radius (<0 inside,
engine convention); gradient = normalize(cellPos - seed) (the away-from-water dodge direction). Particle-count-
INDEPENDENT (O(N^3 log N), N=64). Narrow-band at consumption (clamp used distance to the avoidance radius).
New: include/RenderingHeaders/GpuSdfEdt.hpp + src/Rendering/GpuSdfEdt.cpp (SSBO mgmt + dispatch), the 3 shaders,
gate src/Rendering/LiveSdfGate.cpp (RenderingSystem member, runs the live FluidSystem).
GATES (KRS_LIVESDF_SELFTEST + bench), measured + non-vacuous neg-ctrl:
- SDF-SPEED (HARD, LIVE fluid): seed a FluidVolume, update() the real PBF solver, dispatch JFA over the LIVE
  particle SSBO, **glFinish()** (warm-up first) then time -> <15ms/frame; report particle count + grid res.
  NEG-CTRL: the old O(cells*particles) brute force (krs::field::GridSdf band=big) on the same read-back
  particles reproduces ~235ms and FAILS the 15ms bound.
- SDF-CORRECT: the JFA SDF distance at sample points matches the analytic |point - nearest live particle| - r
  to <tol (~1-2 cells, JFA sub-cell error); gradient points away (dot ~1.0); empty region -> large/no-collision.
  NEG-CTRL: a wrong SDF (pre-JFA seed grid / shifted grid) mismatches the analytic.

PHASE 1 DELIVERED (KRS_LIVESDF_SELFTEST + bench, 90/90): SDF-SPEED = **1.19 ms/frame** JFA over **32768
live PBF particles @ 64^3** (<15ms); NEG brute-force GridSdf on an 800-particle SUBSET, same grid -> **722.9 ms**
(>>15ms, real failing baseline). SDF-CORRECT: max|JFA - analytic| = **0.024 m** (<0.075); gradient dot(away) =
**0.988**; interior (slab centre) d = **-0.038 < 0** (the <0-inside sign convention, at avoidance-margin radius
0.08 m > cell diagonal 0.065 m); empty region floods to a real distance; NEG shifted-grid origin misreads.
Committed + pushed to master (159494f).

### PHASE 2 — LIVE WATER: the SDF FOLLOWS the moving fluid (temporal tracking)
Prove the JFA SDF is rebuilt from the LIVE particle SSBO every frame so its zero-crossing TRACKS the water as it
moves -- and that a baked-once SDF does NOT (the C2 ghost discipline). New gate src/Rendering/LiveTrackGate.cpp
(RenderingSystem::runLiveTrackGate, KRS_LIVETRACK_SELFTEST + bench). Method: seed a THIN fluid slab high in the
domain (centre (0,1.2,0), halfExtents (0.22,0.12,0.22)), real downward gravity so it falls COHERENTLY; build the
SDF at t0 and READBACK a baked copy (the ghost); advance ~30 frames; rebuild the SDF LIVE at t1. Read the ACTUAL
water centroid from the particle buffer at t0 (C0) and t1 (C1).
GATES (measured + non-vacuous neg-ctrl):
- LIVE-SDF (tracking): the water fell |C0-C1| > 0.15 m (real motion); the LIVE SDF reads INSIDE at the new
  centroid (d(C1) < 0) and OUTSIDE at the vacated old centroid (d(C0) > 0) -- the zero-crossing followed the
  water; cross-checked |live - analytic|@C1 < ~tol. NEG-CTRL (GHOST): the baked-once t0 SDF reads inside at the
  OLD centroid (d(C0) < 0) and outside at the NEW (d(C1) > 0) -- exactly backwards, i.e. it stayed on the stale
  water and FAILED to follow (a real lagging model, not base==base).
- LIVE-PERF: the full per-frame path on the REAL moving fluid = GPU SDF gen + 64^3 grid readback, glFinish-timed
  (warm-up first), holds < 15 ms; gen-only reported alongside.

### PHASE 3 — REVIVE the field visualizer (arrows are DATA, not decoration)
The FieldVisualizerPass overlay shipped DORMANT: it executes every frame but no FieldVisualizerComponent
was ever emplaced, so its view was always empty. REVIVAL: emplace a FieldVisualizerComponent (Arrows) at
scene boot (MainWindow ctor, after the default grid) with an initialized blue->green->red intensity gradient
and bounds [-3,3]^3, so the arrow field renders over any field source -- the avoidance-emitter node the
operator drops places PointEffectors(+FieldSourceTag) the visualizer now traces. KRS_FIELD_DEMO adds a
standalone point source for a boot-time starburst (OPERATOR VISUAL-CONFIRM).
GATE VISUALIZER-DATA (KRS_FIELDVIS_SELFTEST + bench), measured + non-vacuous neg-ctrl: dispatch the REAL
arrow_field_compute shader over a sample grid with a known effector field (1 linear-falloff point + 1
directional), read back the InstanceData, and DECODE each arrow's field straight out of its model matrix
(modelMatrix = T(worldPos)*R(+Z->dir)*S(hs,hs,|f|*vs) => worldPos = col 3, field = col 2 / vectorScale).
- VISUALIZER-DATA: the decoded arrow vectors match the analytic effector field at each arrow's own position
  -- direction (dot > 0.99) AND magnitude (<2% relErr) AND intensity -- and the arrow COUNT equals the grid
  points above the cull threshold (the visualizer culls exactly, not decoratively).
- NEG-CTRL (STALE): the SAME arrows compared against a MOVED field mismatch (max dir err > 0.1) while freshly
  recomputed arrows track the moved field -- a real stale-visualizer model, not base==base.
DELIVERED: 125/125 arrows, max dir err 0.0000, max |mag| relErr 0.0000, intensity err 0.0000 (the GPU shader
and the independent CPU field formula agree to float precision); STALE-A-vs-moved-B max dir err = 1.8086
(dot ~= -0.81, near-reversed) while fresh-B tracks at 0.0000. OPERATOR VISUAL-CONFIRM: run an avoidance
emitter (or boot with KRS_FIELD_DEMO=1) and confirm a fan of arrows points along the field, blue (weak) ->
red (strong), longer where the field is stronger.

### PHASE 4 — VELOCITY-PROBE ORB NODE (glass sphere, volume velocity query, bidirectional lifecycle)
A transparent glass SPHERE probe (NOT a rigid body -- a measurement VOLUME), scalable + translatable, bound
1:1 to a VelocityProbeOrb graph node, reporting the AVERAGE VELOCITY of the dynamic particles inside its
volume (a containment query, NOT flux-through-plane). Shared headless-testable core: krs::orb (OrbProbe.hpp/.cpp)
-- averageVelocityInSphere (the volume query) + decorateProbeOrb / findOrbForNode / removeOrbForNode / orbCount
(the orb<->node binding on an entt::registry). New OrbBindingComponent{nodeId,color,radius,measuredVelocity,
containedCount}. Glass sphere spawned via SceneBuilder::spawnPrimitive(IcoSphere) with AutoCollisionComponent
REMOVED (probe volume) + GlassComponent tinted by the node colour. Because the particle SSBO is only readable
where GL is current, the GL-side orb-probe step computes each orb's velocity each frame and writes it into the
component; the node's compute() relays it to its output ports.
GATES (KRS_ORB_SELFTEST + bench), measured + non-vacuous neg-ctrl:
- ORB-VELOCITY: (synthetic, exact) 125 particles inside a sphere all moving at vIn=(1,2,3) + 60 outside at
  (9,-9,9) -> the query returns EXACTLY vIn over exactly 125 contained, while the GLOBAL average (the real
  "average everything" neg-ctrl model) is off by 4.82; off-stream sphere -> count 0, |v| 0; empty -> 0.
  (REAL fluid) 21952 live PBF particles under gravity: an orb at the centroid contains 2080 and reports avg
  velocity (0,-0.736,0) (the falling stream), consistent with an independent manual contained-average to
  <1e-5; an orb moved 3 m off-stream contains 0 -> proves a VOLUME query on real data, not a global average.
- ORB-LIFECYCLE: 5 nodes -> 5 orbs with 5 DISTINCT colours each matching its node, each glass + NO collider;
  delete-node 20 removes its orb (others remain); delete-orb 30 recovers the bound node id (==30) so the
  reverse hook can remove the node; NEG-CTRL: a non-propagating delete leaves an ORPHAN (the real failing
  model the assertion catches) while the correct removal clears it.
OPERATOR VISUAL-CONFIRM: instancing a VelocityProbeOrb node spawns one transparent glass sphere tinted the
node's colour; scaling/moving it changes the measured volume; deleting the node removes the sphere and vice
versa; N nodes -> N spheres in N colours; moving a sphere off the fluid stream drops its reported speed to ~0.

PHASE 4b RUNTIME DELIVERED: VelocityProbeOrb node (src/Nodes/VelocityProbeOrbNode.cpp) registered with
NodeFactory; the nodeCreated hook spawns its bound glass IcoSphere (auto-collider stripped, tinted a
distinct golden-ratio colour per node id) and sets the binding key; the nodeDeleted hook removes the orb
(forward lifecycle); an entt on_destroy<OrbBindingComponent> observer + a 50 ms UI tick delete the matching
graph node when an orb is destroyed (reverse lifecycle, guarded by nodeExists so a node-side delete does not
loop). The GL-side per-frame step RenderingSystem::updateOrbProbes (OrbProbeSystem.cpp, called after the fluid
sim where the SSBO is readable) reads back the live fluid particles and writes each orb's measuredVelocity;
the node's compute() relays it to Velocity/Speed/Count ports and pushes its Radius back to the orb.
Smoke-tested: the node registers; KRS_ORB_DEMO=N instances N nodes -> N glass orbs in N colours; the app
boots and runs crash-free with the per-frame probe step (default no-orb path is a clean no-op). CAVEAT: the
KRS_FLUID_DEMO basin water does not auto-seed particles on the auto-play boot path (a pre-existing fluid-demo
plumbing detail, orthogonal to the orb) -- so the live nonzero-velocity readout is exercised by GATE
ORB-VELOCITY on the real seeded fluid (2080 particles, avg -0.736 m/s) rather than at demo boot; with a fluid
that has live particles the same updateOrbProbes path reports the in-volume average and drops to ~0 off-stream.
OPERATOR VISUAL-CONFIRM: instance VelocityProbeOrb node(s) over a running fluid -> one glass sphere per node
in a distinct colour; scale/move resizes/repositions the probe; deleting the node removes the sphere and
deleting the sphere removes the node; dragging a sphere off the stream drops its reported speed to ~0.

### UI BUGFIX — field-visualizer toggle crash + node combo interactivity + menu wiring
Operator-reported issues fixed (verified on-screen with pixel measurement, not just headlessly):
- CRASH on the field-visualizer toggle: PreviewViewport's constructor called SceneBuilder::createCamera(*m_scene)
  before assigning m_scene = m_previewScene.get(), dereferencing the null base-class scene -- the FlowVisualizer
  menu embeds 3 PreviewViewports, so opening it segfaulted. Fixed by pointing m_scene at the preview scene first.
- Node COMBO boxes not interactable: the node GraphicsView pans on left-drag (ScrollHandDrag), which swallowed
  clicks on embedded controls. DroppableGraphicsView now suppresses the pan when a left-press lands on an
  embedded proxy widget (verified: dragMode 1->0 on a body-centre press), and the Object/Property combos show a
  QMenu dropdown at the box (mapToGlobal through the proxy) instead of the mis-positioned native popup. The Object
  combo was widened (170 px, was clipping body names).
- Field-visualizer menu was UNWIRED: m_flowVisualizerMenu was never assigned and settingsChanged never connected,
  so the menu could not control the visualizer. showMenu now assigns it, connects settingsChanged/transformChanged
  to the handlers, syncs the menu from the live FieldVisualizerComponent, and applies the initial state; the
  handlers are null-guarded and the pointer cleared on close. Verified: opening the menu logs "[SETTINGS APPLIED]
  Mode: Arrows, Density 16x16x16" + "successfully applied to component", no crash.
- VERIFIED ON SCREEN: with a field source present, the FieldVisualizerPass arrow field renders -- a live viewport
  grab with the visualizer ON vs OFF (scene frozen) differs by 274,280 pixels (21% of a 2049x635 viewport); the
  OFF frame shows zero arrows (clean negative control). Bench stays 94/94.

### UI/NODE BUGFIX — falling-object velocity readout + property-aware output gates
Operator report: a numeric readout wired to a Property node shows NO linear velocity for an object dropped from
a height, and the field-visualizer arrows read a constant 1.0; operator suspected the "physics->node poller is
not running every frame". A 5-agent workflow traced the whole chain and (high confidence) found the poller was
NOT the cause: writeBackTransforms() writes rb.linearVelocity every tick, ObjectNode (indeg 0) republishes the
catalog every eval, PropertyNode recomputes every compute. The real defect: the Property node's single scalar
"Value" output = e->v[0] = only the X component, which is ~0 the entire fall (a dropped body moves along -Y);
the same scalar fed the field emitter's Amplitude (~0 -> static field -> the shader's normalized colour ceiling
shows 1.0). FIX = the operator's own follow-up: make the Property node's OUTPUT gates adapt to the selected
property's component type --
  - vector quantity (position / linear|angular velocity / linear|angular acceleration) -> X, Y, Z scalar gates
  - orientation (quaternion) -> Roll, Pitch, Yaw (deg) gates
  - mass (scalar) -> a single Value gate
  - always plus the stale-aware Frequency gate.
Wiring the Y gate to the readout now shows the fall velocity; driving the emitter Amplitude from a live component
makes the arrows track. Implemented as DYNAMIC QtNodes ports: Node::reconfigurePorts (installed by NodeDelegate)
brackets the output-port mutation in portsAboutToBeDeleted/portsDeleted/portsAboutToBeInserted/portsInserted
(correct ordering: announce-before-mutate, since DataFlowGraphModel reads nPorts() live) so stale connections
are cleaned and the visual node rebuilds; INPUT ports (Trigger, Object) + their connections are preserved across
a property change. Node::selectNamedOption drives the selection (combo + future param-restore). Also bumped the
node eval rate 30 -> 60 Hz so the poll runs per frame (the operator's explicit ask; minor freshness win for the
live emitter/visualizer -- NOT the readout fix). GATE TWIN updated + green (mass=scalar-only, position/velocity
xyz with the Y gate carrying vy exactly, orientation rpy); live QtNodes rebuild verified by screenshot for all
three layouts (no crash); bench 94/94.

### EMITTER FIELD CONTROLS — Radius + Falloff-rate exposed on the node UI
Operator wanted "smaller but still strong" fields and asked how the emitter's radius/falloff work. Previously
the field_emitter node only exposed Amplitude (strength); the field RADIUS was a fixed param (5.0 m, no UI) and
the falloff was hardcoded Linear, so the field size/shape were not controllable. Now:
- field_emitter has two new INPUT ports (auto-widget spinboxes, also wireable): **Radius** (m, default 5.0) and
  **Falloff** (the falloff RATE, default 1.0). Per-input spinboxes now initialize from the port literal (added
  Node::literalD + NodeDelegate init) so they show 5.0 / 1.0 instead of 0.
- Falloff is a continuous EXPONENT generalizing the linear law: magnitude = strength * (1 - d/R)^falloff.
  falloff 0 = no falloff (constant strength to the edge, hard cutoff), 1 = linear (the old behavior, backward
  compatible), >1 = steeper / more concentrated near the source. So a small Radius + high Amplitude + falloff>1
  = a small but strong, concentrated field; falloff 0 = strong everywhere inside then a sharp edge.
- PointEffectorComponent gained float falloffExponent (default 1.0); FieldSolver applies it in the Linear
  branch; the visualizer shader (field_visualizer_comp.glsl) applies the IDENTICAL formula and also now honours
  InverseSquare (1/d) + None (constant) so the arrows match the physics for every falloff type (PointEffectorGpu
  'padding' repurposed as falloffExponent -- same std140 slot). The "Rate" port is unchanged: it is the SUBSTANCE
  particle spawn rate (type 1), NOT the field radius.
GATE EMITTER extended (RADIUS-FALLOFF subtest): R=2 -> |f|@1=5.0, ~0 past the edge; at d=2 inside R=5 the falloff
rates 0/1/2 give 10.0 / 6.0 / 3.6 (flat / linear / steeper, strictly monotone). Bench 94/94; emitter node UI
screenshot confirms the Amplitude/Radius(5)/Falloff(1)/Rate spinboxes.

## NODE INPUT FOUNDATION + TRIGGER + IK TARGET + OMPL NODE SPRINT
### PHASE 1 — IN-WIDGET INPUT PATH (the foundation; everything below needs it)
RECON (4-agent workflow, high confidence): a node's own in-widget value does NOT reach its compute when the
port is UNCONNECTED. (a) SPINBOX: NodeDelegate::populateEmbeddedWidget builds the spinbox and calls
sb->setValue(literalD(...)) BEFORE the valueChanged connect, so the initial value fires into no handler and the
port LITERAL is never seeded; literalValue stays nullopt; getInput returns nullopt for an untouched port; binary
nodes (Add: "if (a && b)") then emit nothing -> "ignores its spin box". A wire works because getInput prefers
port.packet. (b) COMBO: enum input ports fall into the `else { delete row; continue; }` -- NO widget is mounted
for them, so an enum/combo selection can never reach compute. ExecutionControlWidget's policy/edge combos work
but via direct node-state setters (setUpdatePolicy/setTriggerEdge), not the per-port path.
FIX:
1.1 NodeDelegate::populateEmbeddedWidget: SEED the port literal from the widget's value at MOUNT (synchronous,
    so it sidesteps the deferred NodeEditQueue and is live before the first drain) for float/int/bool/vec3; a
    wire still overrides (getInput checks packet first). The displayed default value is now the unconnected
    input. (Add a blank now outputs 0+0=0, intended.)
1.2 Generic ENUM-COMBO input: Port gains enumOptions + Node::addEnumInputPort(name, options); NodeDelegate
    builds a QComboBox (tagged krs_input_port) for type.name=="enum", seeds the literal (int index) at mount,
    and on currentIndexChanged sets the port literal -> compute reads getInput<int>(port). New demo node
    "math_op" (A,B + Op enum Add/Sub/Mul/Div) exercises it (also a useful node).
GATES (KRS_WIDGETINPUT_SELFTEST + bench), each with the OLD broken behavior as a REAL failing neg-control:
- GATE WIDGET-INPUT: mount math_add, type A=3 B=4 (unconnected) -> Result 7; an UNTOUCHED mount -> literal
  seeded, Result 0 (0+0) with inputs NOT nullopt; wire 10 into B (packet) -> A(3)+10 = 13 (wire overrides
  widget). NEG-CTRL: a RAW math_add (no delegate mount -> no seeded literal = the OLD behavior) -> no output
  (nan), which DIFFERS from the fixed 0 -> the gate catches a regression to spin-box-ignored.
- GATE COMBO-INPUT: mount math_op, the Op combo is a mounted QComboBox; selecting Add vs Multiply changes the
  Result (selection reaches compute via getInput<int>). NEG-CTRL: an UNBOUND bare combo / unset enum literal
  leaves the op on its default -> selection ignored, differs from the driven case.

### PHASE 2 — BUTTON / TRIGGER NODE (the event source)
New button_trigger node (src/Nodes/ButtonNode.cpp): a large colored clickable QPushButton that emits a BRIEF
Trigger pulse (bool true for exactly one eval tick) on the selected edge. Edge mode is an in-node ENUM combo
(the Phase-1 combo path): Rising fires on press, Falling on release, Dual on both. The button press/release set
the "pressed" param; compute() detects the edge vs the previous tick (m_lastPressed) and emits the momentary
Trigger; the eval timer (per frame) drives the polling. needsExecutionControls=false (it IS the trigger source).
GATE TRIGGER-EDGE (KRS_TRIGGEREDGE_SELFTEST + bench), measured + real failing neg-ctrl: over the sequence
idle/PRESS/hold/RELEASE/idle, Rising fires exactly once at the PRESS tick (and NOT while held -> brief), Falling
once at the RELEASE tick, Dual at both. NEG-CTRL: a fires-continuously-while-held (level) model fires on every
held tick (2) != the 1 edge fire; rising vs falling fire at different ticks (wrong-edge timing fails).
OPERATOR VISUAL-CONFIRM REQUIRED: instance the Button node -> a large red button that lightens when pressed +
an Edge combo; clicking it pulses downstream (e.g. drives the IK sample / OMPL plan trigger).

### PHASE 3 — IK TARGET NODE (sample a target pose on a trigger edge, solve IK, freeze)
New ik_target node (src/Nodes/IKTargetNode.cpp). Inputs Position (vec3) + Orientation (vec3 rpy, ZYX) + the
base Trigger. Outputs TargetX/Y/Z (double), Reachable (bool), Goal (joint_config = Eigen::VectorXd through
std::any). RECON: the obvious "detect the edge inside compute()" approach FAILS because the base process()
treats the inherited "Trigger" port as a HOLD in the default Async policy (Trigger high -> process() returns
WITHOUT calling compute()), so a high trigger SKIPS the sample. FIX (architectural): drive the node with the
framework's TRIGGERED update policy (setUpdatePolicy(Triggered) + setTriggerEdge(Rising)) so process() fires
compute() ONLY on the Trigger rising edge -- that IS sample-and-hold: each edge SAMPLES the live inputs and
solves DLS IK (SerialChain::ik) against the shared default arm; between edges compute() is not called so the
outputs HOLD the last sample. needsExecutionControls=false (policy is fixed to Triggered). The default arm is a
shared builder NodePlannerArm.hpp (buildDefaultArm: 3R arm + capsules + floor/box collision world + limits) so
the IK + OMPL nodes are functional/gateable now; wiring the live FANUC chain is a later integration step.
GATE IK-SAMPLE + IK-VALID (KRS_IKSAMPLE_SELFTEST + bench), measured + real failing neg-ctrls:
- SAMPLE-ON-TRIGGER: target = FK of a real config (so IK must converge). trig -> sample matches to <1e-6; move
  the Position input with NO trigger edge -> output UNCHANGED (held); a continuous-tracking model would output
  the live input here (differs) = the neg-ctrl; trig again -> output SNAPS to the new input.
- IK-VALID: Reachable true and FK(Goal) == target to <1e-3; an unreachable target (5,5,5) -> Reachable false
  (graceful, not a garbage config). NEG-CTRL: a WRONG solution (Goal + a joint offset) has FK far from target.
OPERATOR VISUAL-CONFIRM REQUIRED: wire Button.Trigger -> IK.Trigger; the IK Target output freezes and only
updates on each button press (snaps to the current Position/Orientation), holding between presses.

### PHASE 4 — OMPL PLANNER NODE (two-stage PLAN / EXECUTE, in-node planner params)
New ompl_planner node (src/Nodes/OMPLPlannerNode.cpp): a physically LARGE node wrapping krs::plan::MotionPlanner
with TWO DISTINCT trigger stages. Inputs Start + Goal (joint_config; Goal from the IK node), Plan + Execute
(bool triggers), and IN-NODE param widgets via the Phase-1 literal path: a Planner enum combo (RRT-Connect /
RRT*), a Max Iters spin (planning budget) and a Waypoints spin (path resolution) -- each also a wireable port.
Outputs Command (joint_config, drives the robot) + Planned/Executing/Progress/PathLen/Iterations. It needs TWO
triggers so the single-Trigger Triggered policy can't carry it -> it stays on Async (base Trigger left low so
compute() runs every tick) and detects the Plan/Execute rising edges itself. PLAN runs the planner from Start to
Goal and FREEZES the collision-checked path WITHOUT moving the robot (Command holds at Start). EXECUTE steps the
Command along the frozen path one slice per tick to the Goal. With no successful plan yet the Command tracks the
live Start config; a no-plan EXECUTE is a graceful no-op. Big blue PLAN + green EXECUTE buttons post one-shot
param pulses folded into the same edges.
RECON CATCH (gate-found): NodePlannerArm's floor half-space was at z=-0.05, but the base-post capsule (radius
0.06, foot at world z=0) reaches z=-0.06, so EVERY config penetrated the floor by 0.01 m -> nothing was ever
valid and the planner solved nothing. Fix: floor to z=-0.2 (0.14 m margin). (No effect on the IK gate, which is
collision-free FK/IK.)
GATE OMPL (KRS_OMPL_SELFTEST + bench), three sub-gates with real failing neg-ctrls:
- OMPL-TWO-STAGE: after PLAN (no Execute) the path advances ~2.9 rad yet Command == Start (robot did NOT move) ->
  a single-trigger plan-and-immediately-execute model would have moved (the path is non-vacuous); EXECUTE then
  drives Command to the Goal (Progress -> 1); EXECUTE with no prior plan leaves Command at Start (graceful).
- OMPL-PARAMS-IN-NODE: RRT-Connect vs RRT* give different path length (len 7.78 vs 3.30); Waypoints 24 vs 160
  change the path size; Max Iters (RRT*) 1500 vs 9000 refine the length -- a model ignoring the widgets returns
  identical plans (every "differ" check fails) = the neg-ctrl.
- OMPL-PLAN-VALID: the planned path is collision-free (independent re-check, pen 0) + respects position limits;
  the naive straight line Start->Goal collides with the box obstacle (pen 0.05) = the neg-ctrl.
OPERATOR VISUAL-CONFIRM REQUIRED: instance the OMPL node (large, PLAN+EXECUTE buttons + planner/iters/waypoints
widgets); press PLAN -> a path is computed and the robot stays put; press EXECUTE -> the robot drives along it.

### SPRINT RESULT (2026-06-18, branch avoidance-field) — ALL PASS, bench 99/99 gate groups
Phases 1-4 each built via PowerShell (freshly relinked, exe timestamp verified) and gated green:
KRS_WIDGETINPUT / KRS_COMBOINPUT / KRS_TRIGGEREDGE / KRS_IKSAMPLE / KRS_OMPL all ALL PASS; folded into
KRS_OVERNIGHT_BENCH -> 99/99 gate groups PASS (97 prior + IK-SAMPLE + OMPL). Commits: Phase 1 ea1893b, Phase 2
33ace7c, Phase 3 bf98feb, Phase 4 (this).

### UI BUGFIX — node combo popups + velocity-probe orb gizmo-scaling (2026-06-18)
Two operator-reported bugs, diagnosed by a 4-agent parallel recon workflow (each root cause cross-checked against
independent inline reads), then fixed + gated.

(1) NODE COMBO BOXES DEAD (the OMPL planner-mode combo, the Button edge combo, and the exec-mode
"Continuous/Triggered" + edge combos): clicking showed the flat-hand pan cursor and no menu, while spin boxes
worked. ROOT CAUSE: a node's body is one QGraphicsProxyWidget; the default QComboBox::showPopup() spawns its
dropdown as a separate top-level QWidget whose geometry is computed from the (meaningless) native screen rect
under the QGraphicsView transform, so it mis-positions / closes instantly. (The prior task-#82 fix applied the
QMenu workaround ONLY to the special Object/Property combos in TwinNodes.cpp; the generic combos never got it.)
Proof it is the popup, not the pan: the prior probe logs show the proxy WAS detected and the pan WAS suppressed
(dragMode 1->0) yet the native popup still failed. FIX: a shared ProxyComboBox (include/NodeHeaders/
ProxyComboBox.hpp) that overrides showPopup() to run a QMenu at mapToGlobal(0,height()) -- the proven
ObjectCombo/PropertyCombo idiom -- used in NodeDelegate's enum-input branch AND ExecutionControlWidget's
policy/edge combos. A QMenu action calls setCurrentIndex(i), so the existing currentIndexChanged ->
setPortLiteral<int> binding is untouched. No vendored-QtNodes edit, no DroppableGraphicsView change.
GATE COMBO-POPUP (KRS_COMBOPOPUP_SELFTEST + bench): the math_op Op combo, MOUNTED via the real NodeDelegate, is a
ProxyComboBox (dynamic_cast); showPopup() builds a QMenu child whose action count == item count; triggering an
action drives currentIndex. NEG-CTRL: a bare QComboBox (the old mount) is not a ProxyComboBox and builds no QMenu
(the gate fails on the old code). OPERATOR VISUAL-CONFIRM: click any node combo -> a menu drops under the box and
a selection takes, at 1.0 and zoomed.

(2) VELOCITY-PROBE ORB itty-bitty + ignores the scale gizmo. The gizmo is FINE (it writes t.scale = t0.scale*s
and the orb is pickable via cpuPickAABB with no collider). ROOT CAUSE: VelocityProbeOrbNode::compute() wrote
xf->scale = vec3(radius) EVERY eval tick, clobbering any gizmo scale on the next tick; the spawn radius 0.25 *
the IcoSphere base-mesh radius 0.5 = 0.125 m world radius (tiny); and the GL query used radius = scale.x while the
visible sphere is 0.5*scale.x (a 2x sampled-vs-seen mismatch). FIX: (a) compute() OWNERSHIP RECONCILIATION --
write scale only when Radius is WIRED (graph wins) or the in-node widget literal changed (operator typed);
otherwise READ xf->scale.x back so a gizmo resize PERSISTS and feeds the outputs (tracks m_lastWidgetRadius to
tell a widget edit from a gizmo edit; first tick reads the scale so a 0-seeded literal can't shrink it). (b) spawn
default 0.25->0.5 (world radius 0.25 m) + seed the Radius port literal so the widget shows the live size. (c)
OrbProbeSystem query radius = 0.5*scale.x so the sampled volume matches the visible glass sphere -- RUNTIME ONLY,
NOT krs::orb::averageVelocityInSphere (the ORB-VELOCITY gate asserts exact counts against the unscaled core).
GATE ORB-OWNERSHIP (KRS_ORBOWN_SELFTEST + bench, headless on a real entt registry, drives the real compute()):
a settled scale holds; a simulated gizmo write of 2.0 PERSISTS (param tracks it) [NEG-CTRL: the old clobber would
reset it to the 0.5 param -> differs]; a widget literal of 0.75 applies; a wired 1.25 overrides; releasing the
wire returns control to the gizmo (1.8 read back). OPERATOR VISUAL-CONFIRM: spawn a probe (~0.5 m glass sphere,
not itty-bitty); press Y/Space -> Scale, drag a handle -> the sphere resizes and STAYS resized across ticks, and
the contained Count tracks the visible volume; wiring Radius overrides the gizmo.

RESULT: KRS_ORBOWN + KRS_COMBOPOPUP ALL PASS; standalone re-runs green. Bench 101 gate groups, 100 PASS with the
sole red = GATE PERF, a pre-existing small-N (N=5) eval-timing flake (8.0x vs the >=10x threshold under multi-gate
load) that passes 3/3 standalone and is untouched by these UI/orb changes.
