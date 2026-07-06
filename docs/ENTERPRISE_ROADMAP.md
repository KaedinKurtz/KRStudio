# KRStudio — Enterprise Research Platform: Architecture Constitution & Long Roadmap

*Written 2026-07-05 by the architecting session (Fable). Audience: the coding agents (and humans)
who build everything after this. Read Part 0 and Part IV before touching anything. This document
is versioned with the code — update it when you land an epoch item.*

---

# Part 0 — How to work on this codebase (THE CONSTITUTION)

These rules are not style preferences. Each one exists because its violation already caused a
multi-hour debugging session. Breaking them is how this codebase dies.

## 0.1 The Gate Discipline (non-negotiable)

1. **Nothing ships without a gate.** A gate is a headless self-test: `KRS_<NAME>_SELFTEST` env →
   `run<Name>Gate()` → printed PASS/FAIL rows with MEASURED NUMBERS → `std::_Exit(fails)`.
   Hooks live in `src/Rendering/RenderingSystem.cpp` (~line 1280+, grep `KRS_MEASURE_SELFTEST`
   for the pattern). Every gate is ALSO a row in the `KRS_OVERNIGHT_BENCH` dashboard
   (same file, grep `GateRes g[]`).
2. **Gates print measurements, not adjectives.** `area=6.000000000 (want 6.0)` — never "looks ok".
   Every gate needs NEG-CTRLs: inputs that MUST fail, proving the test can detect the bug it
   guards against. A gate without a negative control is a demo.
3. **Honest failure over fabricated success.** If a value can't be computed, return
   `ok=false`/empty — never a plausible number. (The Measure tool reports tessellated area as an
   *inscribed under-estimate* and says so. The grasp solver returns an EMPTY list when jaws can't
   open wide enough. Copy that spirit.)
4. **Tri-state SKIP** for environmental prerequisites (`include/UtilityHeaders/GateOutcome.hpp`,
   `krs::gate::skip()`): a missing MQTT broker or compiled-out Python is SKIP, never FAIL, never
   silent PASS.
5. **GUI-path gates** exist too (grep `KRS_RIBBON_SELFTEST`, `KRS_SNAPUI_SELFTEST`,
   `KRS_PICK_SELFTEST` in `src/UI/MainWindow.cpp`): QTimer-sequenced, drive REAL QMouseEvents via
   `QApplication::sendEvent`, assert ctx state + pixel-sample `grab()` images, `_Exit(fails)`.
   Use these when the thing under test IS the interaction.

## 0.2 The Parallel-Agent Pattern (how big features get built here)

Proven repeatedly (constraints, edges, snap, .kee, grasp, Python — all built this way):

1. The architect FIXES THE CONTRACTS FIRST in shared headers (structs, enums, function
   signatures, field conventions documented in comments).
2. Background agents own DISJOINT NEW FILES ONLY (one module + its gate each). They never touch
   shared files. They obj-compile their production files via ninja targets and run their gate
   through a scratchpad harness (production `.obj` + `Scene.cpp.obj` + `Qt6Core.lib` + a 4-line
   `main`). Read `src/Physics/Measure.cpp`'s history or any gate harness for the technique.
3. The integrator (you, main session) wires gate hooks, bench rows, UI, and all edits to shared
   files (`components.hpp`, `RenderingSystem.cpp`, `MainWindow.cpp`, `ViewportWidget.cpp`,
   `SelectionService.hpp`), then runs the full battery + bench before committing.
4. **PIN EVERY AGENT to `D:\RoboticsSoftware` (main checkout, branch `avoidance-field`)** — the
   session worktrees are STALE and reading them corrupts plans.

## 0.3 Build & Environment (this machine)

- Windows 11, VS18 Community, CMake+Ninja, vcpkg manifest. Every build shell needs:
  `vcvars64.bat` → `set VCPKG_ROOT=C:\Users\kaedi\vcpkg` →
  `set VCPKG_VISUAL_STUDIO_PATH=C:\Program Files\Microsoft Visual Studio\18\Community`.
- Build via `cmd.exe /c <bat>` from PowerShell (MSYS bash mangles `/c`). Verify EVERY build by
  exe `LastWriteTime` + `BUILD_EXIT=0` in the log — a background build once silently no-op'd.
- **The user runs the RELEASE build** (`build/release`) — it has the deployed assets. Rebuild
  Release on every commit. Debug (`build/debug`) is for CRT asserts.
- Kill `RoboticsSoftware.exe` before linking (LNK1168). NEVER run launch LOOPS with the Focusrite
  USB interface plugged in (kernel driver BSODs the machine — documented in memory).
- Wide-header edits (Eigen-heavy) can OOM the compiler at full parallelism: `-j 2`/`-j 3`.
- New shader files: the POST_BUILD `copy_directory` misses them — manually copy to
  `build/{release,debug}/shaders/` once.
- A hung headless gate in debug = an INVISIBLE CRT assert dialog. Diagnose with
  `Get-Process` MainWindowTitle == "Microsoft Visual C++ Runtime Library", read it via
  EnumWindows/GetWindowText (script in memory). `KRS_ASSERT_DIALOG=1` restores dialogs.
- vcpkg manifest changes trigger long bakes (python3 ≈ 30 min) and take a global file lock —
  coordinate; never run two configures at once.
- PowerShell here-string commit messages DIE on embedded double-quotes. Keep messages quote-free.

## 0.4 Data & Identity Invariants (violate these and saved documents rot)

