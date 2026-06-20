// ImuExtrinsicsGate.cpp -- BLIND IMU EXTRINSIC RECOVERY gates. Pure math/physics,
// gated against SEALED ground truth (the recovery never sees the true link/mount).
//   IMU-MODEL-CORRECT      noiseless readings match closed-form (constant-rate rotation:
//                          gyro=R_mount^T w0 z, centripetal=w0^2 L); leverless model FAILS.
//   INFORMATION-BARRIER    recovery matches truth from readings+joint-states alone, and
//                          FAILS on zeroed/no-motion input (proving it uses the physics).
//   EXCITATION-OBSERVABILITY  each of the 6 mount DOF measurably changes the readings under
//                          the excitation; a non-rotating (prismatic-only) excitation leaves
//                          POSITION under-observable.
//   BLIND-RECOVERY/NOISE-ROBUST  recovered mount matches the sealed truth <tol; identity
//                          model FAILS; under noise the estimate stays centred (a no-bias
//                          model is shifted off-centre).
//   HUNDREDS-OF-TRIALS     over hundreds of random link+pose trials: success rate + error
//                          distribution + honest failure characterisation (small lever arm).

#include "ImuExtrinsics.hpp"

#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

namespace krs::imu {
namespace {

const Eigen::Vector3d kG(0.0, 0.0, -9.81);

void addRev(krs::dyn::SerialChain& c, int parent, const Eigen::Vector3d& axis, const Eigen::Vector3d& off) {
    krs::dyn::DynBody b; b.mass = 1.0; b.inertiaCom = Eigen::Matrix3d::Identity();
    krs::dyn::DynJoint j; j.type = krs::dyn::JType::Revolute; j.parent = parent; j.axis = axis; j.ptree = off;
    c.addBody(j, b);
}

// a 6-DOF arm with compound rotation axes (so deeper links see rich, multi-axis motion).
void buildArm6(krs::dyn::SerialChain& c) {
    addRev(c, -1, Eigen::Vector3d(0,0,1), Eigen::Vector3d(0,0,0));     // b0 base yaw
    addRev(c,  0, Eigen::Vector3d(0,1,0), Eigen::Vector3d(0,0,0.30));  // b1 shoulder pitch
    addRev(c,  1, Eigen::Vector3d(0,1,0), Eigen::Vector3d(0.30,0,0));  // b2 elbow pitch
    addRev(c,  2, Eigen::Vector3d(1,0,0), Eigen::Vector3d(0.30,0,0));  // b3 roll
    addRev(c,  3, Eigen::Vector3d(0,1,0), Eigen::Vector3d(0.20,0,0));  // b4 wrist pitch
    addRev(c,  4, Eigen::Vector3d(0,0,1), Eigen::Vector3d(0.15,0,0));  // b5 wrist yaw
}

Excitation defaultExcitation(int nq, double T = 2.5) {
    Excitation ex; ex.dt = 1e-3; ex.T = T; ex.gravity = kG;
    ex.q0 = Eigen::VectorXd::Zero(nq);
    ex.amp = Eigen::VectorXd::Constant(nq, 0.6);
    ex.freq = Eigen::VectorXd(nq); ex.phase = Eigen::VectorXd(nq);
    for (int j = 0; j < nq; ++j) { ex.freq[j] = 0.5 + 0.23 * j; ex.phase[j] = 0.4 * j; }
    return ex;
}

double streamRms(const ImuStream& a, const ImuStream& b) {
    const int M = std::min(a.samples.size(), b.samples.size());
    double s = 0; for (int j = 0; j < M; ++j)
        s += (a.samples[j].acc - b.samples[j].acc).squaredNorm() + (a.samples[j].gyro - b.samples[j].gyro).squaredNorm();
    return M ? std::sqrt(s / M) : 0.0;
}

} // namespace

// ===========================================================================
bool runImuModelGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[imu] GATE IMU-MODEL-CORRECT -- noiseless readings match closed-form; lever arm affects readings\n");

    // 1-DOF link rotating at constant rate w0 about Z; IMU at lever arm L on +X.
    krs::dyn::SerialChain chain; addRev(chain, -1, Eigen::Vector3d(0,0,1), Eigen::Vector3d(0,0,0));
    const double w0 = 1.7, L = 0.25, dt = 1e-3; const int N = 800;
    JointStates js; js.dt = dt;
    for (int i = 0; i < N; ++i) { Eigen::VectorXd q(1); q[0] = w0 * i * dt; js.q.push_back(q); }
    Mount m; m.p = Eigen::Vector3d(L, 0, 0);
    m.R = Eigen::AngleAxisd(0.6, Eigen::Vector3d(0.3,0.4,0.866).normalized()).toRotationMatrix();
    const LinkKin k = computeKinematics(chain, js, 0);

