// ActuatorModel.cpp -- docs/ACTUATOR_MODEL.md PHASE 1 forward model + GATE (KRS_ACTUATOR_SELFTEST).
// The structural gray-box: interpretable physics (torque-speed envelope, friction curve, backlash,
// compliance, thermal) with {value, provenance, uncertainty} per parameter. Pure CPU / headless;
// the forward-model fns are pure so this same engine drives both the gate and the Phase-2 per-joint
// transfer function. Only QJson* touches Qt (the schema/telemetry descriptor).

#include "ActuatorModel.hpp"

#include <QJsonObject>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <string>

namespace krs::act {

const char* provStr(Param::Prov p) {
    switch (p) {
        case Param::Prov::Theoretical:           return "theoretical";
        case Param::Prov::ManufacturerDatasheet: return "datasheet";
        case Param::Prov::FamilyPredicted:       return "family-predicted";
        case Param::Prov::UnitMeasured:          return "unit-measured";
        case Param::Prov::Learned:               return "learned";
    }
    return "?";
}

// ---------------------------------------------------------------------------------------------------
double effectiveKt(const ActuatorParams& p, const ActuatorState& s) {
    // Magnet Kt droops linearly with temperature above the calibration point; floor at 0 so a wildly
    // hot state can't make torque go negative (unphysical), it just stops producing.
    const double derate = 1.0 - p.ktThermalCoeff * (s.windingTemp - p.ktCalibTemp);
    return p.kt.value * std::max(0.0, derate);
}

double torqueCeiling(const ActuatorParams& p, const ActuatorState& s, double speed) {
    // Available CURRENT is min(Imax, voltage-limited). The voltage-limited current
    // I_avail = (V - Ke*|speed|)/R = (V - |speed|/speedConst)/R drops with speed as back-EMF grows.
    // Below the corner Imax binds (flat ceiling); above it, I_avail binds and the ceiling droops to 0
    // at the no-load speed V*speedConst. Kt is thermally derated via effectiveKt.
    const double ktTherm = effectiveKt(p, s);
    const double R  = (p.windingResistance > 0.0) ? p.windingResistance : 1e-9;
    const double Ke = (p.speedConst > 0.0) ? 1.0 / p.speedConst : 0.0;   // [V per rad/s]
    const double iAvail = (p.busVoltage - Ke * std::fabs(speed)) / R;    // voltage-limited current [A]
    const double iUsable = std::min(p.iMax.value, std::max(0.0, iAvail));// whichever limit binds
    return std::max(0.0, ktTherm * iUsable);
}

double frictionTorque(const ActuatorParams& p, double speed) {
    // Coulomb + Stribeck hump (extra near zero, decaying as exp(-(v/vs)^2)) + viscous. The dry part
    // OPPOSES motion (sign(speed)); backdriving (v<0) is scaled by dirAsym (worm/harmonic asymmetry).
    // At EXACTLY zero speed there is no defined motion direction -> return 0 (static breakaway is the
    // ceiling's job, not a resistive torque with an arbitrary sign).
    if (speed == 0.0) return 0.0;
    const double vs = (p.stribeckSpeed > 0.0) ? p.stribeckSpeed : 1e-9;
    const double a = speed / vs;
    const double dry = p.coulomb.value + p.stribeck * std::exp(-a * a);  // Coulomb + decaying hump
    const double asym = (speed < 0.0) ? p.dirAsym : 1.0;
    const double sgn = (speed > 0.0) ? 1.0 : -1.0;
    return sgn * asym * dry + p.viscous.value * speed;                  // dry opposes; viscous ~ speed
}

double outputTorque(const ActuatorParams& p, const ActuatorState& s, double commandedTorque, double speed) {
    // Saturate the command to the (thermally-derated, speed-dependent) envelope, then subtract friction.
    const double ceil = torqueCeiling(p, s, speed);
    double t = std::max(-ceil, std::min(ceil, commandedTorque));        // clamp to +/- ceiling
    const double fr = frictionTorque(p, speed);                         // resistive, signed by motion
    // Friction removes |fr| of magnitude opposing motion; it must not push the achieved torque back
    // through zero (friction resists, it doesn't drive). While moving, subtract the motion-opposing
    // component but clamp so a small command swamped by friction reads as 0, not a reversed torque.
    if (speed > 0.0)      t = std::max(0.0, t - std::fabs(fr));
    else if (speed < 0.0) t = std::min(0.0, t + std::fabs(fr));
    // speed==0: no kinetic friction to subtract (breakaway handled elsewhere) -> the clamped command.
    return t;
}

double backlashOutput(const ActuatorParams& p, ActuatorState& s, double inputAngleDelta, int dir) {
    // Symmetric dead-zone of total width `backlash`, slack tracked in [-half, +half]. `dir` (+1/-1) is
    // the driving direction of this step. On a same-direction step already engaged, output tracks 1:1.
    // On reversal the slack must first traverse the dead-zone to the other wall; only the OVERSHOOT
    // past the wall reaches the output.
    const double half = std::max(0.0, 0.5 * p.backlash.value);
    if (half <= 0.0) return inputAngleDelta;                            // no backlash -> rigid 1:1

    const double mag = std::fabs(inputAngleDelta);
    const double d = (dir >= 0) ? 1.0 : -1.0;
    const double wall = d * half;                                       // the wall we're driving toward
    const double slackToWall = std::max(0.0, d * (wall - s.backlashTakenUp)); // distance still in the gap
    const double consume = std::min(mag, slackToWall);                  // eaten by the dead-zone
    s.backlashTakenUp += d * consume;                                   // advance slack toward the wall
    s.backlashTakenUp = std::max(-half, std::min(half, s.backlashTakenUp));
    const double output = mag - consume;                               // only the overshoot moves output
    return d * output;
}

void stepThermal(const ActuatorParams& p, ActuatorState& s, double current, double dt, double ambient) {
    // First-order lumped winding: C*dT/dt = I^2*R (Joule heating) - G*(T - ambient) (dissipation).
    const double C = (p.thermalMass > 0.0) ? p.thermalMass : 1e-9;
    const double heating = current * current * p.windingResistance;     // [W]
    const double cooling = p.thermalDissipation * (s.windingTemp - ambient); // [W]
    s.windingTemp += (heating - cooling) * dt / C;
    // effectiveKt/torqueCeiling read the updated windingTemp on the next call -> the derate follows.
}

// ---------------------------------------------------------------------------------------------------
QJsonObject describeParams(const ActuatorParams& p) {
    auto param = [](const Param& v) {
        QJsonObject o;
        o["value"] = v.value;
        o["source"] = provStr(v.prov);
        o["uncertainty"] = v.uncertainty;
        return o;
    };
    QJsonObject o;
    o["kt"] = param(p.kt);
    o["iMax"] = param(p.iMax);
    o["coulomb"] = param(p.coulomb);
    o["viscous"] = param(p.viscous);
    o["backlash"] = param(p.backlash);
    o["stiffness"] = param(p.stiffness);
    // plain-double structural constants (no per-unit uncertainty tracked yet) as bare numbers.
    o["speedConst"] = p.speedConst;
    o["busVoltage"] = p.busVoltage;
    o["stribeck"] = p.stribeck;
    o["stribeckSpeed"] = p.stribeckSpeed;
    o["dirAsym"] = p.dirAsym;
    o["windingResistance"] = p.windingResistance;
    o["thermalMass"] = p.thermalMass;
    o["thermalDissipation"] = p.thermalDissipation;
    o["ktThermalCoeff"] = p.ktThermalCoeff;
    return o;
}

// ---------------------------------------------------------------------------------------------------
bool runActuatorModelGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[act] GATE ACTUATOR-MODEL -- torque-speed envelope + friction/Stribeck + backlash + thermal derate\n");
    bool pass = true;