- **Durable geometry identity**: `BRepFace.faceKey`, `BRepEdge.edgeKey`, `BRepVertex.vertexKey` —
  geometry-invariant FNV-1a hashes over PART-LOCAL params with a POSITION channel
  (`components.hpp::computeFaceKey` is the reference). Anything that must survive re-import
  anchors by KEY, with array indices only as validated hints. NEVER anchor by index alone.
- **Outward normals**: `BRepFace.normal` points OUT of the material (flip on
  `TopAbs_REVERSED` XOR `!pl.Direct()`). Three extraction sites enforce this; a fourth must too.
- **Body-local frames**: MateConnector/Anchor/TCP frames are stored BODY-LOCAL and re-derived
  through the CURRENT transform. World-anchored frames are the original sin (see the mate
  fragility decision doc).
- **IDs are monotonic and never reused** (`nextConnectorId`, `nextMateId`, joint ids, group ids).
- **Documents are versioned (`format: "x/1"`), content-hashed, refused on wrong MAJOR, and
  tamper = detect + warn + honor** (ksave contract). Corrupt input NEVER crashes a loader.
- **`LiveRobot` owns q.** The node graph, PhysX, RobotGraph, panels are peripherals that read or
  command through the per-eval-pass command bus (assert-per-pass, never latch).
- **The selection contract** (post-2026-07-05): ambient always-on; plain click = replace-select
  (re-click toggles), Shift = add; commits are WYSIWYG from the GPU pick; anchors = active snap
  candidate else click surface point; committed visual = the quadrant glyph. Modes
  (bore-quota, measure) LAYER on top and end THEMSELVES without disabling selection.

## 0.5 Library Policy

