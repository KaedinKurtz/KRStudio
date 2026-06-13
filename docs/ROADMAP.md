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
