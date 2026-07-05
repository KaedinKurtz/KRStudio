# Goal Workspace — declarative goals, regression planning, and parameter discovery

Status: **G1–G5 BUILT AND GATED** (2026-07-04, commits b23bf22d / 01b41c98 / 6c24bee2). Gates:
`KRS_KGOAL` (.kgoal doc+builder), `KRS_GOALPLAN` (regression + gaps + envelopes + live guards),
`KRS_HONE` (population honing: observability honesty + Dynamic slosh detection), `KRS_GOALLOOP`
(the full story incl. persistence + invalidation), knowledge ledger in `KRS_WORLDSTATE`/
`KRS_SCENESAVE`. Round-3 decisions (user): trait seeding = best guesses from material/visual
properties; **SimOnly/R2S learn-tags per object** gate extraction AND persist learned params with
the scene (yesterday's glass never re-derived); 6-DoF FT sensor assumed for excitations (CoM =
tilt-and-read-moment); DYNAMIC parameters (sloshing water) must be flagged, never mis-fit —
`krs::hone` half-fit drift detection ships this. RANSAC recognition against an explored object
library = future perception work; divergence-marking is covered by residual-triggered
invalidation ("not a problem until it becomes a problem"). Primitive bodies ship as
`assets/skills/{move_to,grasp,place,push}.knode`. Remaining: hardware excitation via telemetry,
grasp-pose synthesis for the primitive interiors, `goal_maintain` dynamic-goal node, RANSAC library.
Builds directly on the shipped Process & Skills layer (P0–P5: `krs::skill` SkillSpec/Predicate,
`krs::world::WorldState`, `SkillRuntime`, `planTask`) and the data ecosystem (`.klut`, `.rec`,
Data Recorder, `krs::act::Param`, `RlEnv`).

## The vision (user)

A block-code-style goal-definition workspace: object states, preconditions, prerequisites as
snap-together blocks. The robot responds to a goal by a **reverse tree search** — figure out what
must change between the current state and the goal state by chaining primitives, each carrying its
own **requirements and priors**. While regressing, the robot **recursively checks whether it has the
knowledge** each primitive needs (e.g. the mass of an object it has never manipulated); missing
knowledge populates a **to-do list of excitation routines** for parameter discovery through digital-
twin honing: run many small twin-vs-real interactions with the perception thread updating state
fast, compare what the twin predicted against what REALLY happened, and converge a population of
parameter guesses onto the physical value. Goals must also be buildable **programmatically through
an API**, identically to the blocks. Future: **dynamic goals** re-evaluated every n ticks.

## Precise naming (what each piece is)

- The "reverse tree search" = **goal regression** (backward chaining): from the goal, find skills
  whose *effects* establish it; recurse on *their* preconditions until everything grounds in the
  current WorldState. Regression yields the **relevance chain** — exactly which state variables and
  parameters matter for THIS goal — which is what turns "never weighed it" into a targeted to-do.
- The honing loop = **population-based system identification through the digital twin**
  (CEM/particle flavor): hypothesize parameter sets, roll each through the twin, execute the real
  excitation once, score hypotheses by prediction error, resample; write back `{value, Measured, σ}`.

## Architecture

1. **Knowledge-typed world state.** `WorldObject` gains a parameter store — `mass`, `CoM`,
   `friction`, `restitution`, … — each a `Param{value, provenance(Unknown/Prior/Estimated/Measured),
   sigma}` (the exact pattern `krs::act::Param` already ships for actuators; generalized to objects).
2. **`needs[]` on SkillSpec.** Beside `pre[]`/`eff[]`: the parameters a skill's execution model
   requires, each with a maximum acceptable sigma ("lift(cup) needs mass(cup) σ<0.05 kg").
3. **Regression planner with epistemic gaps.** `planBackward(goal)` regresses through skills; a
   too-uncertain `needs[]` entry emits a `KnowledgeGap{object, param, σ_now, σ_needed}` instead of
   failing. A plan returns (skill sequence, gap list).