Permissive only: MIT / BSD / Apache-2.0 / MPL-2.0 (file-level) / LGPL **dynamic-link only, with
the relink obligation satisfied** (OCCT's LGPL+exception is fine as used). **NO GPL static
inclusions** (rules out QScintilla, SOEM without care, CGAL, solvespace, planegcs). Every new
dependency enters through `vcpkg.json` + a `KR_WITH_*` CMake option with a clean compile-out path
and a tri-state SKIP gate.

---

# Part I — System Map (where everything lives)

| Subsystem | Namespace / Files | Gate(s) | Notes |
|---|---|---|---|
| Scene/ECS | `Scene`, entt registry, `components.hpp` | (many) | ctx singletons listed in Part V |
| Rendering | `RenderingSystem` + pass objects (`src/Rendering/*Pass.cpp`) | RENDER G1-G9, FANUC V2 | deferred GL 4.3, engine context offscreen, per-viewport TargetFBOs |
| GPU picking | pick pass in `RenderingSystem` (`renderPickBuffer`), `pick_id_*.glsl` | PICK 4/4 | RG32UI ids; faces via gl_PrimitiveID+triFace TBO; edges/verts instanced quads; double-PBO readback; vertex>edge>face bias |
| Selection | `krs::sel` (`SelectionService.hpp`, `EdgeSelect.hpp`) | HIGHLIGHT-MATCHES, INDICATOR, MULTI-SELECT, EDGE 19/19 | Selection = the pick currency; ambient contract §0.4 |
| Snap/inference | `krs::snap` (`Snap.hpp`), `krs::snapui` (`SnapSession.hpp`), `SnapOverlayPass` | SNAP 37/37, SNAPUI 6/6 | Onshape candidate spec; Fusion glyphs; F/R corrections |
| Highlights | `SelectionHighlightPass` (+`highlight_id.glsl`) | (visual, via SNAPUI grabs) | fill+contour from ID discontinuity; quadrant markers |
| Measure | `krs::measure` (`Measure.hpp`), `MeasureHud` | MEASURE 23/23 | SI internally; `units/length` registry setting is THE unit source |
| B-Rep import | `CadImporter.cpp` (faces/edges/vertices, keys), `RobotBuilder.cpp` | PARSE-RECON, AUTO-PARSE, UV gates | OCCT; world-baked meshes; part-local key space |
| Constraints | `krs::constraint` (`Constraint.hpp/cpp`), `ConstraintsPanel`, `ConstraintIconOverlay` | CONSTRAINT 28/28 | 14 CTypes; PxD6 mapping Z→twist-X in `SimulationController::buildConstraintJoints` |
| Mates/joints | `krs::rbuild` RobotGraph/MateConnector, RobotBuilderPanel | JOINT-AUTHORING suite, MATE-SNAP, etc. | connector = body-local + faceKey |
| Robots | `krs::robot` LiveRobot/RobotRegistry | ROBOTOWNER, MANIP-OPS, IK-POSE, CUT-KEEPS-DRIVABLE | q ownership §0.4 |
| Physics | `SimulationController` (PhysX pimpl) | LIFECYCLE, C3, KRS_BENCH 7/7 | fixed-dt 240Hz, m_timeScale, GPU tier gate |
| Fluids/MPM/FEM | `krs::fluid/mpm/fem` | conservation + fidelity suites | GPU compute |
| Node graph | `krs::nodes` + QtNodes, `NodeFactory` | ~40 node gates | isPureInputFunction contract §Part IV |
| Persistence | `krs::ksave` (.kscene/.krobot/.kjoint/.kstate), `krs::kgraph`, `krs::kpack`, `krs::kdoc` | KSAVE, SCENESAVE, KGRAPH, KPACK, KDOC | hot-reload snapshot on close |
| Skills/goals | `krs::skill/world/goal/hone` | SKILL→GOALLOOP chain | SkillRuntime pumped per eval pass |
| EE stack | `krs::kee` (.kee), `FaceRole.hpp`, `krs::graspq`, `EffectorStudioPanel`, `krs::ee::AttachedEffectors` | KEE, GRASPQ 24/24 | attach-by-connector-id |
| Python | `krs::py` (`PyScript.hpp/cpp`), pybind11+vcpkg python3 | PY 11/11 (tri-state) | main-thread only; per-run isolated globals |
| Sensors | `krs::sensor` synthetic suite, RealSense mgr | SENSOR gates 0..E2E | L1/L2/L3 composition |
| Grasp research | `krs::grasp` (YCB/CoACD/planner/criterion) | 8 GRASP gates | the evaluation harness for graspq solvers |
| RL | `krs::rl` | RLENV | gym-style residual env |
| Data | `krs::klut/rec/datanodes/act/ktp` | KLUT/REC/DATANODES/ACTUATOR/TELEMETRY | .klut/.rec/.kactuator |
| UI shell | `MainWindow`, `StaticToolbar` (action bus), docks, `OutlinerWidget`, groups (`GroupOps.hpp`) | RIBBON 16/16, TOOLBAR, GROUP 12/12, VPREVIVE | dispatcher pattern; panel toggles; hot reload |

**The overnight bench** is the platform's heartbeat: 178 gate groups, currently 175 PASS / 3
env-SKIP / 0 FAIL. **A PR that turns any row red does not merge.** Adding a feature = adding rows.

---

# Part II — THE ROADMAP (Epochs E1–E10)

Ordering encodes dependencies. Within an epoch, items marked ∥ are agent-parallelizable under
§0.2. Effort: S (<1 day-agent), M (1–3), L (3–7), XL (multi-week, decompose further).

---

## EPOCH 1 — Debt Retirement & Platform Hardening (do FIRST, everything stands on it)

### E1.1 Persistence Unification (L) — **STATUS 2026-07-05: DONE (kscene/1.2)**
LANDED (KSave.cpp 1275→2187 lines; KSave.hpp untouched): `.kscene` v1.2 with
`PersistentIdComponent{pid}` (components.hpp) + doc-level `nextPid` (monotonic PidAllocator ctx,
never rewinds/reuses); objects/lights/group-roots mint pids at save; robot MEMBERS never get
pids (addressed as {robot, body, slot, link} index-hint + name-fallback refs). New sections:
`groups` (nesting reconstructs from memberPids), `faceRoles`, `connectors` (shared
connectorSetToJson/FromJson used by BOTH .krobot and loose paths so they cannot drift),
`constraints` (Constraint.hpp toJson wrapped with bodyPidA/B), `attachments`, plus per-object
`brepFaces` — LOAD-BEARING addition: anchorWorldFrame refuses keyed anchors whose faceKey is
absent, and spawnPrimitive rebuilds loose bodies faceless, so keyed relations on loose bodies
would strand without persisted faces (keys stored verbatim, not re-minted). TWO-PASS load:
pass 1 spawns + adopts pids into pidMap; pass 2 resolves all relations through pidMap; missing
pid = DROP with a warning naming the relation; stale faceKey = KEEP + warn (a re-import can
re-anchor; dropping would destroy authored intent); NO re-snap on load (validate only, zero
body motion <1e-6). Relations referencing unsaved bodies drop AT SAVE with named warnings.
GATE (extended SCENESAVE, in-app verified): pids stable across re-save, 2 constraints
(1 kinematic) anchor-resolve with zero load motion, nested groups fan out, roles/connectors
exact, attachment intact; NEG-CTRLs: deleted body drops both constraints BY NAME, corrupt pid
drops 1/1 named, v1.1 scenes load clean with 0 warnings. REMAINING (E1.3 territory): mesh-asset
loose bodies still don't respawn on load; legacy MateGraphComponent mates + snap prefs not
persisted (deliberate punts).

### E1.2 System-wide Undo/Redo (XL — decompose) 
Today undo = gizmo transforms only. An enterprise tool needs command-pattern undo everywhere.
- Design: `krs::cmd::Command { do(), undo(), merge(), text() }` + `CommandStack` ctx singleton
  (cap 200, memory-bounded). Wrap: transform edits (absorb gizmo stack), spawn/delete,
  group/ungroup, constraint add/remove/suppress, role paint, connector place, joint define/cut,
  material apply, graph node add/remove (QtNodes has its own — bridge it).
- ∥ Agent-split: (a) the core stack + transform/spawn/delete commands + gate; (b) constraint/
  mate/role commands; (c) UI wiring (Ctrl+Z global, Edit menu, ribbon undo/redo buttons already
  exist in the action bus).
- GATE `KRS_UNDO_SELFTEST`: scripted sequence of 12 mixed operations → undo all → registry
  state fingerprint IDENTICAL to start (entity count, component checksums) → redo all →
  identical to end. NEG-CTRL: undo past empty is a safe no-op. Threshold: fingerprint match
  must be EXACT, not approximate.

### E1.3 `.kee`/`.krobot` mesh-source refs (M)
Loaded documents can't rebuild visual geometry (bodies persist name/visSize/placement only).
Add `sourceRef { kind: step|mesh|primitive, path (relative-to-doc + assetDir fallback),
partName }` per body; loader reconstructs via CadImporter (STEP, per-part) or ResourceManager.
GATE: save a STEP-authored gripper .kee → load in a FRESH scene → bodies render with real
geometry, faceKeys MATCH the originals (the key-space discipline makes this testable — assert
role/connector re-anchor by key finds 100% of entries). SKIP honestly when the source file is
missing (placeholder boxes + warning).

### E1.4 Script node → Python bridge (S)
`util_script` (CustomScriptNode, compute is a stub) becomes the Python node: script text edits
in the node UI, compiled via `krs::py::runScript` per eval with inputs bound as a `krs.inputs`
dict and outputs read from `krs.outputs`. Respect `isPureInputFunction()=false` (already set).
GATE `KRS_PYNODE_SELFTEST`: a node computing `out = in_a * 2 + 1` through REAL Python matches
closed form; a script exception paints the node error state and does NOT stall the eval loop
(assert eval tick rate unchanged within 10% — the THREAD gate technique).

### E1.5 Selector polish backlog (S each, ∥)
(a) In-canvas flip-Z / rotate-90 buttons at the active snap candidate (icon-overlay pattern from
`ConstraintIconOverlay`) replacing keyboard-only F/R. (b) Fusion two-stage pick: click a bare
face → face becomes scope, dots persist until next click. (c) Cone apex: add `halfAngleRad` to
`BRepFace` (extraction from `gp_Cone::SemiAngle()`), replace the SnapEngine apex fallback; keys
UNAFFECTED (verify: hemisphere-fold + no new key channels). (d) True inner-wire walk for
InnerWireCentroid. (e) Role viz: tint GripSurface faces green/KeepOut red via the existing
`FaceMaterialComponent` overlay machinery, toggled from Effector Studio. Each lands with a gate
row extension (SNAP gate grows checks; no new hooks needed).

### E1.6 Crash & quality telemetry (M)
- crashpad (BSD) or sentry-native (MIT) for minidump capture with symbol upload; keep PDBs per
  release build (symbol store dir).
- Structured logging: replace scattered qDebug with spdlog (MIT) categories + rotating file in
  appdata; a `Log` dock panel tailing it (the Diagnostics panel grows a tab).
- GATE: none (infrastructure) — but the bench gains a LEAK row: run 3 boot/load/close cycles
  under `_CrtDumpMemoryLeaks`/heap snapshots; threshold: zero growth > 1 MB between cycles 2→3.

### E1.7 CI (M)
GitHub Actions (or local runner given repo size): configure + build Release + run the
**CPU-only gate subset** (everything not needing GL/GPU — tag gates `[gpu]` in the bench table
and add `KRS_BENCH_CPU_ONLY=1` filtering). GL rows run nightly on a self-hosted runner with the
RTX box, or under Mesa llvmpipe (accept 10× slower, mark timing-sensitive rows SKIP). Merge rule:
CPU subset green mandatory; nightly full-bench red pages the roadmap owner.

---

## EPOCH 2 — Robot Authoring Completion (the CAD promise)

### E2.1 STEPCAF Phase-4: true 6-DoF FANUC re-import (L) — **STATUS 2026-07-05: substantially DONE**
RECON CORRECTION (verified in code, do not re-plan from the old premise): the DEFAULT app boot
is ALREADY the STEPCAF 6-DoF path — `MainWindow` (≈line 908) does `importStepAssembly` →
`krs::rbuild::buildNamedSerialChain(parts)` (name-driven base→j1..j6 grouping, axes from best
coaxial bore per link pair, J1 verticality prior) → authoring overlay → `instantiateFromGraph`
→ LiveRobot "FANUC-430" FK viz. The 4-joint `canonicalSpec` (J4 frozen) is ONLY the
`KRS_FANUC_LEGACY` fallback + the D/H/V/node gate rig spec.
LANDED (this session): `krs::rbuild::articSpecFromGraph(RobotGraph) -> krs::dyn::RobotArticSpec`
(RobotBuilder.hpp — world-aligned frames at each joint's axisPos, canonicalSpec's exact
convention, ambiguous joints truncate honestly with a report, limits carried, fixed joints fold)
so ANY authored graph can back a PhysX reduced-coordinate articulation; plus GATE
`KRS_FANUC6_SELFTEST` (`krs::fanuc::runFanuc6Gate`, FanucArticulation.cpp) measured on the REAL
STEP: dof==6 + zero ambiguous, J1 vertical >0.999, per-joint residual <=1.5e-2 (never the
residual-1.0 last-resort guess), FK(q=0)==parsed placements <1e-6, converter round-trip <1e-6,
PhysX articDofCount()==6 with J4-only motion (the dof the legacy cap froze) + 30-step zero-g
drift <5e-2; NEG-CTRLs: legacy spec stays 4-capped/frozen, ambiguous mid-chain joint truncates
spec to 3 + report. Bench row added next to AUTO-PARSE-CHAIN; SKIPs without OCCT or the asset.
REMAINING (deliberately deferred to E3): flipping the DEFAULT boot's physics from kinematic
follower actors to the graph-derived dynamic articulation — that is the controller-manager /
torque-control seam (E3.2), not an import problem. D/H/V and node gates keep their 4-DoF rig
spec on purpose (self-contained rigs). Mass/inertia for the converted spec are unit defaults
until E2.5 lands (articSpecFromGraph has a single injection point for it).

### E2.2 Mimic joints in the live model (M)
`.kee` actuation declares mimic; the live LiveRobot/PhysX must enforce `q_follower = ratio * q_drive`.
Implement at the command-bus application layer (after reading drive q, write follower) AND in
the PhysX articulation (fixed tendon or explicit per-step position write — start explicit).
GATE `KRS_MIMIC_SELFTEST`: a 2-jaw gripper graph with mimic -1 → drive to q=0.02 → follower at
-0.02 within 1e-6 in BOTH the kinematic model and a 2-second physics run (< 1e-4 there).

### E2.3 Attached-effector chain extension (L)
Today Attach rigid-snaps bodies + registers identity. Full integration: the attached effector's
joints become DRIVABLE THROUGH THE PARENT ROBOT's namespace (`ee2/jaw` in the joint registry),
its TCP becomes the robot's active TCP for IK (`ikDragEntity` targets the TCP frame, not the
flange), and detach restores. Skill layer: `withEffector(n)` resolves `AttachedEffectors`.
GATE `KRS_ATTACH_SELFTEST`: attach synthetic gripper to robot connector #2 → jaw drivable by
name through the bus; IK to a world pose lands the TCP (not the flange) within 1e-4; detach →
name unresolvable, robot IK reverts to flange; re-attach to connector #1 → frames correct.