    // centripetal magnitude (gravity off) must equal w0^2 L; gyro magnitude = w0, dir = R_mount^T z.
    const ImuStream sCorrect = predictReadings(k, m, Eigen::Vector3d::Zero(), true);
    const ImuStream sLeverless = predictReadings(k, m, Eigen::Vector3d::Zero(), false);
    const Eigen::Vector3d gyroExpectDir = (m.R.transpose() * Eigen::Vector3d(0,0,1)).normalized();
    double accMag = 0, gyroMag = 0, gyroDirDot = 0, leverlessAccMag = 0; int n = k.size();
    for (int j = 0; j < n; ++j) {
        accMag += sCorrect.samples[j].acc.norm();
        gyroMag += sCorrect.samples[j].gyro.norm();
        gyroDirDot += sCorrect.samples[j].gyro.normalized().dot(gyroExpectDir);
        leverlessAccMag += sLeverless.samples[j].acc.norm();
    }
    accMag /= n; gyroMag /= n; gyroDirDot /= n; leverlessAccMag /= n;
    const double expectAcc = w0 * w0 * L;
    const bool centripetalOk = std::abs(accMag - expectAcc) < 1e-3 * expectAcc;
    const bool gyroOk = std::abs(gyroMag - w0) < 1e-3 * w0 && gyroDirDot > 0.9999;
    const bool leverlessFails = leverlessAccMag < 1e-3 * expectAcc;     // position has NO effect -> centripetal 0

    // sensed gravity rotates with mount orientation (static link, gravity on).
    JointStates stat; stat.dt = dt; for (int i = 0; i < 200; ++i) { Eigen::VectorXd q(1); q[0] = 0; stat.q.push_back(q); }
    const LinkKin ks = computeKinematics(chain, stat, 0);
    Mount mA = m, mB; mB.p = m.p; mB.R = Eigen::AngleAxisd(1.2, Eigen::Vector3d(0,1,0)).toRotationMatrix();
    const Eigen::Vector3d gA = predictReadings(ks, mA, kG, true).samples[0].acc;
    const Eigen::Vector3d gB = predictReadings(ks, mB, kG, true).samples[0].acc;
    const bool gravityMag = std::abs(gA.norm() - 9.81) < 1e-3 && std::abs(gB.norm() - 9.81) < 1e-3;
    const bool gravityRotates = (gA - gB).norm() > 1.0;                 // different mount -> different sensed gravity

