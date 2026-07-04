# Actuator Model & Characterization Ecosystem (design)

Status: **design / not yet built.** Extends the shipped `.kmotor`/`.kactuator` chain
(`krs::ksave::resolveActuator`, which today derives scalar joint effort/velocity). This doc pins
the plan for a real-2-sim-faithful actuator representation and the learning flywheel that improves
it as it sees real data.

## The core framing: gray-box, not black-box

The primary representation is a **physics model with interpretable parameters + a small learned
residual** — NOT a neural net that mimics one actuator (ANYmal's Actuator-Net approach: works, but
opaque, non-transferable, data-hungry). Gray-box buys: parameters a user can read/edit, data
efficiency (fit ~15 numbers, not train from scratch), family-shareable residuals, and graceful
degradation (no data → physics + manufacturer priors; some data → fit structural params; lots of
data → add the residual).

## Non-idealities to represent (each a named, bounded parameter or tiny state)

An actuator = motor + transmission + encoder + embedded controller. In rough order of sim-to-real
impact:

- **Torque–speed envelope** (not a scalar): `Kt·I` saturates against the current limit at low speed
  and the voltage/back-EMF limit at high speed (the corner). Params: `Kt`, `I_max`, voltage/speed
  constant.
- **Friction curve**: Coulomb + viscous + Stribeck dip near zero speed, with **direction
  asymmetry** (backdriving a worm/harmonic drive differs). Optional LuGre for presliding hysteresis
  (fine positioning).
- **Backlash**: position dead-zone on direction reversal. One angle param; the thing people feel
  first.
- **Torsional compliance**: the drive is a stiff spring, not rigid — series-elastic behavior that
  dominates control bandwidth. Harmonic drives: notable compliance + periodic kinematic
  (transmission) error.
- **Cogging + torque ripple**: position-periodic disturbance; cogging is current-independent, ripple
  scales with current.
- **Encoder**: resolution (bits), noise, latency, and **location** (motor-side sees neither
  backlash nor compliance; output-side sees both) — this choice changes the whole observable
  behavior.
- **Thermal state**: I²R heating → winding temperature → resistance up → `Kt` droop → torque
  ceiling derate. Path-dependent → a small state, not a param.

## Characterization pipeline: system ID, not a data hose

Specific **designed excitations** make specific params observable; the dynamometer runs these so
each param is isolated before a joint refinement (prediction-error / max-likelihood fit over the
structural model):

| Protocol | Isolates |
|---|---|
| Constant-velocity sweep, both directions, speed range | Coulomb/viscous/Stribeck + direction asymmetry |
| Slow reversal test | backlash angle + hysteresis loop |
| Locked-output torque ramp | torsional stiffness |
| Torque–speed boundary walk (sustained) | Kt, current/voltage limits, thermal derate |
| Step/chirp response | controller bandwidth, reflected inertia |

**Identifiability is the whole point**: friction, efficiency, and thermal all read as "less torque
than Kt·I" — without separate excitations the fit is degenerate (plausible but meaningless). The
designed protocol turns the dyno from a data source into a measurement instrument.

## The learning flywheel: hierarchical Bayesian with calibrated uncertainty

Structure (partial pooling):

- A part has **features**: motor type/size/Kt/rotor inertia, gearbox type (planetary/harmonic/
  cycloidal — *categorically* different), ratio, stage count, encoder type/location/resolution.
- Each physical **unit** has true params θ. Units of a **SKU** share a distribution around a SKU
  mean (manufacturing variation). SKUs of a **family** share a hyperprior = a regression from
  features → typical params.

The generalized model = the **family feature→parameter-prior regressor + calibrated uncertainty**.
The flywheel:

- **Day 0, new SKU, no data**: predict params from the family regressor on its features, with WIDE
  error bars. The sim runs; the ghost shows a confidence envelope.
- **A few dyno runs**: Bayesian update — family prior + data → tightened SKU posterior (partial
  pooling → 5 measurements go far).
- **Across many SKUs**: the family regressor improves → the *next* new SKU's day-0 prediction
  improves. Compounding return.

**Calibrated uncertainty is the actual product** — the honest answer to "how good is my day-0 sim?"
is a distribution. A residual NN can live here as a *family-shared backbone fine-tuned per SKU*, on
top of the physics, never in place of it.