### E2.4 Interference & clearance (M) ∥
`krs::clearance`: swept/static interference check between selected body sets using the existing
nanort BVHs (broadphase AABB → exact tri-tri, `Manifold` (Apache) optional for boolean volume of
intersection). Panel affordance: "Check interference" on selection; result list with volumes;
viewport tint. GATE: two overlapping boxes report intersection volume within 2% of analytic;
touching-not-overlapping reports zero; robot self-check at home config uses the SELFCOLLISION
matrix and reports only enabled pairs.

### E2.5 Mass properties & inertia authoring (M) ∥
Per-body mass/CoM/inertia from B-Rep volume (OCCT `BRepGProp::VolumeProperties`) × material
density (MaterialComponent gains density; library presets). Writes into RobotGraph → URDF export
gains real `<inertial>` (closing the "kinematics only" caveat). GATE: unit cube steel density →
mass 7850 kg ± 0.1%; inertia tensor matches closed form < 0.5%; URDF re-import round-trips.

### E2.6 Drawing-grade section views & exploded views (M, lower priority) ∥
Clip-plane section rendering (gbuffer discard + cap shading via stencil) with a section gizmo;
exploded-view slider driven by the assembly graph. OPERATOR-VISUAL-CONFIRM + a pixel gate
(section through the FANUC base shows interior pixels ≠ exterior color).