    const bool pass = centripetalOk && gyroOk && leverlessFails && gravityMag && gravityRotates;
    printf("[imu]   centripetal |acc|=%.5f vs w0^2*L=%.5f  %s ; gyro |w|=%.5f (w0=%.3f) dir.dot=%.6f  %s\n",
           accMag, expectAcc, centripetalOk ? "ok" : "BAD", gyroMag, w0, gyroDirDot, gyroOk ? "ok" : "BAD");
    printf("[imu]   sensed gravity |g|=%.4f rotates-with-mount |dA-dB|=%.4f  %s\n",
           gA.norm(), (gA - gB).norm(), (gravityMag && gravityRotates) ? "ok" : "BAD");
    printf("[imu]   NEG-CTRL leverless model centripetal |acc|=%.3e (must be ~0 -> position would be unrecoverable)  %s\n",
           leverlessAccMag, leverlessFails ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[imu] %s\n", pass ? "ALL PASS (IMU model matches closed-form; lever arm makes position affect the readings)"
                              : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
bool runInfoBarrierGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[imu] GATE INFORMATION-BARRIER -- recovery uses readings+joint-states ONLY; FAILS on garbage input\n");

    krs::dyn::SerialChain chain; buildArm6(chain);
    const Excitation ex = defaultExcitation(chain.nq());
    ImuNoise noise; noise.accelNoise = 0.01; noise.gyroNoise = 0.001;
    std::mt19937 rng(20260620u);

    // Garbage (zeroed/no-motion) input drives the recovery to the DEGENERATE fixed point
    // {R=I, p=0} (no signal to fit). So a threshold "garbage != truth" check would be fragile
    // iff the TRUE mount itself sat near {I,0} (zero lever arm + ~aligned -- a degenerate,
    // measure-zero pose). We therefore demonstrate the barrier over SEVERAL trials whose truth
    // is provably FAR from {I,0} (rotation >= 45deg AND |p| >= 0.15m): the degenerate garbage
    // recovery is then unambiguously distinguishable, robustly (not seed-luck).
    const Mount kIdentity;   // {R=I, p=0}
    const int K = 8;
    int realOk = 0, zeroFails = 0, statFails = 0;
    double realPosW = 0, realRotW = 0, zeroPosMin = 1e9, zeroRotMin = 1e9, statPosMin = 1e9, statRotMin = 1e9;
    for (int t = 0; t < K; ++t) {
        SealedTrial tr;
        do { tr = generateTrial(chain, ex, noise, rng); }
        while (rotErrorDeg(tr.mount, kIdentity) < 45.0 || tr.mount.p.norm() < 0.15);   // non-degenerate truth

        const Recovered real = recoverMount(chain, tr.joints, tr.readings, ex.gravity);
        const double rp = posErrorM(real.mount, tr.mount), rr = rotErrorDeg(real.mount, tr.mount);
        if (real.link == tr.link && rp < 0.01 && rr < 1.0) ++realOk;
        realPosW = std::max(realPosW, rp); realRotW = std::max(realRotW, rr);

        ImuStream zero = tr.readings; for (auto& s : zero.samples) { s.acc.setZero(); s.gyro.setZero(); }
        const Recovered g0 = recoverMount(chain, tr.joints, zero, ex.gravity);
        const double zp = posErrorM(g0.mount, tr.mount), zr = rotErrorDeg(g0.mount, tr.mount);
        if (zp > 0.1 || zr > 20.0) ++zeroFails;
        zeroPosMin = std::min(zeroPosMin, zp); zeroRotMin = std::min(zeroRotMin, zr);

        JointStates statJs; statJs.dt = tr.joints.dt;
        for (size_t i = 0; i < tr.joints.q.size(); ++i) statJs.q.push_back(Eigen::VectorXd::Zero(chain.nq()));
        const Recovered gS = recoverMount(chain, statJs, tr.readings, ex.gravity);
        const double sp = posErrorM(gS.mount, tr.mount), sr = rotErrorDeg(gS.mount, tr.mount);
        if (sp > 0.1 || sr > 20.0) ++statFails;
        statPosMin = std::min(statPosMin, sp); statRotMin = std::min(statRotMin, sr);
    }

    const bool pass = realOk == K && zeroFails == K && statFails == K;
    printf("[imu]   REAL recovery from sealed package: %d/%d matched (worst posErr=%.4fm rotErr=%.3fdeg)  %s\n",
           realOk, K, realPosW, realRotW, realOk == K ? "PASS" : "FAIL");
    printf("[imu]   NEG-CTRL zeroed readings: %d/%d fail to match (min posErr=%.3fm rotErr=%.1fdeg over trials)  %s\n",
           zeroFails, K, zeroPosMin, zeroRotMin, zeroFails == K ? "REJECTS(non-vacuous)" : "VACUOUS! barrier LEAKS");
    printf("[imu]   NEG-CTRL no-motion joint-states: %d/%d fail to match (min posErr=%.3fm rotErr=%.1fdeg)  %s\n",
           statFails, K, statPosMin, statRotMin, statFails == K ? "REJECTS(non-vacuous)" : "VACUOUS! barrier LEAKS");
    printf("[imu] %s\n", pass ? "ALL PASS (recovery solves from the physics; provably fails without the real readings)"
                              : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
bool runExcitationObservGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[imu] GATE EXCITATION-OBSERVABILITY -- all 6 mount DOF observable; degenerate excitation leaves position under-observable\n");

    krs::dyn::SerialChain chain; buildArm6(chain);
    const Excitation ex = defaultExcitation(chain.nq());
    const int link = 3;
    Mount m; m.p = Eigen::Vector3d(0.12, -0.08, 0.06);
    m.R = Eigen::AngleAxisd(0.7, Eigen::Vector3d(0.2,0.5,0.84).normalized()).toRotationMatrix();
    const JointStates js = sineExcitation(chain.nq(), ex);
    const LinkKin k = computeKinematics(chain, js, link);
    const ImuStream base = predictReadings(k, m, ex.gravity, true);

    // perturb each of the 6 DOF; the readings must measurably change.
    const double dp = 0.01, dth = 2.0 * kPi / 180.0;
    double sens[6];
    for (int a = 0; a < 3; ++a) { Mount mp = m; mp.p[a] += dp; sens[a] = streamRms(predictReadings(k, mp, ex.gravity, true), base); }
    for (int a = 0; a < 3; ++a) { Mount mr = m; Eigen::Vector3d ax = Eigen::Vector3d::Unit(a);
        mr.R = Eigen::AngleAxisd(dth, ax).toRotationMatrix() * m.R; sens[3 + a] = streamRms(predictReadings(k, mr, ex.gravity, true), base); }
    bool all6 = true; for (int a = 0; a < 6; ++a) if (sens[a] < 1e-4) all6 = false;

    // DEGENERATE: a prismatic base joint moved alone -> the link translates without rotating
    // -> the lever arm (position) leaves no signature.
    krs::dyn::SerialChain pc;
    { krs::dyn::DynBody b; b.mass = 1; b.inertiaCom = Eigen::Matrix3d::Identity();
      krs::dyn::DynJoint j0; j0.type = krs::dyn::JType::Prismatic; j0.parent = -1; j0.axis = Eigen::Vector3d(1,0,0); pc.addBody(j0, b);
      krs::dyn::DynJoint j1; j1.type = krs::dyn::JType::Revolute;  j1.parent = 0;  j1.axis = Eigen::Vector3d(0,0,1); j1.ptree = Eigen::Vector3d(0.2,0,0); pc.addBody(j1, b); }
    Excitation dex = defaultExcitation(pc.nq(), 2.5);
    dex.amp[0] = 0.5; dex.amp[1] = 0.0;            // move ONLY the prismatic; freeze the revolute
    const JointStates djs = sineExcitation(pc.nq(), dex);
    const LinkKin dk = computeKinematics(pc, djs, 1);
    const ImuStream dbase = predictReadings(dk, m, dex.gravity, true);
    double degPosSens = 0; for (int a = 0; a < 3; ++a) { Mount mp = m; mp.p[a] += dp; degPosSens += streamRms(predictReadings(dk, mp, dex.gravity, true), dbase); }
    degPosSens /= 3.0;
    double richPosSens = (sens[0] + sens[1] + sens[2]) / 3.0;
    const bool degenerateUnderObs = degPosSens < 0.02 * richPosSens;   // position signature collapses without rotation

    const bool pass = all6 && degenerateUnderObs;
    printf("[imu]   per-DOF sensitivity (px py pz rx ry rz): %.3e %.3e %.3e %.3e %.3e %.3e ; all>1e-4 %s\n",
           sens[0], sens[1], sens[2], sens[3], sens[4], sens[5], all6 ? "PASS" : "FAIL");
    printf("[imu]   NEG-CTRL degenerate (non-rotating) excitation: position sensitivity %.3e vs rich %.3e (ratio %.4f<0.02 -> under-observable)  %s\n",
           degPosSens, richPosSens, richPosSens > 0 ? degPosSens / richPosSens : 0.0,
           degenerateUnderObs ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[imu] %s\n", pass ? "ALL PASS (rich excitation makes all 6 DOF observable; rotation reveals the lever arm)"
                              : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
bool runBlindRecoveryGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[imu] GATE BLIND-RECOVERY + NOISE-ROBUST -- recovered mount matches sealed truth; estimate centred under noise\n");

    krs::dyn::SerialChain chain; buildArm6(chain);
    const Excitation ex = defaultExcitation(chain.nq());

    // --- BLIND-RECOVERY: low-noise trials -> tight match; identity model does NOT match. ---
    ImuNoise lo; lo.accelNoise = 0.005; lo.gyroNoise = 0.0005;
    std::mt19937 rng(424242u);
    int ok = 0; double worstPos = 0, worstRot = 0, idPos = 0; const int K = 8;
    for (int t = 0; t < K; ++t) {
        const SealedTrial tr = generateTrial(chain, ex, lo, rng);
        const Recovered r = recoverMount(chain, tr.joints, tr.readings, ex.gravity);
        const double pe = posErrorM(r.mount, tr.mount), re = rotErrorDeg(r.mount, tr.mount);
        if (r.link == tr.link && pe < 0.01 && re < 1.0) ++ok;
        worstPos = std::max(worstPos, pe); worstRot = std::max(worstRot, re);
        idPos += tr.mount.p.norm();                          // identity-mount model error = |true.p|
    }
    idPos /= K;
    const bool blindOk = ok >= K - 1;                        // allow at most one weak-observability outlier
    const bool idFails = idPos > 0.05;                       // the identity "guess" does not match the truth

    // --- NOISE-ROBUST: fixed truth, many noisy runs -> mean centred; no-bias model is biased. ---
    Mount trueM; trueM.p = Eigen::Vector3d(0.10, -0.07, 0.05);
    trueM.R = Eigen::AngleAxisd(0.8, Eigen::Vector3d(0.1,0.6,0.79).normalized()).toRotationMatrix();
    const int trueLink = 3;
    const JointStates js = sineExcitation(chain.nq(), ex);
    const LinkKin k = computeKinematics(chain, js, trueLink);
    const ImuStream clean = predictReadings(k, trueM, ex.gravity, true);
    ImuNoise nz; nz.accelNoise = 0.05; nz.gyroNoise = 0.005;
    nz.accelBias = Eigen::Vector3d(0.2, -0.15, 0.1); nz.gyroBias = Eigen::Vector3d(0.02, 0.01, -0.015);  // CONSTANT bias
    const int R = 40;
    Eigen::Vector3d meanReal = Eigen::Vector3d::Zero(), meanNoBias = Eigen::Vector3d::Zero();
    double spread = 0;
    for (int r = 0; r < R; ++r) {
        std::mt19937 nr(1000u + r);
        const ImuStream obs = applyNoise(clean, nz, nr);
        const LinkFit fReal = recoverForLink(chain, js, obs, trueLink, ex.gravity, /*estimateBias=*/true);
        const LinkFit fNo   = recoverForLink(chain, js, obs, trueLink, ex.gravity, /*estimateBias=*/false);
        meanReal += fReal.mount.p; meanNoBias += fNo.mount.p;
        spread += (fReal.mount.p - trueM.p).squaredNorm();
    }
    meanReal /= R; meanNoBias /= R; spread = std::sqrt(spread / R);
    const double biasReal = (meanReal - trueM.p).norm();        // should be ~0 (centred)
    const double biasNoBias = (meanNoBias - trueM.p).norm();    // shifted by the un-modelled bias
    const bool centred = biasReal < 0.01;
    const bool noBiasShifted = biasNoBias > 3.0 * std::max(biasReal, 1e-4);

    const bool pass = blindOk && idFails && centred && noBiasShifted;
    printf("[imu]   BLIND-RECOVERY low-noise: %d/%d within 1cm/1deg (worst pos=%.4fm rot=%.3fdeg)  %s\n",
           ok, K, worstPos, worstRot, blindOk ? "PASS" : "FAIL");
    printf("[imu]   NEG-CTRL identity-mount model error=%.3fm (must NOT match)  %s\n",
           idPos, idFails ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[imu]   NOISE-ROBUST: real mean-bias=%.4fm (centred, spread=%.4fm) ; no-bias-model mean-bias=%.4fm  %s\n",
           biasReal, spread, biasNoBias, (centred && noBiasShifted) ? "PASS" : "FAIL");
    printf("[imu] %s\n", pass ? "ALL PASS (blind recovery matches sealed truth; centred under noise; identity & no-bias models fail)"
                              : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
bool runHundredsOfTrialsGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[imu] GATE HUNDREDS-OF-TRIALS -- random link+pose distribution vs sealed truth (success rate + honest failures)\n");

    krs::dyn::SerialChain chain; buildArm6(chain);
    const Excitation ex = defaultExcitation(chain.nq());
    ImuNoise noise; noise.accelNoise = 0.02; noise.gyroNoise = 0.002;
    noise.accelBias = Eigen::Vector3d(0.05, -0.03, 0.02); noise.gyroBias = Eigen::Vector3d(0.005, -0.004, 0.003);

    const int nTrials = 300;
    const double tolPos = 0.02, tolRot = 2.0;       // 2 cm / 2 deg success bar
    int success = 0, linkOk = 0, idMatches = 0;
    std::vector<double> posErrs; posErrs.reserve(nTrials);
    std::vector<double> rotErrs; rotErrs.reserve(nTrials);
    std::mt19937 rng(7u);
    for (int t = 0; t < nTrials; ++t) {
        const SealedTrial tr = generateTrial(chain, ex, noise, rng);
        const Recovered r = recoverMount(chain, tr.joints, tr.readings, ex.gravity);
        const double pe = posErrorM(r.mount, tr.mount), re = rotErrorDeg(r.mount, tr.mount);
        const bool lk = (r.link == tr.link);
        posErrs.push_back(pe); rotErrs.push_back(re);
        if (lk) ++linkOk;
        const bool good = lk && pe < tolPos && re < tolRot;
        if (good) ++success;
        // NEG: the identity-mount "model" -- does it match? (count how often |true.p|<tol == a lucky pass)
        if (tr.mount.p.norm() < tolPos) ++idMatches;
    }

    // --- HONEST FAILURE CHARACTERISATION (the directive's "which configs fail and why"):
    //     an IMU on a SINGLE-AXIS link (body 0 rotates about its yaw-Z joint only) is
    //     geometrically UNDER-DETERMINED: omega_body always points along z, so the Procrustes
    //     cannot resolve the about-axis orientation, AND the along-axis lever component leaves
    //     no centripetal signature -- so several mount DOF are unobservable and the recovery is
    //     far from truth. This is the "near a joint axis -> weakly observable" mode, DEMONSTRATED
    //     (not hidden) -- and is WHY the trials use deeper links with compound, multi-axis motion. ---
    double saPosErr = 0, saRotErr = 0; bool singleAxisChar = false;
    {
        const JointStates js0 = sineExcitation(chain.nq(), ex);   // body 0 sees pure yaw-Z rotation
        Mount tm; tm.p = Eigen::Vector3d(0.12, -0.09, 0.15);
        tm.R = Eigen::AngleAxisd(0.6, Eigen::Vector3d(0.2,0.3,0.93).normalized()).toRotationMatrix();
        const LinkKin k0 = computeKinematics(chain, js0, 0);
        std::mt19937 nr(99u);
        const ImuStream obs0 = applyNoise(predictReadings(k0, tm, ex.gravity, true), noise, nr);
        const LinkFit f0 = recoverForLink(chain, js0, obs0, 0, ex.gravity, true);
        saPosErr = posErrorM(f0.mount, tm);
        saRotErr = rotErrorDeg(f0.mount, tm);
        // the failure mode is REAL: a single rotation axis cannot pin the 6-DOF mount.
        singleAxisChar = (saPosErr > 0.05) || (saRotErr > 5.0);
    }
    std::sort(posErrs.begin(), posErrs.end()); std::sort(rotErrs.begin(), rotErrs.end());
    const double medPos = posErrs[nTrials / 2], p90Pos = posErrs[int(nTrials * 0.9)], maxPos = posErrs.back();
    const double medRot = rotErrs[nTrials / 2], p90Rot = rotErrs[int(nTrials * 0.9)];
    const double rate = double(success) / nTrials;

    const bool rateOk = rate >= 0.85;
    const bool linkRate = double(linkOk) / nTrials >= 0.95;
    const bool idModelFails = double(idMatches) / nTrials < 0.10;   // identity rarely "lucky" -> not a distribution-passer

    const bool pass = rateOk && linkRate && idModelFails && singleAxisChar;
    printf("[imu]   %d trials (deep links, compound rotation): success=%.1f%% (pos<%.0fcm & rot<%.0fdeg & link), link-ID=%.1f%%\n",
           nTrials, rate * 100.0, tolPos * 100, tolRot, double(linkOk) / nTrials * 100.0);
    printf("[imu]   pos err median=%.4fm p90=%.4fm max=%.4fm ; rot err median=%.3fdeg p90=%.3fdeg\n",
           medPos, p90Pos, maxPos, medRot, p90Rot);
    printf("[imu]   HONEST FAILURE MODE -- single-axis link (body 0, yaw-Z only) is UNDER-DETERMINED: posErr=%.3fm rotErr=%.2fdeg\n"
           "[imu]     (one rotation axis cannot observe the about-axis orientation or along-axis position) -> trials use compound-rotation links  %s\n",
           saPosErr, saRotErr, singleAxisChar ? "characterised" : "NOT-SHOWN");
    printf("[imu]   NEG-CTRL identity-mount model 'matches' only %.1f%% (a lucky-case model fails the distribution)  %s\n",
           double(idMatches) / nTrials * 100.0, idModelFails ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[imu] %s\n", pass ? "ALL PASS (recovery matches the sealed truth in the large majority over hundreds of random trials)"
                              : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::imu