4. **Excitation routines are skills with *epistemic effects*** — `lift_weigh(obj)` has effect
   `σ(mass(obj)) → 0.02`. Reducing uncertainty is an effect like any other, so the SAME planner can
   schedule information gathering before the step that needs it. Gaps either auto-plan or queue on
   the to-do for user approval (excitation moves a real robot).
5. **The honing executor**: N parameter hypotheses → deterministic twin rollouts (the `RlEnv`
   machinery) → ONE real execution captured by perception + the Data Recorder → score → resample →
   converge → write back `Param{value, Measured, σ}` (+ optionally bake a `.klut`). The hypothesis
   cloud plots live in the Data Recorder.

## UX — the Goal Workspace dock

- **Left — world browser**: live WorldState objects/frames/robots; each object shows parameter
  badges colored by provenance (green measured / yellow prior / red unknown). Drag into predicates.
- **Center — goal stack**: predicate cards (`at(cup, drop_pose) ±0.1`, `holding(r0, cup)`,
  `gripperOpen`…), AND-composed, each showing LIVE truth (✓/✗) against the current world.
- **Right — plan & knowledge**: [Plan] → the regressed skill sequence (per-step pre/eff/bindings);
  the **Knowledge To-Do** (gaps + suggested excitation + [Queue]); [Execute] → `SkillRuntime` with
  live step highlighting + the existing retry/replan status.

**The artifact is `.kgoal`** — content-addressed like the rest of the `.k*` family. The panel edits
the document; a C++ builder writes the SAME document (`GoalDoc g; g.require(at("cup","drop_pose",
0.1));`) — block UI and API are two front-ends to one format, shareable/packable like skills.

**Why not the node canvas for static goals:** the node editor is dataflow (continuous evaluation);
a goal is declarative (a state description). But *dynamic* goals ARE dataflow — a later
`goal_maintain` node references a `.kgoal`, re-evaluates every n ticks, and replans on violation.
Both idioms, each where it belongs.

## Round 2 (user ideation + agreed formalization)

**Observability-weighted estimation (SETTLED: population + info-weighted updates).** The hypothesis
population measures observability EMPIRICALLY: the spread of the N hypotheses' twin-predicted
trajectories under an excitation IS the empirical Fisher information (∂y/∂θ by simulation). High
spread → residuals discriminate → posterior collapses hard (high influence); low spread → the
parameter is unobservable from that excitation → sigma honestly barely moves. Consequence:
excitations are SCORED IN THE TWIN BEFORE the robot moves (expected information gain per candidate
routine = optimal experiment design) — the to-do shows "lift_weigh: expected σ 0.4→0.05;
slide_test: σ 0.4→0.38 (mass unobservable from sliding)". Handles mass/friction identifiability by
choosing decorrelating excitations. Convergence in tens of samples is the expected regime for 1–3
params/object. In-house precedent: krs::imu::runExcitationObservGate. Priors seed from CAD:
MaterialComponent density×volume → massKg = provenance "Prior (from material)" with wide sigma.

**Residual-triggered belief invalidation ("every interaction is a free excitation").** The
twin-vs-real residual runs during EVERY skill execution, not just excitation: matching reality
tightens beliefs for free; an integral residual ∫|y_real−y_twin|dt crossing threshold in the first
reconstruction samples INVALIDATES the touched object's parameters (sigma bumps, provenance →
Suspect) — staleness IS sigma; the planner re-sees the gap naturally. Credit assignment v1: bump
all params of the touched object; v2: match the residual signature against per-parameter
sensitivity directions (same population spread). The later "visibly changed" perception pipeline is
just a second writer to the same sigma.

