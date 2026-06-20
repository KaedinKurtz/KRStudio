#pragma once
// ===========================================================================
// BLIND IMU EXTRINSIC RECOVERY (krs::imu) -- sensor extrinsic calibration via
// system-ID. A random IMU at a random 6-DOF pose on a random non-base link is
// excited by a known multi-sine; the synthetic readings (lever-arm physics +
// realistic noise) plus the FK joint states are the ONLY thing a recovering
// function sees -- the true mount + link are SEALED. The recovery backs the
// mount out of the physics and is checked against the sealed truth afterward.
//
// FORWARD MODEL (lever-arm inclusive): for a point r on a link with body pose
// (R_b,p_b), world angular velocity w and acceleration, the IMU point's accel is
//   a_point = a_origin + alpha x (R_b r) + w x (w x (R_b r))         (linear in r)
// The accelerometer reads specific force in the IMU frame: R_imu^T (a_point - g);
// the gyro reads R_imu^T w. POSITION enters ONLY through the lever-arm term, which
// is excited by ROTATION -- so position is observable iff the link rotates.
//
// RECOVERY (closed form, hence provably centered under zero-mean noise):
//   * orientation R_mount: gyro = R_mount^T (R_b^T w) -> orthogonal Procrustes (SVD)
//   * position p_mount: a_point is LINEAR in p_mount -> linear least squares
//   * gyro/accel bias estimated jointly (constant offsets)
// Pure Eigen, no GL/PhysX/OCCT.
// ===========================================================================
#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <Eigen/SVD>
#include <vector>
#include <random>
#include <cmath>
#include <algorithm>
#include "RobotDynamics.hpp"

