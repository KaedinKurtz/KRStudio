#pragma once
// ActuatorModel.hpp -- docs/ACTUATOR_MODEL.md PHASE 1: the structural gray-box actuator model.
// The primary representation is a PHYSICS model with interpretable parameters (NOT a black-box net):
// a torque-speed envelope, a Coulomb+viscous+Stribeck friction curve (direction-asymmetric), a
// backlash dead-zone, torsional compliance, and a small thermal state. Every structural parameter
// carries {value, provenance, uncertainty} so the sim can use the best available number and the UI
// can show confidence. Pure CPU, headless; forward-model fns are pure & deterministic so the same
// engine feeds both the gate and the (Phase-2) per-joint transfer function.
//
// Sign convention: `speed` is the OUTPUT-shaft angular velocity [rad/s]; torques are [Nm] at the
// output; positive speed = the "forward" driving direction. Thermal is winding-side.

#include <QJsonObject>   // telemetry/schema descriptor only (QJson* is the one Qt allowance here)

namespace krs::act {

// ---------------------------------------------------------------------------------------------------
// A parameter value WITH provenance + uncertainty (Phase-1 extends the shipped Provenance ladder --
// Theoretical/ManufacturerDatasheet at day 0, then FamilyPredicted, then UnitMeasured/Learned as the
// flywheel turns). `uncertainty` is a 1-sigma in the value's own units; the sim uses `value`, the UI
// shows the band. Kept a plain struct (aggregate-initializable) so params read like data.
struct Param {
    double value = 0.0;
    enum class Prov { Theoretical, ManufacturerDatasheet, FamilyPredicted, UnitMeasured, Learned };
    Prov   prov  = Prov::Theoretical;
    double uncertainty = 0.0;   // 1-sigma, same units as value
};

const char* provStr(Param::Prov p);   // stable short tag for logs/telemetry ("theoretical", ...)

// ---------------------------------------------------------------------------------------------------
// The interpretable structural parameters. Physically-motivated, each independently identifiable by a
// designed dyno excitation (see the doc's protocol table). Params that a user reads/edits or that
// carry real measurement uncertainty are Param; a few pure-scaling constants stay plain double.
struct ActuatorParams {
    // --- Torque-speed envelope: at low speed the CURRENT limit binds so the ceiling is flat at
    //     Kt*Imax; past a corner the back-EMF (Ke*speed) eats the bus so the available current
    //     I_avail = (V - Ke*|speed|)/R falls, and the ceiling droops to 0 at no-load speed V*speedConst.
    //     `speedConst` [rad/s per V] = 1/Ke; the corner is where I_avail crosses Imax. ---
    Param  kt   { 0.10, Param::Prov::ManufacturerDatasheet, 0.005 };   // torque constant [Nm/A]
    Param  iMax { 20.0, Param::Prov::ManufacturerDatasheet, 0.5   };   // current limit [A]
    double speedConst = 30.0;   // motor speed constant [rad/s per V] (1/Ke, output-referred)
    double busVoltage = 24.0;   // available bus voltage [V]

    // --- Friction curve: tau_f(speed) = [ Coulomb + (Stribeck-Coulomb) * exp(-(|v|/vs)^2) ] * sign(v)
    //     + viscous*v ; scaled by dirAsym for backdriving (negative speed). Stribeck is the near-zero
    //     "stiction" hump above Coulomb; it decays with speed to leave Coulomb+viscous. ---
    Param  coulomb  { 0.30, Param::Prov::ManufacturerDatasheet, 0.03 };  // dry friction floor [Nm]
    Param  viscous  { 0.02, Param::Prov::ManufacturerDatasheet, 0.005 }; // [Nm/(rad/s)]
    double stribeck = 0.20;     // EXTRA torque at zero speed above Coulomb [Nm] (the dip's height)
    double stribeckSpeed = 0.5; // speed scale of the Stribeck decay [rad/s] (>0)
    double dirAsym = 1.30;      // backdrive (v<0) friction / drive (v>0) friction (worm/harmonic asym)