**Traits → envelopes → intersection propagation (SETTLED: requirements are DERIVED, never
user-mandatory).** Intrinsic needs live on the primitive TYPE, authored once by the skill author
(grasp needs mass/friction/graspable-geometry). CONSTRAINT MODIFIERS live on objects as TRAITS
(liquid-container → {maxTilt, maxAccel}; fragile → {maxForce}; sharp; hot); binding a skill to an
object injects its traits' constraints into the skill's execution ENVELOPE. Propagation =
INTERSECTION down the decomposition tree (the move inside carry(glass) inherits maxTilt; most
restrictive wins); knowledge gaps bubble UP the same tree — the annotated decomposition tree is the
"tree structure graph that falls out". Constraints compile to TWO artifacts: (a) planner filters,
and (b) auto-injected RUNTIME GUARDS (live Condition/BT nodes watching actual tilt/force, tripping
retry/replan) — boundary conditions must reach the running controller or they are decorative.
User-modifiable at the subgraph level (tighten freely; loosening = loud logged override).
IN-PROCESS observables (e.g. bolt-head torque): needs flagged observe-in-process — the skill's
instrumentation must publish them as live channels (recorder taps); pre-check is only "is the
estimator for this channel available?".

**Primitives ARE subgraphs (the OCCT/OMPL counter-argument dissolves).** The heavy machinery is
already node-wrapped (ompl_planner, ik_target, physics_config_drive nodes; the avoidance
FieldSolver; gated When/If/While control-flow nodes) — a move_to primitive-subgraph COMPOSES the
C++ planner node with reactive field-following and boolean timing logic ("while EE >10cm from
grasp: follow field gradient"). No unzipping required. Primitives as .knodes = instrumentable,
tunable, shareable (.knodepack), versioned; the P1 hybrid contract stays the OUTER interface so
the planner never sees the interior.

## Open decisions (awaiting user)

1. **Workspace surface**: dedicated panel + `.kgoal` first, dynamic-goal nodes later *(recommended)*.
2. **Excitation autonomy**: auto-insert vs ask-first queue *(recommended: ask-first + per-routine
   "auto-OK" flag)*.
3. ~~Estimator flavor~~ **SETTLED round 2**: population/CEM with information-weighted updates.
4. **Round-one scope**: sim-only honing first (twin-vs-twin, injected truth) *(recommended)*.
5. **Trait ontology seed set**: liquid-container / fragile / rigid-deformable / mass-source;
   manual assignment in the properties panel first, perception-assigned later?
6. **Envelope persistence**: traits in `.kscene`, intrinsic needs+envelopes in the skill `.knode`
   manifest, COMPOSED envelope stamped into the plan artifact at plan time (auditable "this move
   was tilt-limited because glass was liquid-filled")?
7. **First three excitation routines**: lift_weigh (mass), push_probe (friction), tilt_settle
   (CoM) — each a primitive-subgraph with CLAIMED information gain, gated by the twin's MEASURED
   population collapse matching the claim (+ an uninformative-routine ~zero-collapse neg-ctrl)?

## Phased build sketch (each gated, house idiom)

- **G1** `.kgoal` document + C++ builder API (`krs::goal`), content-addressed round-trip.
  Gate: build-save-load-evaluate a goal against a WorldState; tamper/major refusals.
- **G2** `planBackward` + `needs[]`/`KnowledgeGap`. Gate: regression reproduces the forward
  pick/place plan; a high-sigma `needs` emits the right gap; the relevance chain excludes
  irrelevant objects. NEG: an unsatisfiable goal fails bounded.
- **G3** Epistemic effects + gap→excitation planning. Gate: a plan with a gap auto-inserts
  `lift_weigh` before `lift`; ask-first mode queues instead.
- **G4** Sim-only honing executor. Gate: twin-vs-twin with injected truth mass — the population
  converges to truth within tolerance and sigma shrinks monotonically; NEG: an uninformative
  excitation does NOT shrink sigma (honesty).
- **G5** The Goal Workspace panel (eyes-on) + live-truth cards + to-do UI + Execute wiring.
- **G-later** `goal_maintain` dynamic-goal node; hardware excitation via the telemetry loop.