---

## EPOCH 3 — Motion & Control Core (from "moves" to "controls")

### E3.1 Trajectory time-parameterization (M)
Library: **Ruckig Community (MIT)** — jerk-limited online trajectory generation; vcpkg or vendored.
Wrap as `krs::traj::timeParameterize(path, limits) → sampled trajectory` + a node
(`traj_ruckig`). GATE `KRS_TRAJ_SELFTEST`: a 3-waypoint joint path with vel/acc/jerk limits →
output respects ALL limits (max observed within 0.1% of limit, never over), reaches waypoints
< 1e-6, monotone time; NEG-CTRL: infeasible (zero max vel) → honest failure. Benchmark row:
1000-waypoint path parameterized < 50 ms.

### E3.2 Cartesian planning + collision-aware IK (L)
Cartesian straight-line/arc segments with orientation interpolation (slerp), collision-checked
via the existing planning validity layer; IK seeded continuation along the path (track the
6-DoF pose IK already gated). GATE: a 20 cm straight-line EE move on the FANUC → max path
deviation < 1 mm at 100 samples; a path THROUGH an obstacle honestly fails with the colliding
segment named.

### E3.3 Controller manager (L) — the ros2_control shape without ROS
`krs::ctrl::ControllerManager` ctx: named controllers (position/velocity/effort/impedance) with
exclusive claims on joint groups, switchable at runtime, all writing through the per-pass bus.
The existing PID/computed-torque gates become controllers in the registry. Panel: controller
list + claim matrix. GATE `KRS_CTRLMGR_SELFTEST`: two controllers claiming the same joint →
second REFUSED; switch position→computed-torque mid-motion → no command discontinuity > 5%
of the running amplitude; E-STOP releases all claims (extend the ribbon E-STOP dispatcher).

### E3.4 Real-time loop separation (XL — design review with the human first)
Today the eval loop ticks in the GUI thread. Enterprise control needs a deterministic loop:
move sim stepping + bus application + controllers onto a dedicated thread (the 1 kHz HIL jitter
gate proves the machine can); GUI thread becomes a reader through triple-buffered state.
DANGER ZONE: every registry access must be audited (entt is not thread-safe). Strategy: a
command queue INTO the sim thread + a published immutable state snapshot OUT per tick.
GATE: jitter row (p99.9 < 1 ms at 1 kHz with GUI under load — drag a dock while it runs via
synthesized events); state snapshot reader never observes torn q vectors (checksum per snapshot).

### E3.5 Force/impedance interaction (L)
Impedance controller (stiffness/damping per Cartesian axis at the TCP) against PhysX contact:
the gripper "gives" on contact. Uses the 6-DoF FT assumption at the EE (the user's stated
hardware plan). GATE: press the EE into the ground plane with 500 N/m stiffness → steady-state
penetration force within 5% of k·x; NEG-CTRL: rigid position control shows >10× contact force.

---

## EPOCH 4 — Scripting & API Surface (the "script everything" mandate)

### E4.1 `krs` Python API v2 (L) ∥ (API design doc FIRST, then parallel binding agents)
Grow the bound module from read-only to full control (main-thread execution model stands):
`krs.spawn_primitive/spawn_mesh/import_step`, `krs.select/selection()`, `krs.robot(n)` handle:
`.q/.set_q/.ik(pose)/.plan(goal)/.execute(traj)`, `krs.sim.play/pause/step/state`,
`krs.scene.save/load`, `krs.constraints.add(...)`, `krs.kee.load/attach`,
`krs.graspq.solve(...)` + `krs.graspq.register_solver(fn)` (Python solvers enter the SAME
registry — the contract seam pays off), `krs.nodes.create/connect`, `krs.record/df` (recorder →
pandas via zero-copy numpy where possible). EVERY function: honest exceptions, no silent None.
GATE `KRS_PYAPI_SELFTEST` (script-driven): a 30-line script spawns a box, plans a robot to it,
solves a grasp, executes, asserts final EE-box distance < 2 cm — the full loop headless from
Python. Benchmark: `krs.faces()` on the FANUC (19 bodies) < 50 ms.