    // --- Backlash + compliance ---
    Param  backlash  { 0.0035, Param::Prov::ManufacturerDatasheet, 0.0005 }; // total dead-zone [rad]
    Param  stiffness { 8000.0, Param::Prov::ManufacturerDatasheet, 500.0  }; // torsional [Nm/rad]

    // --- Thermal: I^2*R heats the winding; dissipation pulls it toward ambient; hotter winding droops
    //     Kt (ktThermalCoeff, fractional per degC above the nominal calibration temp). ---
    double windingResistance = 0.15;    // [Ohm]
    double thermalMass       = 40.0;    // winding heat capacity [J/degC]
    double thermalDissipation = 0.8;    // [W/degC] winding->ambient conductance
    double ktThermalCoeff    = 0.004;   // fractional Kt droop per degC (e.g. 0.4%/degC for NdFeB)
    double ktCalibTemp       = 25.0;    // temp at which `kt.value` is the nominal [degC]
};

// ---------------------------------------------------------------------------------------------------
// Hidden state advanced over time (path-dependent -> state, not param). windingTemp derates the
// ceiling; backlashTakenUp tracks how much of the dead-zone slack is currently consumed and its sign
// tells which side we're engaged on (the reversal detector).
struct ActuatorState {
    double windingTemp = 25.0;      // [degC]
    double backlashTakenUp = 0.0;   // signed slack consumed [rad], in [-backlash/2, +backlash/2]
};

// ---------------------------------------------------------------------------------------------------
// FORWARD MODEL (pure, deterministic).

// Thermally-derated Kt at the state's winding temp: kt * (1 - ktThermalCoeff*(T - Tcalib)), floored >=0.
double effectiveKt(const ActuatorParams& p, const ActuatorState& s);

// Envelope ceiling at a given output |speed|: min(current-limited Kt*Imax, voltage/back-EMF limit),
// thermally derated by the state temp. Flat (== Kt*Imax) up to the corner, then falls to 0 at no-load.
double torqueCeiling(const ActuatorParams& p, const ActuatorState& s, double speed);

// Friction torque OPPOSING motion at `speed`: Coulomb+viscous+Stribeck, direction-asymmetric.
double frictionTorque(const ActuatorParams& p, double speed);

// Achieved output torque: saturate the command to +/- ceiling, then subtract the load-independent
// friction (friction can't reverse the sign of a torque it merely resists -> clamped at zero-cross).
double outputTorque(const ActuatorParams& p, const ActuatorState& s, double commandedTorque, double speed);

// Backlash dead-zone: advance the taken-up slack by inputAngleDelta in direction `dir` (+1/-1); the
// output stays put until the slack is fully consumed on the new side, then tracks the input 1:1.
// Returns the OUTPUT angle delta this step. Mutates s.backlashTakenUp.
double backlashOutput(const ActuatorParams& p, ActuatorState& s, double inputAngleDelta, int dir);

// One thermal step: winding heats by I^2*R*dt/C, cools toward ambient by dissipation*(T-amb)*dt/C.
// Mutates s.windingTemp (which torqueCeiling then reads to derate Kt).
void stepThermal(const ActuatorParams& p, ActuatorState& s, double current, double dt, double ambient = 25.0);

// ---------------------------------------------------------------------------------------------------
// Telemetry / schema descriptor -- the {value, source, uncertainty}-per-param view the .kactmodel
// schema and the UI confidence band consume. QJson* only (the doc's telemetry allowance).
QJsonObject describeParams(const ActuatorParams& p);

// ---------------------------------------------------------------------------------------------------
// GATE (env KRS_ACTUATOR_SELFTEST; in the bench). Checks: (1) torqueCeiling saturates then droops;
// (2) frictionTorque has the near-zero Stribeck dip + direction asymmetry; (3) backlash dead-zone on
// reversal then 1:1 tracking; (4) sustained current raises windingTemp AND derates the ceiling;
// (5) Param provenance+uncertainty round-trip; NEG-CTRL: ideal params reproduce command-clamped-to-
// ceiling with no dead-zone. Returns true iff all pass.
bool runActuatorModelGate();

} // namespace krs::act
