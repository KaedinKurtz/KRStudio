# Behavior, Policy, Animation-Import & Learning (design)

Status: **design / not yet built.** This is the layer *above* motion — how a robot in KRStudio is
told *what to do over time* (behavior/policy), how authored or captured motion (glTF / BVH / mocap)
becomes a robot reference trajectory, and how that reference seeds reinforcement learning instead of
starting RL from a cold random policy. It sits on top of the shipped motion primitives (IK, OMPL
planning, computed-torque execution, redundancy resolution) and the node/command bus, and it feeds
the actuator-characterization flywheel (see `ACTUATOR_MODEL.md`) by exposing a real sim-to-real gap
to close.

## The layered stack (each layer depends only on the one below it)

The load-bearing design claim: **motion is a pyramid**, and every layer is meaningless without the
one under it. Build bottom-up; each level is independently gateable.

```
6  RL SEEDING        residual policy learned on top of the reference (external trainer)
5  ANIMATION IMPORT  glTF/BVH/USD clip -> retargeted robot reference trajectory (imitation seed)
4  BEHAVIOR (POLICY)  BT / state machine over the node system: sequence, branch, react, loop
3  TRAJECTORY TIMING  time-parameterize a path from actuator vel/accel limits (feasible, not just geometric)
2  REDUNDANCY FOLLOWER path through config space that also satisfies secondary objectives (elbow, limits, posture)
1  CARTESIAN PATH     a geometric primitive: a sequence of EE poses (task-space waypoints), no timing
```

- **Layer 1 — Cartesian path primitive.** A *purely geometric* ordered list of end-effector poses
  (`krs::dyn::Pose` = {R, p}) in task space — a straight line, an arc, a spline through via-points.
  No timing, no joint angles, no dynamics. This is the atom everything else consumes. It is already
  latent in the codebase: the OMPL node emits a joint-space path, and the IK primitive
  (`SerialChain::ik`, GATE IK-POSE / GATE IK-SOLVES) turns a single target pose into a config. Layer
  1 is the *task-space* version of that — a list of poses, agnostic to which robot realizes it.

- **Layer 2 — Redundancy-aware follower.** Turn the pose path into a joint-space path. For each pose
  we need an IK solution (`SerialChain::ik` -> `IKResult`), but on a redundant arm (nq > 6) IK is a
  one-to-many map: the null space is a free surface. The follower resolves that freedom *coherently
  along the path* using `krs::redun::resolveInNullspace` + the secondary builders
  (`posture`, `limitAvoidance`, `jointBias`, `linkToPoint`) so consecutive frames stay near each
  other (no elbow flips), stay off joint limits, and honor a preferred posture. **Depends on Layer 1**
  because it needs the pose sequence to solve against; the GATE REDUNDANCY null-space machinery is
  exactly this resolver.

- **Layer 3 — Trajectory timing.** The joint-space path from Layer 2 is still just *geometry in
  config space* — an ordered list of configs with no clock. Layer 3 assigns a time to each waypoint
  so the motion is **dynamically feasible**: no joint exceeds its velocity or acceleration limit. The
  per-dof traverse-time helper already in `krs::plan` (`|Δq_i| / vMax_i`, the fastest dof rides
  exactly at `vMax`) is the velocity-limited core; the acceleration/jerk-limited version
  (time-optimal path parameterization, TOPP-style) layers on top. Crucially, the limits are **not
  hand-picked constants** — they come from the actuator model (`ACTUATOR_MODEL.md`): the torque-speed
  envelope sets `vMax`, reflected inertia + torque ceiling set `aMax`. This is the seam where the
  actuator flywheel makes trajectories physically honest. **Depends on Layer 2** because you cannot
  time a path you do not yet have in joint space, and the timing limits are per-joint.

- **Layer 4 — Behavior (policy).** Layers 1–3 produce *one* timed motion. Layer 4 is *what to do and
  when*: sequence several motions, wait on a sensor, branch on a force reading, loop, retract on
  fault. This is a behavior tree / finite state machine, built on the **existing** node system and
  command bus (detailed below). It **depends on Layer 3** because its leaves *are* timed trajectories
  ("move to A" = plan + time + execute a path); the behavior layer is pure orchestration and owns no
  motion math of its own.