namespace krs::imu {

constexpr double kPi = 3.14159265358979323846;

inline Eigen::Matrix3d hat(const Eigen::Vector3d& w) {
    Eigen::Matrix3d S;
    S <<     0, -w.z(),  w.y(),
         w.z(),      0, -w.x(),
        -w.y(),  w.x(),      0;
    return S;
}
inline Eigen::Vector3d vee(const Eigen::Matrix3d& S) {
    return 0.5 * Eigen::Vector3d(S(2,1) - S(1,2), S(0,2) - S(2,0), S(1,0) - S(0,1));
}

// IMU pose on the link (the 6-DOF extrinsic to recover).
struct Mount {
    Eigen::Matrix3d R = Eigen::Matrix3d::Identity();   // IMU orientation in the link frame
    Eigen::Vector3d p = Eigen::Vector3d::Zero();        // IMU position in the link frame (lever arm)
};

// realistic per-axis IMU error model.
struct ImuNoise {
    Eigen::Vector3d accelBias  = Eigen::Vector3d::Zero();
    Eigen::Vector3d gyroBias   = Eigen::Vector3d::Zero();
    Eigen::Vector3d accelScale = Eigen::Vector3d::Ones();
    Eigen::Vector3d gyroScale  = Eigen::Vector3d::Ones();
    double accelNoise = 0.0;     // white std, m/s^2
    double gyroNoise  = 0.0;     // white std, rad/s
    double biasWalk   = 0.0;     // random-walk std per step on the biases
};

struct ImuSample { Eigen::Vector3d acc = Eigen::Vector3d::Zero(); Eigen::Vector3d gyro = Eigen::Vector3d::Zero(); };
struct ImuStream { std::vector<ImuSample> samples; double dt = 1e-3; };
struct JointStates { std::vector<Eigen::VectorXd> q; double dt = 1e-3; };   // FK joint states over time

struct Excitation {
    Eigen::VectorXd q0, amp, freq, phase;       // per-joint base / amplitude / frequency(Hz) / phase
    double dt = 1e-3, T = 2.0;
    Eigen::Vector3d gravity = Eigen::Vector3d(0, 0, -9.81);
};

// --- the known multi-sine excitation (distinct freqs/phases -> rich, all links rotate) ---
inline JointStates sineExcitation(int nq, const Excitation& ex) {
    JointStates js; js.dt = ex.dt;
    const int N = std::max(8, int(ex.T / ex.dt));
    for (int i = 0; i < N; ++i) {
        const double t = i * ex.dt;
        Eigen::VectorXd q(nq);
        for (int j = 0; j < nq; ++j)
            q[j] = ex.q0[j] + ex.amp[j] * std::sin(2.0 * kPi * ex.freq[j] * t + ex.phase[j]);
        js.q.push_back(q);
    }
    return js;
}

// --- per-link kinematics from FK (central finite differences), mount-independent ---
// Valid over interior samples i in [2, N-3] (alpha is a double difference).
struct LinkKin {
    double dt = 1e-3;
    std::vector<Eigen::Matrix3d> Rb;       // link body orientation (world)
    std::vector<Eigen::Vector3d> pb;       // link body origin (world)
    std::vector<Eigen::Vector3d> omegaW;   // link angular velocity (world)
    std::vector<Eigen::Vector3d> alphaW;   // link angular acceleration (world)
    std::vector<Eigen::Vector3d> aOrigin;  // link body-origin linear acceleration (world)
    int size() const { return int(Rb.size()); }
};

inline LinkKin computeKinematics(const krs::dyn::SerialChain& chain, const JointStates& js, int link) {
    const int N = int(js.q.size());
    const double dt = js.dt;
    std::vector<krs::dyn::Pose> P(N);
    for (int i = 0; i < N; ++i) P[i] = chain.bodyPose(js.q[i], link);
    std::vector<Eigen::Vector3d> omega(N, Eigen::Vector3d::Zero());
    for (int i = 1; i < N - 1; ++i) {
        const Eigen::Matrix3d Rdot = (P[i + 1].R - P[i - 1].R) / (2.0 * dt);
        omega[i] = vee(Rdot * P[i].R.transpose());
    }
    LinkKin k; k.dt = dt;
    for (int i = 2; i < N - 2; ++i) {                       // i in [2, N-3]
        k.Rb.push_back(P[i].R);
        k.pb.push_back(P[i].p);
        k.omegaW.push_back(omega[i]);
        k.alphaW.push_back((omega[i + 1] - omega[i - 1]) / (2.0 * dt));
        k.aOrigin.push_back((P[i + 1].p - 2.0 * P[i].p + P[i - 1].p) / (dt * dt));
    }
    return k;
}

// --- forward sensor model: kinematics + mount -> noiseless readings ---
// leverArm=false is the BROKEN model that ignores the position term (the IMU-MODEL
// neg-control): position then has NO effect on the readings -> unrecoverable.
inline ImuStream predictReadings(const LinkKin& k, const Mount& m, const Eigen::Vector3d& g,
                                 bool leverArm = true) {
    ImuStream s; s.dt = k.dt; s.samples.resize(k.size());
    for (int j = 0; j < k.size(); ++j) {
        const Eigen::Matrix3d Rimu = k.Rb[j] * m.R;
        Eigen::Vector3d aPoint = k.aOrigin[j];
        if (leverArm) {
            const Eigen::Vector3d rW = k.Rb[j] * m.p;       // lever arm in world
            aPoint += (hat(k.alphaW[j]) + hat(k.omegaW[j]) * hat(k.omegaW[j])) * rW;
        }
        s.samples[j].acc  = Rimu.transpose() * (aPoint - g);
        s.samples[j].gyro = Rimu.transpose() * k.omegaW[j];
    }
    return s;
}

inline ImuStream applyNoise(const ImuStream& clean, const ImuNoise& n, std::mt19937& rng) {
    ImuStream s = clean;
    std::normal_distribution<double> nd(0.0, 1.0);
    Eigen::Vector3d bA = n.accelBias, bG = n.gyroBias;
    for (auto& smp : s.samples) {
        for (int a = 0; a < 3; ++a) {
            smp.acc[a]  = n.accelScale[a] * smp.acc[a]  + bA[a] + n.accelNoise * nd(rng);
            smp.gyro[a] = n.gyroScale[a]  * smp.gyro[a] + bG[a] + n.gyroNoise  * nd(rng);
        }
        if (n.biasWalk > 0.0)
            for (int a = 0; a < 3; ++a) { bA[a] += n.biasWalk * nd(rng); bG[a] += n.biasWalk * nd(rng); }
    }
    return s;
}

// --- THE INFORMATION BARRIER: the truth (link + mount) lives here, SEALED. The
//     recovery is handed only `joints` + `readings`; it cannot reach link/mount. ---
struct SealedTrial {
    int link = -1;          // SEALED
    Mount mount;            // SEALED
    JointStates joints;     // visible to the recovery
    ImuStream readings;     // visible to the recovery
};

inline Mount randomMount(std::mt19937& rng, double posBox = 0.3) {
    std::uniform_real_distribution<double> u(-1, 1), ang(0.2, kPi), pos(-posBox, posBox);
    Eigen::Vector3d axis(u(rng), u(rng), u(rng));
    if (axis.norm() < 1e-6) axis = Eigen::Vector3d::UnitZ();
    axis.normalize();
    Mount m;
    m.R = Eigen::AngleAxisd(ang(rng), axis).toRotationMatrix();
    m.p = Eigen::Vector3d(pos(rng), pos(rng), pos(rng));
    return m;
}

inline SealedTrial generateTrial(const krs::dyn::SerialChain& chain, const Excitation& ex,
                                 const ImuNoise& noise, std::mt19937& rng) {
    SealedTrial t;
    std::uniform_int_distribution<int> li(1, chain.nbody() - 1);     // random NON-base link
    t.link = li(rng);
    t.mount = randomMount(rng);
    t.joints = sineExcitation(chain.nq(), ex);
    const LinkKin k = computeKinematics(chain, t.joints, t.link);
    t.readings = applyNoise(predictReadings(k, t.mount, ex.gravity, true), noise, rng);
    return t;
}

// --- recover the mount for a GIVEN candidate link (Procrustes R + linear-LS p + bias) ---
struct LinkFit { Mount mount; Eigen::Vector3d gyroBias, accBias; double residual = 1e30; };

inline LinkFit recoverForLink(const krs::dyn::SerialChain& chain, const JointStates& js,
                              const ImuStream& obs, int link, const Eigen::Vector3d& g,
                              bool estimateBias = true) {
    LinkFit f;
    const LinkKin k = computeKinematics(chain, js, link);
    const int M = std::min(k.size(), int(obs.samples.size()));
    if (M < 12) return f;                                   // not enough motion samples

    std::vector<Eigen::Vector3d> wbody(M);
    for (int j = 0; j < M; ++j) wbody[j] = k.Rb[j].transpose() * k.omegaW[j];

    // orientation R_mount + gyro bias (alternating: Procrustes then mean-residual bias).
    // The NEG-control (estimateBias=false) leaves the bias at zero -> a constant IMU bias
    // shifts the estimate off-centre (a noise-biased model).
    Eigen::Matrix3d Rm = Eigen::Matrix3d::Identity();
    Eigen::Vector3d bg = Eigen::Vector3d::Zero();
    const int nIter = estimateBias ? 8 : 1;
    for (int it = 0; it < nIter; ++it) {
        Eigen::Matrix3d H = Eigen::Matrix3d::Zero();
        for (int j = 0; j < M; ++j) H += (obs.samples[j].gyro - bg) * wbody[j].transpose();
        Eigen::JacobiSVD<Eigen::Matrix3d> svd(H, Eigen::ComputeFullU | Eigen::ComputeFullV);
        Eigen::Matrix3d D = Eigen::Matrix3d::Identity();
        D(2, 2) = (svd.matrixV() * svd.matrixU().transpose()).determinant() < 0 ? -1.0 : 1.0;
        Rm = svd.matrixV() * D * svd.matrixU().transpose();
        if (!estimateBias) break;
        Eigen::Vector3d s = Eigen::Vector3d::Zero();
        for (int j = 0; j < M; ++j) s += obs.samples[j].gyro - Rm.transpose() * wbody[j];
        bg = s / double(M);
    }

    // position p_mount (+ accel bias when estimating): a_point is LINEAR in p_mount.
    const int nCol = estimateBias ? 6 : 3;
    Eigen::MatrixXd A(3 * M, nCol);
    Eigen::VectorXd rhs(3 * M);
    for (int j = 0; j < M; ++j) {
        const Eigen::Matrix3d Mm = (hat(k.alphaW[j]) + hat(k.omegaW[j]) * hat(k.omegaW[j])) * k.Rb[j];
        A.block<3, 3>(3 * j, 0) = Rm.transpose() * k.Rb[j].transpose() * Mm;
        if (estimateBias) A.block<3, 3>(3 * j, 3) = Eigen::Matrix3d::Identity();
        const Eigen::Vector3d c = Rm.transpose() * k.Rb[j].transpose() * (k.aOrigin[j] - g);
        rhs.segment<3>(3 * j) = obs.samples[j].acc - c;
    }
    const Eigen::VectorXd x = A.colPivHouseholderQr().solve(rhs);
    f.mount.R = Rm; f.mount.p = x.head<3>(); f.gyroBias = bg;
    if (estimateBias) f.accBias = x.segment<3>(3); else f.accBias.setZero();

    // fit residual (predicted vs de-biased observed) -> used to identify the link.
    const ImuStream pred = predictReadings(k, f.mount, g, true);
    double r = 0.0;
    for (int j = 0; j < M; ++j)
        r += (pred.samples[j].acc  - (obs.samples[j].acc  - f.accBias )).squaredNorm()
           + (pred.samples[j].gyro - (obs.samples[j].gyro - f.gyroBias)).squaredNorm();
    f.residual = std::sqrt(r / double(M));
    return f;
}

// --- THE RECOVERY: sees ONLY (chain, joints, readings, gravity). Identifies the link
//     (best fit residual over all non-base links) and returns its recovered mount. ---
struct Recovered { int link = -1; Mount mount; double residual = 1e30; };

inline Recovered recoverMount(const krs::dyn::SerialChain& chain, const JointStates& js,
                              const ImuStream& obs, const Eigen::Vector3d& g) {
    Recovered best;
    for (int link = 1; link < chain.nbody(); ++link) {
        const LinkFit f = recoverForLink(chain, js, obs, link, g);
        if (f.residual < best.residual) { best.residual = f.residual; best.mount = f.mount; best.link = link; }
    }
    return best;
}

// --- error metrics vs the sealed truth ---
inline double posErrorM(const Mount& a, const Mount& b) { return (a.p - b.p).norm(); }
inline double rotErrorDeg(const Mount& a, const Mount& b) {
    const Eigen::Matrix3d E = a.R.transpose() * b.R;
    double c = (E.trace() - 1.0) * 0.5;
    c = std::clamp(c, -1.0, 1.0);
    return std::acos(c) * 180.0 / kPi;
}

// gates (env KRS_IMU_SELFTEST; folded into KRS_OVERNIGHT_BENCH).
bool runImuModelGate();             // IMU-MODEL-CORRECT (closed-form; leverless neg-ctrl)
bool runInfoBarrierGate();          // INFORMATION-BARRIER (fails on garbage input)
bool runExcitationObservGate();     // EXCITATION-OBSERVABILITY (6 DOF; degenerate leaves position under-observable)
bool runBlindRecoveryGate();        // BLIND-RECOVERY (matches sealed truth <tol) + NOISE-ROBUST (centered)
bool runHundredsOfTrialsGate();     // HUNDREDS-OF-TRIALS (distribution + honest failure characterization)

} // namespace krs::imu