    ActuatorParams P;                          // realistic defaults (datasheet-tagged)
    const ActuatorState cold;                  // 25 degC, no slack

    // --- (1) torqueCeiling saturates: FLAT (== Kt*Imax) at low speed, DROOPS at high speed --------
    {
        const double stall = P.kt.value * P.iMax.value;                 // cold, current-limited ceiling
        const double noLoad = P.speedConst * P.busVoltage;              // 720 rad/s for the defaults
        const double cLow0 = torqueCeiling(P, cold, 0.0);
        const double cLowN = torqueCeiling(P, cold, -100.0);           // well below the corner, still flat
        const double cHi  = torqueCeiling(P, cold, 0.98 * noLoad);      // deep in the voltage-limited droop
        const double cTop = torqueCeiling(P, cold, noLoad);            // at no-load -> ~0
        const bool flatLow = std::fabs(cLow0 - stall) < 1e-9 && std::fabs(cLowN - stall) < 1e-9;
        const bool droops  = cHi < stall - 1e-6 && cHi > cTop + 1e-9 && cTop < 1e-6;  // strictly between
        const bool symmetric = std::fabs(torqueCeiling(P, cold, 400.0) - torqueCeiling(P, cold, -400.0)) < 1e-12;
        const bool ok = flatLow && droops && symmetric;
        printf("[act]   (1) ceiling: flat@low=%.3f (==Kt*Imax %.3f) both dirs; droop@0.98-noload=%.3f<stall, @no-load=%.4f~0; |speed|-symmetric  %s\n",
               cLow0, stall, cHi, cTop, ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // --- (2) frictionTorque: Stribeck dip near zero + direction asymmetry -------------------------
    {
        // |friction| just off zero (Coulomb+Stribeck hump) exceeds |friction| at a moderate speed where
        // the hump has decayed to ~Coulomb (before viscous piles back on) -> the dip.
        const double near = std::fabs(frictionTorque(P, +0.01));        // ~Coulomb+Stribeck
        const double mid  = std::fabs(frictionTorque(P, +1.5 * P.stribeckSpeed)); // hump mostly gone
        const bool stribeckDip = near > mid + 1e-6 && near > P.coulomb.value;
        // static point returns exactly 0 (no arbitrary-sign resistive torque at rest).
        const bool zeroAtRest = frictionTorque(P, 0.0) == 0.0;
        // direction asymmetry: backdriving (v<0) is HARDER by dirAsym than driving (v>0), same |v|.
        const double fFwd = std::fabs(frictionTorque(P, +0.2));
        const double fBwd = std::fabs(frictionTorque(P, -0.2));
        const bool asym = fBwd > fFwd + 1e-6;
        // opposes motion: sign(friction) matches sign(speed) for the dry-dominated small-speed regime.
        const bool opposes = frictionTorque(P, +0.05) > 0.0 && frictionTorque(P, -0.05) < 0.0;
        const bool ok = stribeckDip && zeroAtRest && asym && opposes;
        printf("[act]   (2) friction: |near0|=%.3f > |mid|=%.3f (Stribeck dip), rest==0, backdrive %.3f > drive %.3f (asym x%.2f), opposes motion  %s\n",
               near, mid, fBwd, fFwd, P.dirAsym, ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // --- (3) backlashOutput: dead-zone on reversal, then 1:1 --------------------------------------
    {
        ActuatorState s;                        // slack starts at 0 (mid-gap)
        const double half = 0.5 * P.backlash.value;
        // drive forward to seat the far wall: first `half` is eaten, rest tracks 1:1.
        const double step = 0.010;              // > backlash so we clear the gap in one step
        const double outFwd = backlashOutput(P, s, step, +1);
        const bool seatedFwd = std::fabs(s.backlashTakenUp - half) < 1e-9;
        const bool fwdTracks = std::fabs(outFwd - (step - half)) < 1e-9;
        // continue forward, already engaged -> pure 1:1, no loss.
        const double outFwd2 = backlashOutput(P, s, 0.004, +1);
        const bool fwdEngaged11 = std::fabs(outFwd2 - 0.004) < 1e-12;
        // REVERSE: from the +half wall, crossing the full 2*half gap is needed to reach the -half wall.
        // A reversal step of exactly `half` only gets HALFWAY across (slack -> 0) -> output does NOT move.
        const double outRevDead = backlashOutput(P, s, half, -1);
        const bool midGapNow = std::fabs(s.backlashTakenUp) < 1e-9;    // slack back at mid-gap
        const bool deadOnReversal = std::fabs(outRevDead) < 1e-9 && midGapNow;
        // continue reversing past the near wall -> only the OVERSHOOT (step - remaining half) reaches
        // the output, and it comes out NEGATIVE (driving in -dir).
        const double revStep = 0.006;
        const double outRevMove = backlashOutput(P, s, revStep, -1);
        const bool seatedRev = std::fabs(s.backlashTakenUp + half) < 1e-9;             // now at -half wall
        const bool revTracks = std::fabs(outRevMove - (-(revStep - half))) < 1e-9;     // overshoot, -dir
        // once seated at -half, further reverse motion tracks 1:1 (no more dead-zone to eat).
        const double outRevEngaged = backlashOutput(P, s, 0.003, -1);
        const bool revEngaged11 = std::fabs(outRevEngaged - (-0.003)) < 1e-12;
        const bool ok = seatedFwd && fwdTracks && fwdEngaged11 && deadOnReversal
                      && seatedRev && revTracks && revEngaged11;
        printf("[act]   (3) backlash: fwd out=%.4f (=step-half), engaged 1:1=%.4f; reversal dead-zone out=%.2e (~0), overshoot=%.4f (=-(step-half)), then reverse 1:1=%.4f  %s\n",
               outFwd, outFwd2, outRevDead, outRevMove, outRevEngaged, ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // --- (4) stepThermal: sustained current RAISES windingTemp AND DERATES the ceiling ------------
    {
        ActuatorState s;                        // 25 degC
        const double ceil0 = torqueCeiling(P, s, 0.0);
        const double I = P.iMax.value;          // hold at the current limit
        double prevT = s.windingTemp; bool monotonicUp = true;
        for (int i = 0; i < 2000; ++i) {        // 2000 * 5ms = 10 s of sustained load
            stepThermal(P, s, I, 0.005, 25.0);
            if (s.windingTemp < prevT - 1e-12) monotonicUp = false;     // heating phase is monotone up
            prevT = s.windingTemp;
        }
        const double ceilHot = torqueCeiling(P, s, 0.0);
        const bool heated = s.windingTemp > 26.0 && monotonicUp;
        const bool derated = ceilHot < ceil0 - 1e-6;
        // NEG-CTRL for the thermal path: with ktThermalCoeff==0 the SAME heating does NOT derate.
        ActuatorParams Pnoderate = P; Pnoderate.ktThermalCoeff = 0.0;
        const bool noDerateWhenCoeff0 =
            std::fabs(torqueCeiling(Pnoderate, s, 0.0) - torqueCeiling(Pnoderate, cold, 0.0)) < 1e-9;
        const bool ok = heated && derated && noDerateWhenCoeff0;
        printf("[act]   (4) thermal: T %.1f->%.1f degC (monotone up), ceiling %.3f->%.3f (derated); NEG coeff=0 -> no derate  %s\n",
               25.0, s.windingTemp, ceil0, ceilHot, ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // --- (5) provenance + uncertainty round-trip on a Param ---------------------------------------
    {
        Param q; q.value = 0.123; q.prov = Param::Prov::UnitMeasured; q.uncertainty = 0.004;
        const bool rt = q.value == 0.123 && q.prov == Param::Prov::UnitMeasured
                      && q.uncertainty == 0.004 && std::string(provStr(q.prov)) == "unit-measured";
        // the descriptor carries it through to the schema/telemetry view unchanged.
        QJsonObject d = describeParams(P);
        const QJsonObject ktObj = d["kt"].toObject();
        const bool descOk = std::fabs(ktObj["value"].toDouble() - P.kt.value) < 1e-12
                          && ktObj["source"].toString() == "datasheet"
                          && std::fabs(ktObj["uncertainty"].toDouble() - P.kt.uncertainty) < 1e-12;
        const bool ok = rt && descOk;
        printf("[act]   (5) provenance: Param{v=%.3f, prov=%s, sigma=%.3f} round-trips; describeParams.kt carries {value,source,uncertainty}  %s\n",
               q.value, provStr(q.prov), q.uncertainty, ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // --- NEG-CTRL: IDEAL params (zero friction, zero backlash) reproduce the ideal actuator -------
    // torque == command clamped to the ceiling (no friction loss), and NO dead-zone (rigid 1:1).
    {
        ActuatorParams I;
        I.coulomb.value = 0.0; I.viscous.value = 0.0; I.stribeck = 0.0; I.dirAsym = 1.0;
        I.backlash.value = 0.0;
        ActuatorState s;
        const double v = 2.0;
        const double ceil = torqueCeiling(I, s, v);
        // command WELL inside the ceiling -> achieved == command exactly (no friction subtracted).
        const double cmdIn = 0.4 * ceil;
        const double achIn = outputTorque(I, s, cmdIn, v);
        const bool noLoss = std::fabs(achIn - cmdIn) < 1e-12;
        // command ABOVE the ceiling -> clamped to exactly the ceiling.
        const double achSat = outputTorque(I, s, 10.0 * ceil, v);
        const bool clamps = std::fabs(achSat - ceil) < 1e-12;
        // zero friction everywhere.
        const bool zeroFric = frictionTorque(I, 0.01) == 0.0 && frictionTorque(I, -3.0) == 0.0;
        // zero backlash -> rigid pass-through, no dead-zone even on reversal.
        ActuatorState sb;
        const bool rigid = std::fabs(backlashOutput(I, sb, 0.01, +1) - 0.01) < 1e-12
                         && std::fabs(backlashOutput(I, sb, -0.02, -1) - (-0.02)) < 1e-12;
        // DISCRIMINATION: the REALISTIC params do NOT satisfy the ideal identities (loss + dead-zone
        // are real), so this neg-ctrl has teeth.
        ActuatorState sr;
        const double realAch = outputTorque(P, cold, 0.4 * torqueCeiling(P, cold, v), v);
        const bool realHasLoss = realAch < 0.4 * torqueCeiling(P, cold, v) - 1e-6;
        const double realBack = backlashOutput(P, sr, 0.5 * P.backlash.value, +1); // starts mid-gap
        const bool realHasDeadzone = std::fabs(realBack) < 1e-9;                    // < half -> no output yet
        const bool ok = noLoss && clamps && zeroFric && rigid && realHasLoss && realHasDeadzone;
        printf("[act]   NEG-CTRL ideal: torque==cmd (%.3f==%.3f), clamps to ceiling (%.3f), zero friction, rigid 1:1; real params DO lose+dead-zone  %s\n",
               achIn, cmdIn, ceil, ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    printf("[act] %s\n", pass
        ? "ALL PASS (envelope saturates+droops; Stribeck dip + direction asymmetry; backlash dead-zone->1:1; thermal derate; provenance round-trips; ideal neg-ctrl)"
        : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::act