- **Layer 5 — Animation import.** An external clip (authored in Blender, captured on a mocap stage)
  is a *reference trajectory* for the behavior layer to imitate. Import + retarget turns skeleton
  motion into a robot joint-space reference. **Depends on Layer 4** because a clip is played *inside*
  a behavior ("do this authored gesture, then wait"), and on Layers 1–2 because retargeting *is*
  per-frame IK through the redundancy resolver.

- **Layer 6 — RL seeding.** A retargeted reference is the imitation target (DeepMimic/AMP). RL learns
  a small *residual* correction on top of it, which is dramatically more sample-efficient than
  learning from scratch. **Depends on Layer 5** for the reference and on the actuator model for the
  sim it trains in; the gap the reference exposes is the actuator flywheel's fuel.

Read top-down, the stack answers a question chain: *what pose sequence?* (1) → *which joint solution?*
(2) → *at what speed?* (3) → *in what order, reacting to what?* (4) → *imitating what demonstration?*
(5) → *improved how?* (6).

## 2. Policy substrate: a behavior tree over the node graph + command bus

**The foundation is the node graph, not a scripting language.** KRStudio already ships the exact
primitives a behavior tree needs — as *trigger-model* nodes (`src/Nodes/ControlFlowNodes.cpp`, gated
by GATE WHEN / GATE IF / GATE WHILE / GATE TRIGGER-EDGE) plus a BT scaffold in `AINodes.cpp`. The
policy layer is a *composition discipline* over these, not new infrastructure.

The trigger model (the load-bearing abstraction): a **trigger** is a boolean PULSE true for exactly
one eval tick (the Button node); a **condition** is a boolean level (a Compare node's `a>b`). Every
control-flow node is defined in these terms, which is why they compose cleanly:

- **Compare** (`logic_compare`) — the CONDITION source. `A,B` (spin boxes) + `Op` (enum) → a bool.
  This is "force > X", "at target", "sensor high".
- **When** (`flow_when`) — fires a trigger PULSE on a condition's `false->true` EDGE (not a level).
  "When the sensor goes high, do the next thing." Edge, not level, so it fires *once* per event.
- **If** (`flow_if`) — routes an incoming trigger pulse to its True or False output by the condition
  *at trigger time*. Exactly one branch fires. This is the reactive `if force>X retract`.
- **While** (`flow_while`) — a BOUNDED, event-driven iteration: one Body pulse per tick while the
  condition holds, with a **mandatory max-iteration cap** (GATE WHILE explicitly catches an infinite
  loop). This is the safety-critical difference from a raw scripting `while`.
- **BT Action / BT Sequence / BT Selector** (`ai_bt_action` / `ai_bt_sequence` / `ai_bt_selector`)
  with a `BTStatus` enum {Success, Failure, Running} + a Blackboard (`ai_get_blackboard_float`) —
  the canonical BT vocabulary. Sequence = "all children in order, fail-fast"; Selector = "first
  child that succeeds"; the `Running` status is what makes a *long-lived* action (a multi-second
  trajectory) tickable rather than blocking.

The action leaves that *drive the robot* already exist too: **IK Target** (`GATE IK-SAMPLE` — samples
a pose on trigger, solves IK, holds), **OMPL Planner** (`GATE OMPL` — two-stage PLAN/EXECUTE), and
the joint-drive/command path (`RobotRef`, `GATE OWNERSHIP` / `GATE DRIVE-BY-NAME` — a node command is
the sole joint driver, FK round-trips <1e-4). The **command bus** is this node→robot write path plus
the Twin/PropertyCatalog read-back (`GATE TWIN`, `publishRobotState`); a behavior senses through the
Twin and acts through the command path — the same bus in both directions.

**Worked example — "Move to A, wait for a sensor, if force > X retract"** as a behavior tree over
existing nodes:

```
BT Sequence
├─ BT Action:  IK Target(pose=A) -> OMPL(PLAN+EXECUTE)      [Running until at A, then Success]
├─ BT Action:  When( Compare(sensor, >, threshold) )        [Running until the edge fires]
└─ BT Selector
   ├─ If( Compare(force, >, X) ) -> True  -> IK Target(pose=A_retracted) -> EXECUTE   [reactive retract]
   └─ (fallthrough)                                          -> hold                   [nominal]
```

Every box here is a node type that already exists and is already gated. The behavior layer's *only*
new deliverable is (a) a small conventionalization — a canonical "BT root tick" node that walks the
tree at eval rate and surfaces the active leaf, and (b) authoring ergonomics (a subtree/group node so
a whole behavior collapses to one box). No new execution model: the eval loop
(`evaluateGraphQuiet`, 60 Hz+, decoupled via `NodeEditQueue`, proven by GATE THREAD) already ticks
everything.

**State machine vs behavior tree:** both fall out of the same nodes. A BT is the recommended default
(reactive, composable, the `Running` status gives you preemption for free). A flat FSM ("state = enum,
transitions on conditions") is expressible as a Selector of `If`-guarded states over a Blackboard
state variable — offer it as an authoring template, not a separate engine.

**The scripting escape hatch (later, not the foundation).** A visual graph is the right *primary*
surface — it is inspectable, diffable in the scene file, and gateable node-by-node. But some users
will want text. Recommend **Lua via sol2** (both MIT — clean for a permissive codebase; sol2 is
header-only, trivial to vendor). Lua is the industry default for exactly this (embeddable, sandboxable,
cheap to suspend/resume — coroutines map naturally onto the `Running` tick model). Bind a *narrow*
API: `moveTo(pose)`, `waitFor(condition)`, `read(channel)`, `command(joint, q)` — i.e. the same
command-bus verbs the BT leaves use, so a script and a graph are interchangeable front-ends over one
substrate. **Explicitly a convenience, not the foundation**: the moment scripting becomes load-bearing
you lose the per-node gate story and the diffable scene. Ship the BT first; add Lua when a real user
needs an escape hatch, and keep it strictly a thin veneer over the node command bus.

## 3. Animation import formats

Goal: get authored / captured articulated motion *in*, normalized to one internal type, so retargeting
and RL seeding never see a file format. Motion here is **skeletal animation** — a joint hierarchy with
a per-frame pose — NOT volumetric data. (Note: OpenVDB / `.vdb` is the wrong tool here; it is a sparse
*volumetric* grid for fluids/smoke/SDFs — it has no notion of an articulated skeleton. It belongs to
the field/SDF side of the engine, GATE SDF, not to animation import. Do not reach for it.)

Recommended formats, in order of when to build them:

- **glTF 2.0** — *recommend as the primary authored-animation path.* Skeletal animation is a
  first-class part of the spec (skins + animation channels with sampler interpolation over
  translation/rotation/scale). **Blender exports it natively**, so an artist round-trips without a
  plugin. Parsers: **cgltf** (single-header C, MIT) or **tinygltf** (header-only C++, MIT) — either is
  a clean, permissive vendor with zero heavy dependency. This is the sweet spot: rich enough for real
  authored motion, ubiquitous tooling, trivial licensing.
- **BVH** — *recommend as the raw-mocap path.* The de-facto mocap interchange: an ASCII `HIERARCHY`
  block (joint tree + offsets + channel order) followed by a `MOTION` block of Euler-angle frames at a
  fixed frame time. **Trivial to self-parse** (a few hundred lines, no library, no license question) —
  and self-parsing is a feature, because you control the coordinate/Euler conventions exactly. Ideal
  for ingesting captured human/animal motion for imitation.
- **USD** (`.usd` / `.usdc`) — *the heavyweight interchange option, later.* Apache-2.0, the emerging
  studio-pipeline standard (skeletal animation via UsdSkel), composable/layered, and increasingly what
  large asset pipelines speak. But the runtime is a *large* dependency with a real build cost. Adopt it
  only when interop with a USD-native pipeline is a concrete requirement — not for the first import.

**Tradeoffs / recommendation.** glTF = best authored-motion ergonomics + tiny permissive parser →
**build first**. BVH = zero-dependency mocap ingestion + full control of conventions → **build second**
(the two together cover authored *and* captured motion). USD = maximal interop at a heavy dependency
cost → **defer** until a pipeline demands it. All three are permissive (MIT parsers / Apache USD), so
licensing never forces the choice — capability and build-cost do.

**One common internal type: `SkeletonClip`.** Every importer targets a single format-agnostic in-memory
structure so nothing downstream knows or cares where the motion came from:

```
struct SkeletonClip {
    struct Joint { std::string name; int parent; Eigen::Vector3d restOffset; };  // hierarchy + rest pose
    std::vector<Joint> joints;                 // topological order (parent < child)
    double frameRate;                          // Hz
    // per frame, per joint: a local transform (translation + quaternion), root carries global motion
    std::vector<std::vector<Eigen::Isometry3d>> frames;   // frames[f][j] = local pose of joint j at frame f
    // optional named end-effector sites (hand, foot) for retargeting anchors
    std::vector<std::pair<std::string,int>> sites;
};
```

Quaternions internally (no Euler ambiguity, cheap SLERP resampling); an SE(3) local transform per
joint (glTF wants scale too — drop or record it, robots do not scale). Importers normalize units to SI
metres, up-axis to the engine convention, and resample to a common frame rate on the way in, so the
retargeter sees one clean signal regardless of source. This mirrors the actuator/telemetry philosophy:
**normalize at the boundary, keep the interior format-free.**

## 4. Retargeting: a clip is a reference trajectory, fitting the robot is per-frame IK

A `SkeletonClip` is a *human/character* trajectory; a robot has a *different* kinematic structure
(different link lengths, fewer/more DoF, different joint axes). Retargeting maps one to the other.

The mapping is **per-frame IK**, and it is *exactly the Layer-1→Layer-2 pipeline reused*:

1. **Correspondence.** Pick which skeleton joints/sites drive which robot links — usually the
   end-effector(s): skeleton "hand" site → robot flange (`RobotRef::ee`), optionally "elbow" →
   an intermediate link. This correspondence is authored once per (skeleton, robot) pair.
2. **Per-frame task.** For each clip frame, read the skeleton site poses (forward kinematics on the
   `SkeletonClip` hierarchy) → a target `Pose` for each corresponded robot link. That target *is* a
   Cartesian-path waypoint (Layer 1).
3. **Solve.** Run the robot IK (`SerialChain::ik`) to hit the primary EE target, and let the
   **redundancy resolver handle the DoF mismatch** (`krs::redun::resolveInNullspace`): the elbow site
   becomes a `linkToPoint` secondary, "stay human-like" becomes a `posture` secondary toward the
   retargeted reference, and `limitAvoidance` keeps the solution in-range. This is precisely why
   Layer 2 exists — the skeleton has more (or differently-placed) freedom than the robot, and the
   null-space machinery absorbs the difference instead of failing.
4. **Coherence + timing.** Warm-start each frame's IK from the previous frame's solution so the
   joint-space reference is continuous (no flips), then time-parameterize it against actuator limits
   (Layer 3) — a human clip's timing is *not* dynamically feasible on the robot and must be re-timed.

The product is an **imitation SEED**: a robot joint-space reference trajectory `q_ref(t)` (plus its
`q̇_ref`) that approximately reproduces the demonstrated motion, feasible on *this* robot. It is
"approximate" by construction — link-length and DoF mismatch mean the robot cannot exactly reproduce a
human motion — and that residual mismatch is the *point*: it is what RL refines. Honest failure modes:
unreachable frames (clip target outside the robot's workspace → IK clamps, `IKResult::clampedToReach`;
the retargeter must flag and either clip or scale the motion) and self-collision on the robot that was
fine on the skeleton (validity-check the seed against the collision world before handing it on).

**Gate shape** (headless, pure-CPU, Eigen-only): retarget a synthetic clip whose EE traces a known
circle onto the default `RobotRef` arm; assert the resolved `q_ref(t)` reproduces the circle within IK
tolerance frame-by-frame, stays within limits, and is continuous (bounded frame-to-frame `Δq`).
NEG-CTRL: a clip demanding a pose outside the workspace must be *flagged* (clampedToReach), not
silently wrong; a resolver run with no posture/limit secondary flips the elbow (larger `Δq`) —
demonstrating the redundancy resolver is doing real work.

## 5. RL seeding: imitation-first, residual policy, external trainer

RL-from-scratch on a 6+ DoF arm is a sample-eating disaster (millions of steps, reward hacking,
degenerate gaits). The reference trajectory is the fix.

**Paradigm: reference-trajectory imitation (DeepMimic / AMP).** DeepMimic: reward = "match the
reference at each phase" (imitation reward) + a small task reward; the policy learns to *track* the
retargeted `q_ref(t)` and only deviates where the task demands. AMP (adversarial motion priors)
generalizes it — a discriminator rewards "moves like the reference *distribution*" without exact
phase-locking, so behavior stays natural while the task reward drives the goal. Either way the
retargeted clip from Layer 5 is the anchor.

**Residual policy is the key efficiency lever.** Do NOT ask the network to output raw torques from
zero. The base action is the reference (`q_ref`, or a computed-torque feedforward that tracks it — the
GATE EXECUTE controller); the policy outputs a *small residual* `Δ` on top. The search starts from an
already-good motion, so exploration is local, sample complexity drops by orders of magnitude, and the
worst-case behavior (residual → 0) is "just track the reference," which is already safe. This composes
perfectly with the actuator model: the feedforward runs through the *gray-box actuator transfer
function*, so the residual only has to learn what the physics model *misses* — the same "physics +
small learned residual" split the actuator doc uses, one layer up.

**The sim-to-real flywheel connection (be honest about it).** The reference exposes a concrete
sim-to-real gap: track `q_ref(t)` in sim, track it on the real robot, and the *difference* is a
direct, quantified measurement of where the actuator model is wrong (backlash lag, friction, torque
droop under load). That error signal feeds the actuator-characterization loop (`ACTUATOR_MODEL.md`) —
imitation gives you a *repeatable excitation* to measure against, which is exactly what identifiability
needs. Better actuator model → smaller sim-to-real gap → residual policy transfers → the model
improves from the residual it had to learn. That is the flywheel, closed.

**What is tractable in-engine vs external (the honest boundary).** Do **not** implement the RL trainer
(replay buffers, PPO/SAC, autodiff, GPU rollout batching) in C++ inside KRStudio — that is a
research-grade moving target and a poor fit for a Qt/OpenGL studio. Instead:

- **In-engine (build here):** the *environment* and the *interface*. KRStudio already is a good sim —
  it has the dynamics (`krs::dyn`, computed-torque GATE EXECUTE), the actuator transfer function, the
  collision world, and the reference. Expose a **gym-style step interface over the existing command +
  telemetry bus**: the trainer sends an action (a residual on `q_ref`) via the command path, the
  engine steps physics and returns observation + reward via the Twin/telemetry channels. This is the
  *same bus* the behavior layer and the hardware telemetry protocol (`TELEMETRY_PROTOCOL.md`) already
  use — RL is just another consumer/producer on it. Reward shaping (imitation + task terms) and the
  reference are engine-side.
- **External (interface to, do not reimplement):** the actual optimizer — a Python trainer
  (Stable-Baselines3, RLlib, or a custom PPO) driving KRStudio as its environment over the bus. This
  keeps the fast-moving RL stack in the ecosystem built for it, and keeps KRStudio's role as the
  high-fidelity, gateable *simulator + reference source*. The wire format is the telemetry/command
  protocol we already froze, so no new IPC layer is invented.

This division is deliberate and matches the rest of the project's philosophy: keep the *honest physics
+ interpretable structure* in-engine (sim, actuator model, reference, reward), and let the black-box
optimizer live outside, talking over a typed bus. In-engine gates prove the environment is correct
(reference tracking, reward monotonicity, deterministic step); they do *not* try to gate "the policy
converged."

## Phased build (each independently useful, each gateable)

1. **Cartesian path + redundancy follower.** Task-space pose-list primitive → per-frame IK →
   `resolveInNullspace` for a coherent joint-space path. *Gate:* a straight-line/arc EE path resolves
   to a continuous joint path within IK tol, on-limits, elbow-coherent; NEG-CTRL a no-secondary run
   flips the elbow. (Reuses GATE IK-POSE + GATE REDUNDANCY.)
2. **Trajectory timing from actuator limits.** Time-parameterize the path against per-joint `vMax`/
   `aMax` (from the actuator model, `ACTUATOR_MODEL.md`). *Gate:* the timed trajectory respects vel/
   accel limits (the fastest dof rides `vMax` exactly), monotone time; NEG-CTRL a 2× time-compressed
   copy violates the limit and is flagged. (Reuses the `krs::plan` traverse-time helper.)
3. **Behavior layer (BT/FSM over existing nodes).** A BT-root tick node + subtree grouping over the
   already-gated Compare/When/If/While + BT Action/Sequence/Selector + IK Target/OMPL leaves. *Gate:*
   the worked "move→wait→branch-retract" tree executes the correct leaf sequence and reactively
   retracts when force>X; NEG-CTRL a level (not edge) condition mis-fires; the While cap catches a
   runaway. (Reuses GATE WHEN/IF/WHILE/OMPL/IK-SAMPLE.)
4. **Animation import → `SkeletonClip`.** glTF importer (cgltf/tinygltf) first, then a self-parsed BVH
   importer, both normalizing to `SkeletonClip`. *Gate:* a known glTF and a known BVH round-trip into
   identical `SkeletonClip` frames (SLERP-resampled to a common rate) matching closed-form joint poses;
   NEG-CTRL a malformed hierarchy is rejected, not silently truncated.
5. **Retargeting → imitation seed.** Per-frame IK + redundancy mapping `SkeletonClip` → robot
   `q_ref(t)`, re-timed by step 2. *Gate:* the seed reproduces a synthetic clip's EE trace within tol,
   collision-free, continuous; NEG-CTRL out-of-workspace frames are flagged (`clampedToReach`), not
   faked. (Reuses steps 1–2 + GATE SELFCOLLISION.)
6. **RL environment interface (external trainer).** A gym-style step API over the command/telemetry
   bus: action = residual on `q_ref`, observation + reward = Twin/telemetry, base action = the GATE
   EXECUTE feedforward through the actuator transfer function. *Gate:* the environment is
   deterministic (same action seq → same trajectory), the imitation reward is maximized by the pure
   reference (residual 0), and a residual improves a task reward on a synthetic objective; NEG-CTRL a
   scrambled reward is *not* maximized by the reference — proving the reward tracks the reference. The
   optimizer itself stays external (Stable-Baselines3 / RLlib), interfaced, not implemented.
7. **Scripting escape hatch (Lua/sol2), optional.** A thin Lua veneer (`moveTo`/`waitFor`/`read`/
   `command`) over the *same* command-bus verbs the BT leaves use, so scripts and graphs are
   interchangeable. *Gate:* an identical behavior authored as a graph and as a Lua script drives the
   robot to the same FK state (<1e-4). Convenience only — the graph remains the gateable foundation.

Steps 1–3 give you an authorable, reactive robot behavior with *no* learning at all (valuable alone: a
teach-and-repeat / scripted-cell studio). Step 4–5 add "show it a motion and it imitates." Step 6 is
the RL flywheel, and it deliberately reuses the telemetry/command bus rather than inventing IPC. Step 7
is ergonomics. Nothing above Layer 3 owns any motion math — it all delegates down to the shipped,
gated primitives, which is what keeps the whole tower honest and testable.