### E4.2 Script editor & runner panel (M)
QPlainTextEdit + custom Python syntax highlighter (NO QScintilla — GPL). Run button → runScript
with stdout/traceback into an output pane; scripts saved to appdata `scripts/` + a library list;
`.kscript` = script + manifest (name, params, requirements) packaged like `.knodepack`.
GUI gate: run `print(krs.bodies())` from the panel, assert output pane non-empty (SNAPUI style).

### E4.3 Async script execution (L — after E3.4 or carefully before)
Long-running scripts (training loops, sweeps) must not freeze the GUI: a worker interpreter
executing scripts that interact through the same command-queue seam as E3.4, with progress
callbacks and a hard Cancel (interpreter interrupt via `Py_AddPendingCall`). Until then, the
documented contract is main-thread cooperative only. GATE: a `for i in range(1e9)` script
cancels < 200 ms after Cancel with the GUI having remained responsive (event-loop latency probe
< 100 ms during the run).

### E4.4 Headless mode (M)
`RoboticsSoftware.exe --headless script.py`: offscreen boot (the gate infrastructure ALREADY
boots offscreen-ish), run script, exit with its code. This is the CI/vectorized-RL substrate.
GATE: the E4.1 script runs headless, exit 0, < 60 s.

---

## EPOCH 5 — External Interfaces (the "interface and control things around it" mandate)

*All protocol servers live behind `KR_WITH_*` flags + tri-state gates. Auth from day one: token
file in appdata; TLS via Qt's stack where applicable. Never bind non-localhost by default.*

### E5.1 gRPC remote API (L) ∥
Library: grpc + protobuf (Apache/BSD, vcpkg). Define `krs.proto`: SceneService (list/spawn/
transform), RobotService (state stream @ 100 Hz, command, plan/execute), SimService, DataService
(recorder streams). The Python client SDK (`pip install krstudio-client`) is GENERATED + a thin
ergonomic wrapper mirroring E4.1 names — one mental model, in-process or remote.
GATE `KRS_GRPC_SELFTEST` (tri-state): loopback client drives a joint, streams 100 state msgs
(measured rate 100 ± 5 Hz), plan+execute round-trip; unauthenticated call REFUSED.

### E5.2 ROS 2 bridge (L) ∥
Apache-2.0. Two options — decide by experiment: (a) native rclcpp (vcpkg ros2 is heavy on
Windows) or (b) **zenoh-based bridge** (zenoh-plugin-ros2dds, EPL/Apache) speaking to a ROS 2
graph without a local ROS install — RECOMMEND (b) first for Windows sanity. Publish
`/joint_states`, TF tree (the tf_tree_visualizer ribbon button awaits), sensor topics; subscribe
`/joint_trajectory`. GATE (tri-state, needs a peer): echo round-trip of joint states against a
recorded rosbag fixture; SKIP without peer.

### E5.3 OPC-UA server (M) ∥
Library: **open62541 (MPL-2.0)**. Expose robot state + sim state as an OPC-UA address space
(the industrial/enterprise checkbox). GATE tri-state with open62541's own test client:
browse finds `Robot0/q0..5`, subscription delivers 10 updates, write to a command node moves
the sim robot.

### E5.4 Fieldbus (investigate → M each) ∥
EtherCAT: **ethercrab (Rust, MIT)** via C FFI, or acontis licensing for enterprise — document
the SOEM GPL problem explicitly and DO NOT vendored-static it. CANopen: the existing
`krs::hil::IVirtualCAN` seam takes a real SocketCAN/PCAN backend (PCAN-Basic API is
redistributable-free). Modbus-TCP: libmodbus (LGPL-2.1, dynamic-link ok). Each: tri-state gate
against a software slave (e.g. a Modbus echo server fixture).