## Provenance & uncertainty as first-class outputs

Extend the existing `Provenance` enum (`Theoretical`/`GeometryDerived`/`UserSupplied`/
`ManufacturerDerived`) with `FamilyPredicted`/`UnitMeasured`/`Learned`. Every actuator parameter
carries `{value, source, uncertainty}`. The sim uses the best available; the UI shows confidence.

## Runtime integration

`LiveRobot.q` is the single writer, so the actuator model slots in as a **per-joint transfer
function** (command → physics + non-idealities → achieved torque/position) in the sim tick. The
ghost-validity robot extends from "limit-clamped" to "dynamically achievable" (bandwidth/friction
lag, thermal derate) with a confidence band.

## Real-time state observer (closes the loop with live telemetry)

The gray-box model becomes the **process model** of a Kalman/particle filter; live telemetry
(current, output velocity, case temperature) are the **measurements**; the filter estimates hidden
state — winding temperature, position on the derated torque-speed curve, backlash slack currently
taken up. Soft-real-time observer, not an RTOS control loop. KRStudio already has Kalman/filter
nodes (GATE FILTER) to build this from.

## Data schema (extends the parts family)

- `.kmotor` / `.kgearbox` — intrinsic manufacturer params (shipped).
- `.kactmodel` (or extended `.kactuator`) — identified structural params + residual-model ref +
  `{value, source, uncertainty}` per param.
- `.kdyno` — raw characterization runs (the training data), content-addressed + uploaded through
  the existing `krs::parts` repository/cloud.
- Family priors — versioned artifacts trained from the corpus and published back (the flywheel
  substrate is the cloud repository we already scaffolded).
- `publishRobotState` (PropertyCatalog) is the **field** telemetry channel — weaker than dyno data
  (rarely clean reversals/locked-rotor in the field, so it *refines* rather than *identifies*), but
  continuous and free; catches drift/wear over a robot's life.

## Honest hard parts

- **Field observability is limited** — backlash needs reversals, stiffness needs load steps; a
  robot doing its job may never excite them. Dyno = gold instrument; field = refinement + drift.
- **Distribution shift** — models fit on one lab's mounting/thermal env won't perfectly transfer;
  track operating conditions (temp, mounting stiffness, supply voltage) as features.
- **Day-0 extrapolation** — early on the family regressor extrapolates outside measured SKUs;
  uncertainty MUST reflect that (an overconfident prior is worse than an honest wide one).
  Calibration (do the bars contain the truth at the stated rate?) is a day-1 metric.
- **Validation** needs held-out SKUs + a real sim-to-real gap metric on physical robots.

## Phased build (each independently useful, each gateable)

1. **Structural gray-box model + richer schema** — parameterized torque-speed/friction/backlash/
   stiffness, each `{value, provenance, uncertainty}`. Pure physics + manufacturer priors. Gate:
   reproduce a known torque-speed curve + backlash dead-zone from params.
   **✅ DONE (first pass)** — `krs::act` (`include/PhysicsHeaders/ActuatorModel.hpp`): `Param{value,
   prov, uncertainty}` + `ActuatorParams`/`ActuatorState`; torque-speed envelope (flat `Kt·Imax`
   corner → back-EMF droop), Coulomb+viscous+Stribeck friction with direction asymmetry, backlash
   dead-zone, first-order thermal derate; `describeParams` emits the `{value,source,uncertainty}`
   view. Gate `KRS_ACTUATOR_SELFTEST` (`runActuatorModelGate`) with an ideal-params NEG-CTRL.
2. **Runtime integration + confidence ghost** — model as per-joint transfer function; ghost renders
   the uncertainty envelope. Gate: commanded step lags per bandwidth/friction; ghost band brackets.
3. **Characterization pipeline (offline first)** — `.kdyno` records + fitters that decompose
   designed-excitation runs. Validate on SYNTHETIC dyno data (generate with known params, recover
   them within tolerance) before hardware.
4. **Hierarchical model + cloud training loop** — family feature→prior regressor (partial pooling),
   trained on the repository corpus, published back. Gate: held-out-SKU day-0 prediction beats the
   manufacturer-only baseline with calibrated uncertainty.

Steps 1-2 make the sim faithful with hand/manufacturer numbers (valuable alone). Step 3 makes the
dyno meaningful. Step 4 is the flywheel.
