# KRStudio Roadmap — toward a state-of-the-art robot-training & rendering engine

*Living document. Created 2026-06-12 from a deep-research pass (fluid solvers, smoke,
USD, Isaac-class RL infrastructure); update at the end of each round.*

**Where we are:** Qt6 / OpenGL 4.5 compute / EnTT / PhysX 5.5 (vcpkg, GPU DLL present).
GPU PBF fluid (200k particles, real SI units) with screen-space surface rendering
(anisotropic ellipsoid splats, two-interface refraction, Ihmsen whitewater, HDR/ACES),
CPU SPlisHSPlasH DFSPH reference tier, OpenVDB SDF baking + hero-still particle
meshing, auto exact-shape collision, analytic physics benchmark suite (KRS_BENCH),
texture/asset browsers, mesh-native materials.

**Where we're going:** an MQTT-connected robot-training engine that imports USD
scenes and trains policies Isaac-Sim-style, with film-grade fluids and smoke.

---

## A) Next fluid solver: GPU MLS-MPM (decision made)

| Option | Verdict |
|---|---|
| **GPU MLS-MPM** | **Build next.** ~1M particles real-time on a 4080-class GPU (1.33M @ 68 fps on V100, arXiv:2111.00699). Water + sand + snow + viscous + elastoplastic in ONE framework, and it deletes the neighbour search entirely. 2–4 weeks to first water. |
| GPU DFSPH | Skip — costs more (persistent neighbour lists, two iterative loops) and yields less (water only, ~20 steps/s @ 1.2M on a 3090). |
| FLIP/APIC | Defer post-Vulkan. MPM's APIC transfer code is ~80 % reusable if we go there. |
| Neural surrogates | Nothing production-ready for liquids; re-evaluate in 12 months. |

Implementation sketch: 3 GLSL compute kernels (P2G with int32 fixed-point atomicAdd —
the proven WebGPU-Ocean pattern — grid update, G2P), APIC transfers, dense grid first,
claymore-style block-sparse past ~2M particles. CFL substeps 2–10/frame. Start from
nialltl/incremental_mpm + taichi_mpm's 88-line core. Tiers: 780M ~100–150k, 4080 0.5–1M+.
Keep the PBF tier shipping until MPM water reaches parity; keep CPU DFSPH as offline
ground truth (enable its Jeske-2023 implicit surface tension).

## B) Smoke milestone 1 (the "next frontier")

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