### E5.5 USD / MJCF / SDF export-import (M each) ∥
USD (OpenUSD, Apache — HEAVY; gate the vcpkg build cost first with a spike), MJCF export (MuJoCo
consumers; XML writer honest about what doesn't map, following the URDF export report pattern),
SDF import exists partially. GATE per format: FANUC export → re-import in the reference tool's
own parser (bundle mujoco (Apache) headless for validation) → DOF count + limits + inertials
match the source within tolerances.

---

## EPOCH 6 — Perception & World Understanding

### E6.1 Point-cloud pipeline (L) ∥
Library: **Open3D (MIT)** core (or PCL/BSD if Open3D's vcpkg story fights back). RealSense frames
→ `krs::pcl::Cloud` asset in-scene (renderable via the existing PointCloudPass) → ops as NODES:
voxel downsample, plane RANSAC extract, Euclidean cluster, ICP register. GATE: synthetic cloud
of a known box + plane → RANSAC recovers the plane normal < 0.5°, cluster count exact, ICP on a
5°-perturbed copy converges < 1 mm RMS.

### E6.2 RANSAC object recognition → WorldState (L)
The user's stated goal: recognize scene objects and bind knowledge. Match segmented clusters
against the scene's B-Rep bodies (or a .kee/asset library) via FPFH+RANSAC / ICP scoring; on
match, ASSERT pose + identity into `krs::world::WorldState` with provenance `Estimated` (never
Measured — honesty ladder). GATE: render a synthetic depth view of a known scene (the sensor
suite is gated for exactly this), recognize 3/3 objects, pose error < 5 mm / 2°; an object NOT
in the library is reported UNKNOWN, never force-matched (NEG-CTRL).

### E6.3 AprilTag + hand-eye calibration (M) ∥
apriltag (BSD). Detect in RealSense/synthetic frames; hand-eye (Tsai-Lenz) between EE and
camera using the robot's FK. GATE: synthetic camera on a known mount, 10 poses → recovered
extrinsic < 1 mm / 0.2° (reuse the IMU blind-recovery gate style: sealed truth, information
barrier).

### E6.4 Occupancy / TSDF fusion (M, after E6.1)
Voxel TSDF from depth streams (Open3D's integration or a small custom CUDA kernel — the GPU
infra exists); expose as a collision obstacle source for planning (planning-scene diff).
GATE: fuse 20 synthetic views of the box scene → mesh extraction error < 5 mm; the planner
refuses a path through the fused obstacle (chain with the PLAN gate harness).

---

## EPOCH 7 — Learning & Data at Scale

### E7.1 Gym bindings + vectorized headless envs (L)
`krs.rl.make_env(cfg)` from Python (gymnasium API, MIT); vectorization = N headless processes
(E4.4) with a shared-memory step protocol (the HIL frame-ring pattern generalizes). Benchmark
row: 8 parallel envs × 100 steps of the residual env < 30 s wall.

### E7.2 Domain randomization service (M)
The ribbon button exists, unwired. `krs::dr`: declarative randomization spec (JSON: material
albedo/roughness ranges, light EV, object pose jitter, physics params via the actuator model's
provenance-tracked params) applied per-episode. GATE: 100 sampled episodes → every randomized
parameter's histogram spans the declared range (KS-test sanity), determinism under fixed seed.

### E7.3 Dataset export & demonstrations (M) ∥
Recorder (.rec) → HDF5/Parquet export (HighFive BSD / Apache Arrow) with camera frames; a
"demonstrate" mode recording jog/gizmo teleop as trajectories; loader for LeRobot-style dataset
layout (document the mapping honestly). GATE: record 10 s of sine drive + camera @ 10 H z →
export → re-import → bit-exact joint series, frame count exact.

### E7.4 ONNX policy runtime (M)
onnxruntime (MIT, vcpkg). A policy asset (.onnx + IO spec) runs as: a NODE (obs in → action
out), a CONTROLLER (E3.3 registrant), or a GRASP SOLVER (graspq registrant — the seams all
converge here, by design). GATE: an identity-MLP onnx fixture round-trips obs→action < 1e-5;
inference < 2 ms at batch 1 on CPU.

---

## EPOCH 8 — Enterprise Shell

### E8.1 Project format `.kproj` (M)
One directory (or zip) bundling: .kscene + sibling graphs + .kee assets + scripts + roles +
a manifest with content hashes of members (the kpack pattern scaled up). Recent-projects UI,
relative paths throughout (the assetDir resolver generalizes). GATE: save project → move the
directory → open → zero broken refs; tamper a member → named warning.

### E8.2 Asset library & versioning (M) ∥
The Manufacturer Parts panel generalizes: user library roots (parts, .kee, materials, scripts,
strategies) with search + tags; every asset content-hashed; "newer version available" diffing
by hash chain. GATE: import a pack twice → deduped by hash; edit → new version listed, old
loadable.

### E8.3 Plugin SDK (L — design review first)
QPluginLoader-based: plugins register nodes, solvers, panels, importers through a versioned C++
ABI header (`krs_plugin_api.h`, semver-checked at load). The registries built this year
(NodeFactory, graspq solvers, controllers, ksave codecs) ARE the extension points — the SDK just
exposes them. GATE: a sample plugin (built in-tree as a separate target) loads, registers a
node + a solver, both function; ABI-mismatched plugin REFUSED with a clear message.

### E8.4 Installer, updates, licensing (M)
WiX (MS-RL is fine for the toolchain, not linked) or NSIS (zlib) installer bundling the Release
deploy tree (windeployqt output + python zip + PhysX GPU dlls — the deployment list is already
gated by experience); winsparkle (MIT) update checks; license-key gate if commercializing
(keep a `KR_ENTERPRISE` flag seam, decide policy later).

### E8.5 Workspace profiles & UX system (S–M each) ∥
Named layout profiles bound to workflows ("CAD", "Simulate", "Control", "Analyze") — the layout
slots system exists, add profile switching to the ribbon View tab; command palette (Ctrl+K,
fuzzy over the action bus ids + descriptions — the bus makes this trivial); first-run tour;
keyboard-map editor writing the existing shortcut table.

---

## EPOCH 9 — Simulation Fidelity & Sim2Real

### E9.1 Contact-rich manipulation validation (L)
The grasp SUCCESS criterion is gated; extend to in-hand rotation/slip under the LOCKED physics
config; friction-cone visualization at contacts. GATE: a grasped box under 2× gravity with
μ=0.3 SLIPS (measured displacement > threshold), holds at μ=0.8 — the criterion must
discriminate friction, not just closure.

### E9.2 Cable/hose simulation (L, research-grade) 
PhysX articulated chains or XPBD rods for dress-packs; attach ends to links. Gate on catenary
sag vs analytic for a hanging chain < 5%.

### E9.3 Sim2real calibration loops (L)
The hone machinery generalizes: excite a REAL robot (through E5 interfaces), record via the
telemetry stack, fit actuator-model params (friction/backlash/inertia — the .kactuator format
exists), and REPORT the residual honestly per joint. GATE (tri-state, hardware): synthetic
"real" fixture — a second simulated instance with perturbed params plays the real robot; the
loop recovers the perturbation within declared sigma (the information-barrier pattern from the
IMU gates — sealed truth, blind recovery).

---

## EPOCH 10 — Horizon (design documents before code)

- **Collaboration**: CRDT or lock-based multi-user scene editing (the kdoc JSON layer is the
  substrate). Massive; needs its own constitution amendment.
- **Cloud fleets**: headless instances (E4.4) orchestrated for sweeps/training; artifact store.
- **Digital-twin sync**: the twin_rules/digital_twin_sync ribbon stubs — live plant mirroring
  with drift detection (residual-triggered invalidation already exists in WorldState — reuse).
- **Formal safety**: velocity/force limiters as NON-BYPASSABLE bus middleware; safety-rated
  stop categories mapped to the E-STOP dispatcher; ISO 10218 speed/separation monitoring
  prototypes with the perception stack.

---

# Part III — Gate Authoring Reference (copy this shape)

```cpp
// 1. Module gate (headless, CPU): src/<area>/<Name>Gate.cpp or in the module .cpp
bool krs::<ns>::run<Name>Gate() {
    std::printf("[<tag>] GATE <NAME> -- <one-line contract>\n");
    int pass = 0, total = 0;
    auto check = [&](const char* name, bool ok, /*measured*/ double got, double want) { ... };
    // ... synthetic entities in a Scene registry; analytic asserts with printed numbers ...
    // ... NEG-CTRLs that MUST fail ...
    std::printf("[<tag>] %d/%d checks\n[<tag>] %s\n", pass, total, pass==total ? "ALL PASS (...)" : "FAILURES PRESENT");
    return pass == total;
}
// 2. Hook in RenderingSystem.cpp next to KRS_MEASURE_SELFTEST (printf banner, _Exit(ok?0:1)).
// 3. Bench row in the GateRes g[] table with the one-line contract as the row name.
// 4. Tri-state: return krs::gate::skip() ? true : ... for missing env prerequisites.
// 5. Agent verification: obj-compile via `ninja CMakeFiles/RoboticsSoftware.dir/src/.../X.cpp.obj`,
//    link a scratchpad harness (obj + Scene.cpp.obj + Qt6Core.lib), run, require ALL PASS.
```

Thresholds convention: geometric identities < 1e-6 (double math) / < 1e-4 (through float
transforms) / < 1e-3 m for physics steady-states; rates ±5%; perf rows state absolute wall-time
budgets and the hardware assumption (this RTX 4080 box).

# Part IV — The Trap List (each cost hours; do not rediscover)

1. ADS docks: CLOSED ≠ REMOVED — never destroy resources on `closed`; restore-orphaned docks
   float on `toggleView(true)` — re-dock via `addDockWidgetTab`.
2. `glLineWidth > 1` is invalid in core profile; expand quads instead. `glPolygonOffset` doesn't
   affect lines — bias z in the shader.
3. Integer FBO attachments: NEAREST only, no blending, `usampler2D` + `texelFetch`; PBO fences —
   never map the same frame you read into.
4. Pre-tonemap overlays must divide by `exposureMultiplier` or they render black; post-tonemap
   passes (after TonemapPass in the overlay list) skip it.
5. entt: `std::hash<entt::entity>` isn't automatic — key containers on `uint32_t`. ctx
   `emplace<T>()` returns the EXISTING instance. Nested class types can't be forward-declared.
6. Qt: `QMultiHash` for many-buttons-per-key; `QSignalBlocker` around programmatic setChecked;
   InstantPopup buttons never emit `clicked`; QOpenGLWidget has its own default FBO — rebind it.
7. OCCT: face orientation flips normals (§0.4); seam-split cylinders arrive as two half faces /
   two arc edges; `Axis().Location()` is an arbitrary infinite-axis point — re-seed to trimmed
   midpoints; per-shape try/catch in ALL extraction loops.
8. PhysX: joint frames use X as twist — map anchor-Z→X; joints release BEFORE the scene;
   GPU tier has a body-count gate (small scenes are FASTER on CPU); `fetchResults(true)` blocks
   the calling thread.
9. The compile-error that matters is the FIRST one (`Select-Object -First`), not the last.
10. std::clamp with lo>hw is UB — order bounds from user input. Check EVERY user-fed range.
11. Gate scenes: the boot FANUC is pickable/verticed — keep synthetic rigs 3 m clear, name hit
    entities in failure output (the `'PickBox'` diagnostic pattern).
12. isPureInputFunction: file-fed/script-fed/stateful/scene-probing nodes MUST return false.
13. Windows services squat fixed UDP ports — always walk fallback port pairs.
14. Session hot-reload writes on close and restores on boot — gates suppress via any KRS_* env;
    `KRS_SESSION_TEST=1` opts back in.

# Part V — ctx Singleton Registry (the shared state surface)

`SceneProperties` · `SelectionState (krs::sel)` · `SnapSessionState (krs::snapui)` ·
`ConstraintGraphComponent (krs::constraint)` · `MateGraphComponent` · `RobotGraph (krs::rbuild)` ·
`RobotRegistry (krs::robot)` · `WorldState (krs::world)` · `SkillRuntimeHolder (krs::skill)` ·
`ArticulationCommandComponent` (the per-pass bus) · `AttachedEffectors (krs::ee)` ·
`MeasureUiPrefs (krs::measure)` · `ConstraintFocusRequest (krs::ui)` · `EnvironmentSettings` ·
`OpenSceneInfo (krs::ksave)` · `PropertyCatalog (krs::twin)`.
Adding one: get-or-emplace accessor function, document the writer/reader contract at the struct,
and if it must persist — E1.1 is your template.

---

*Final note from the architect: the platform's superpower is not any subsystem — it is the
discipline. 178 gates let a hundred agents build on each other's work without meetings. Guard
the discipline harder than the code. — F.*
