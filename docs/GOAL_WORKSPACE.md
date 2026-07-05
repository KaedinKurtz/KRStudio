# Goal Workspace — declarative goals, regression planning, and parameter discovery

Status: **design agreed in principle / open decisions marked below — not yet built.**
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

## Open decisions (awaiting user)

1. **Workspace surface**: dedicated panel + `.kgoal` first, goal-blocks on the node canvas, or
   panel-first with dynamic-goal nodes later *(recommended)*.
2. **Excitation autonomy**: auto-insert excitation skills into plans vs always queue for approval
   *(recommended: ask-first default with a per-routine "auto-OK" flag)*.
3. **Estimator flavor**: population/CEM (robust, visualizes the converging cloud, matches the
   description) vs RLS/EKF (cheaper, Gaussian) *(recommended: population)*.
4. **Round-one scope**: `.kgoal` + panel + `planBackward` with `needs[]`/gaps + to-do UI, honing
   **sim-only** (twin-vs-twin with injected truth parameters proves the estimator honestly) before
   any hardware excitation *(recommended)*.

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
