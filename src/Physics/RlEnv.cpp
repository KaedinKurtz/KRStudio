// ===========================================================================
// RlEnv.cpp -- RL environment interface (gym-style step API), krs::rl.
//
// docs/POLICY_AND_LEARNING.md step 6. The policy outputs a RESIDUAL on a
// reference joint trajectory q_ref(t); the base action is the reference
// feedforward (residual → 0 ⇒ "just track the reference"). The reward tracks
// imitation (−w‖q−q_ref‖²) plus an optional Cartesian task term via the
// existing SerialChain FK. Deterministic: no Date/rand; step() is a pure
// function of (state, action). The OPTIMIZER stays external.
//
// GATE (KRS_RLENV_SELFTEST) proves: (1) determinism, (2) the pure reference
// (zero residual) maximizes the imitation reward, (3) a residual can raise a
// synthetic task reward (the interface exposes control), (4) dims/reset are
// correct; NEG-CTRL flips the imitation sign so the reference no longer wins.
// ===========================================================================
#include "RlEnv.hpp"

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <limits>

namespace krs::rl {

using krs::dyn::SerialChain;
using krs::dyn::DynJoint;
using krs::dyn::DynBody;
using krs::dyn::JType;

// ---------------------------------------------------------------------------
// sampleRef: piecewise-linear interpolation of the reference, clamped at ends.
// ---------------------------------------------------------------------------
Eigen::VectorXd sampleRef(const RefTrajectory& ref, double time) {
    const int n = int(ref.q.size());
    // Empty reference -> empty config (defensive; a real ref always has samples).
    if (n == 0) return Eigen::VectorXd();
    // Single sample, or before/after the whole span: clamp to an endpoint.
    if (n == 1 || time <= ref.t.front()) return ref.q.front();
    if (time >= ref.t.back())            return ref.q.back();
    // Find the bracketing interval [t[k], t[k+1]] with t[k] <= time < t[k+1].
    int k = 0;
    while (k + 1 < n && ref.t[k + 1] <= time) ++k;
    const double t0 = ref.t[k], t1 = ref.t[k + 1];
    const double denom = (t1 - t0);
    // Degenerate zero-width interval -> take the left sample (no divide-by-0).
    const double a = (denom > 0.0) ? (time - t0) / denom : 0.0;
    return ref.q[k] + a * (ref.q[k + 1] - ref.q[k]);
}

// ---------------------------------------------------------------------------
// RlEnv ctor: cache the chain/ref/config and pull per-dof position limits out
// of the chain's DynJoints so step() can clamp q_next.
// ---------------------------------------------------------------------------
RlEnv::RlEnv(SerialChain chain, RefTrajectory ref, RlEnvConfig cfg)
    : chain_(std::move(chain)), ref_(std::move(ref)), cfg_(cfg) {
    // nq from the reference sample dimension (== chain movable dofs by construction).
    nq_ = ref_.q.empty() ? chain_.nq() : int(ref_.q.front().size());
    // Walk the bodies; every movable joint contributes one (qLower, qUpper) column,
    // in dof order, so the limit vectors line up with the action / q layout.
    qLower_.assign(nq_, -std::numeric_limits<double>::infinity());
    qUpper_.assign(nq_, std::numeric_limits<double>::infinity());
    for (int b = 0; b < chain_.nbody(); ++b) {
        const int dof = chain_.dofOf(b);            // −1 if the joint is fixed
        if (dof < 0 || dof >= nq_) continue;
        const DynJoint& j = chain_.joint(b);
        qLower_[dof] = j.qLower;
        qUpper_[dof] = j.qUpper;
    }
    q_ = sampleRef(ref_, 0.0);
    time_ = 0.0;
}

// ---------------------------------------------------------------------------
// clampToLimits: project a config onto the per-dof [qLower, qUpper] box.
// ---------------------------------------------------------------------------
Eigen::VectorXd RlEnv::clampToLimits(const Eigen::VectorXd& q) const {
    Eigen::VectorXd out = q;
    for (int i = 0; i < nq_ && i < int(out.size()); ++i)
        out[i] = std::min(qUpper_[i], std::max(qLower_[i], out[i]));
    return out;
}

// ---------------------------------------------------------------------------
// duration: the total span of the reference (0 if fewer than two samples).
// ---------------------------------------------------------------------------
double RlEnv::duration() const {
    return (ref_.t.size() >= 2) ? ref_.t.back() : 0.0;
}

// ---------------------------------------------------------------------------
// observe: concat[ q, q_ref(t)−q, t/duration ]  (dimension 2*nq + 1).
// ---------------------------------------------------------------------------
Eigen::VectorXd RlEnv::observe() const {
    const Eigen::VectorXd qref = sampleRef(ref_, time_);
    Eigen::VectorXd obs(2 * nq_ + 1);
    obs.segment(0, nq_)  = q_;
    obs.segment(nq_, nq_) = qref - q_;
    const double D = duration();
    obs[2 * nq_] = (D > 0.0) ? (time_ / D) : 0.0;   // normalized phase (no divide-by-0)
    return obs;
}

// ---------------------------------------------------------------------------
// reward: imitation (−w‖q−q_ref(t)‖², sign flipped by the NEG-CTRL knob) plus an
// optional Cartesian task term −taskWeight·‖ee(q) − eeTarget‖² (higher when the
// EE is nearer the target). The task term uses the existing SerialChain FK.
// ---------------------------------------------------------------------------
double RlEnv::reward() const {
    const Eigen::VectorXd qref = sampleRef(ref_, time_);
    const double imit2 = (q_ - qref).squaredNorm();
    // NEG-CTRL (scrambleReward): flip the sign so the pure reference no longer
    // maximizes the imitation term (proves the honest reward tracks the ref).
    const double imitTerm = cfg_.scrambleReward
        ? (+cfg_.imitationWeight * imit2)      // scrambled: reward = +‖q−q_ref‖²
        : (-cfg_.imitationWeight * imit2);     // honest:   reward = −‖q−q_ref‖²
    double taskTerm = 0.0;
    if (cfg_.taskWeight != 0.0 && cfg_.eeBody >= 0) {
        const krs::dyn::Pose pe = chain_.bodyPose(q_, cfg_.eeBody);
        // −‖p_ee − target‖²: larger (less negative) as the EE nears the target.
        taskTerm = -cfg_.taskWeight * (pe.p - cfg_.eeTarget).squaredNorm();
    }
    return imitTerm + taskTerm;
}

// ---------------------------------------------------------------------------
// reset: t=0, q=q_ref(0); return the initial observation.
// ---------------------------------------------------------------------------
Eigen::VectorXd RlEnv::reset() {
    time_ = 0.0;
    q_ = sampleRef(ref_, 0.0);
    return observe();
}

// ---------------------------------------------------------------------------
// step: q_next = clamp( q_ref(t+dt) + residualScale·action ); advance t by dt.
//       reward evaluated at the new (q, t); done when t >= duration.
// ---------------------------------------------------------------------------
StepResult RlEnv::step(const Eigen::VectorXd& action) {
    const double tNext = time_ + cfg_.dt;
    Eigen::VectorXd qref = sampleRef(ref_, tNext);
    // Base action is the reference feedforward; the policy adds a scaled residual.
    Eigen::VectorXd cmd = qref;
    for (int i = 0; i < nq_ && i < int(action.size()); ++i)
        cmd[i] += cfg_.residualScale * action[i];
    q_ = clampToLimits(cmd);
    time_ = tNext;

    StepResult r;
    r.reward = reward();
    r.done   = (time_ >= duration());
    r.obs    = observe();
    return r;
}

int RlEnv::actionDim() const { return nq_; }
int RlEnv::obsDim()    const { return 2 * nq_ + 1; }

// ===========================================================================
// GATE -- runRlEnvGate() / env KRS_RLENV_SELFTEST.
// ===========================================================================
namespace {

// Build a small 3-revolute chain: axes alternate z / y / z, ptree spacing ~0.3,
// mass=1, com=0, inertia=I. Generous joint limits so the reference never clamps.
SerialChain makeChain() {
    SerialChain c;
    const Eigen::Vector3d axes[3] = {
        Eigen::Vector3d(0, 0, 1),   // J0: z
        Eigen::Vector3d(0, 1, 0),   // J1: y
        Eigen::Vector3d(0, 0, 1) }; // J2: z
    for (int i = 0; i < 3; ++i) {
        DynJoint dj;
        dj.type   = JType::Revolute;
        dj.parent = i - 1;                                   // serial chain: 0←world, 1←0, 2←1
        dj.Rtree  = Eigen::Matrix3d::Identity();
        dj.ptree  = Eigen::Vector3d(0.3, 0, 0);              // ~0.3 spacing along local x
        dj.axis   = axes[i];
        dj.qLower = -3.14159265358979;                        // wide limits -> ref never clamps
        dj.qUpper =  3.14159265358979;
        DynBody db;                                          // mass=1, com=0, inertia=I (defaults)
        db.mass = 1.0; db.com = Eigen::Vector3d::Zero(); db.inertiaCom = Eigen::Matrix3d::Identity();
        c.addBody(dj, db);
    }
    return c;
}

// A smooth 3-waypoint reference over ~1s (t = {0, 0.5, 1.0}); each dof follows a
// distinct, deterministic set of angles (no rand).
RefTrajectory makeRef() {
    RefTrajectory ref;
    ref.t = { 0.0, 0.5, 1.0 };
    auto v = [](double a, double b, double c) { Eigen::VectorXd q(3); q << a, b, c; return q; };
    ref.q = { v(0.00,  0.10, -0.05),
              v(0.40,  0.30,  0.20),
              v(0.70, -0.10,  0.35) };
    return ref;
}

// Deterministic, index-driven residual pattern (no rand): a fixed sinusoid of the
// dof index and step index, scaled by `amp`. Same seed -> same sequence.
Eigen::VectorXd fixedResidual(int stepIdx, int nq, double amp, int pat) {
    Eigen::VectorXd a(nq);
    for (int i = 0; i < nq; ++i)
        a[i] = amp * std::sin(0.7 * stepIdx + 1.3 * i + 0.9 * pat);
    return a;
}

// Roll a full episode with a fixed residual pattern; return total reward and,
// if requested, capture the obs/reward streams for the determinism check.
double rollout(RlEnv& env, double amp, int pat,
               std::vector<Eigen::VectorXd>* obsLog = nullptr,
               std::vector<double>* rewLog = nullptr) {
    env.reset();
    double total = 0.0;
    // ~1s duration / dt=0.02 -> ~50 steps; run a fixed count so both rollouts match.
    for (int k = 0; k < 60; ++k) {
        const Eigen::VectorXd a = (amp == 0.0)
            ? Eigen::VectorXd::Zero(env.actionDim())
            : fixedResidual(k, env.actionDim(), amp, pat);
        StepResult r = env.step(a);
        total += r.reward;
        if (obsLog) obsLog->push_back(r.obs);
        if (rewLog) rewLog->push_back(r.reward);
    }
    return total;
}

} // namespace

bool runRlEnvGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[rlenv] GATE RLENV -- gym-style residual-on-reference env: determinism, "
           "imitation-max-by-reference, residual-improves-task, dims/reset\n");

    const SerialChain chain = makeChain();
    const RefTrajectory ref = makeRef();

    // -------------------------------------------------------------------
    // (4) DIMS + RESET: obs/action dims are correct and reset() == q_ref(0).
    // -------------------------------------------------------------------
    bool dimsOk = false, resetOk = false;
    {
        RlEnvConfig cfg;                              // imitation only (taskWeight=0)
        RlEnv env(chain, ref, cfg);
        dimsOk = (env.actionDim() == 3) && (env.obsDim() == 2 * 3 + 1);
        const Eigen::VectorXd obs0 = env.reset();
        const Eigen::VectorXd q0   = sampleRef(ref, 0.0);
        // obs = concat[ q, q_ref−q, t/dur ]; at reset q==q_ref(0) so the first 3 == q0,
        // the middle 3 == 0, and the phase == 0.
        const double headErr  = (obs0.segment(0, 3) - q0).norm();
        const double midErr   = obs0.segment(3, 3).norm();
        const double phaseErr = std::abs(obs0[6]);
        resetOk = int(obs0.size()) == 7 && headErr < 1e-12 && midErr < 1e-12 && phaseErr < 1e-12;
        printf("[rlenv]   (4) dims actionDim=%d(==3) obsDim=%d(==7) %s ; "
               "reset()==q_ref(0) (headErr=%.2e midErr=%.2e phase=%.2e) %s\n",
               env.actionDim(), env.obsDim(), dimsOk ? "PASS" : "FAIL",
               headErr, midErr, phaseErr, resetOk ? "PASS" : "FAIL");
    }

    // -------------------------------------------------------------------
    // (1) DETERMINISM: two envs, same action sequence -> identical obs/reward.
    // -------------------------------------------------------------------
    bool determOk = false;
    {
        RlEnvConfig cfg;
        RlEnv envA(chain, ref, cfg), envB(chain, ref, cfg);
        std::vector<Eigen::VectorXd> obsA, obsB;
        std::vector<double> rewA, rewB;
        rollout(envA, 0.15, /*pat*/2, &obsA, &rewA);
        rollout(envB, 0.15, /*pat*/2, &obsB, &rewB);
        double maxObsDiff = 0.0, maxRewDiff = 0.0;
        const size_t n = std::min(obsA.size(), obsB.size());
        for (size_t k = 0; k < n; ++k) {
            maxObsDiff = std::max(maxObsDiff, (obsA[k] - obsB[k]).cwiseAbs().maxCoeff());
            maxRewDiff = std::max(maxRewDiff, std::abs(rewA[k] - rewB[k]));
        }
        determOk = (obsA.size() == obsB.size()) && maxObsDiff < 1e-12 && maxRewDiff < 1e-12;
        printf("[rlenv]   (1) determinism over %zu steps: max|Δobs|=%.2e max|Δreward|=%.2e (<1e-12) %s\n",
               n, maxObsDiff, maxRewDiff, determOk ? "PASS" : "FAIL");
    }

    // -------------------------------------------------------------------
    // (2) IMITATION MAXIMIZED BY THE PURE REFERENCE (zero residual), taskWeight=0.
    //     Total reward with action=0 must be >= any nonzero-residual rollout.
    // -------------------------------------------------------------------
    bool imitOk = false; double refTotal = 0.0, bestNonzero = -std::numeric_limits<double>::infinity();
    {
        RlEnvConfig cfg;                              // honest imitation reward, no task
        RlEnv env(chain, ref, cfg);
        refTotal = rollout(env, /*amp*/0.0, /*pat*/0);      // pure reference
        // A few deterministic, distinct nonzero-residual patterns.
        const double amps[3]  = { 0.10, 0.25, 0.40 };
        const int    pats[3]  = { 0, 1, 2 };
        for (int i = 0; i < 3; ++i) {
            RlEnv e2(chain, ref, cfg);
            const double tot = rollout(e2, amps[i], pats[i]);
            bestNonzero = std::max(bestNonzero, tot);
        }
        // refTotal should be ~0 (q tracks q_ref exactly, no clamp), strictly above the best residual.
        imitOk = (refTotal >= bestNonzero) && (refTotal > -1e-9);
        printf("[rlenv]   (2) imitation reward: pure reference total=%.6f  best nonzero-residual total=%.6f "
               "(reference >= residual) %s\n",
               refTotal, bestNonzero, imitOk ? "PASS" : "FAIL");
    }

    // -------------------------------------------------------------------
    // (3) A RESIDUAL IMPROVES A SYNTHETIC TASK REWARD: taskWeight>0,
    //     imitationWeight=0, eeTarget slightly off the reference EE path. A
    //     hand-picked residual toward the target must beat the zero residual.
    // -------------------------------------------------------------------
    bool taskOk = false; double zeroTask = 0.0, steerTask = 0.0;
    {
        // The EE is the last body (index nbody-1). Measure the reference EE path,
        // then set a target slightly OFF it so a residual has something to close.
        const int eeBody = chain.nbody() - 1;
        // Reference EE at mid-trajectory + a small offset the residual should chase.
        const Eigen::VectorXd qMid = sampleRef(ref, 0.5);
        const krs::dyn::Pose peMid = chain.bodyPose(qMid, eeBody);
        const Eigen::Vector3d target = peMid.p + Eigen::Vector3d(0.05, -0.03, 0.04);

        RlEnvConfig cfg;
        cfg.imitationWeight = 0.0;      // task-only reward
        cfg.taskWeight      = 1.0;
        cfg.eeBody          = eeBody;
        cfg.eeTarget        = target;

        RlEnv envZero(chain, ref, cfg);
        zeroTask = rollout(envZero, /*amp*/0.0, /*pat*/0);   // zero residual (reference EE path)

        // Hand-pick a residual that steers the EE toward the target: at each step,
        // take one Jacobian-transpose (gradient) step of −‖ee−target‖² in q-space,
        // scaled into the residual channel. This is a DETERMINISTIC controller (no
        // optimizer inside the env) that demonstrates the interface exposes control.
        RlEnv envSteer(chain, ref, cfg);
        envSteer.reset();
        double total = 0.0;
        double tSim = 0.0;
        for (int k = 0; k < 60; ++k) {
            tSim += cfg.dt;
            const Eigen::VectorXd qref = sampleRef(ref, tSim);
            const krs::dyn::Pose pe = chain.bodyPose(qref, eeBody);
            const Eigen::Vector3d err = target - pe.p;                 // desired EE motion
            // Positional Jacobian (rows [linear;angular]) -> use the top 3 rows.
            const Eigen::MatrixXd J = chain.jacobian(qref, eeBody);
            const Eigen::Vector3d dLin = err;
            const Eigen::VectorXd dq = J.topRows(3).transpose() * dLin;  // Jᵀ·Δp gradient step
            // Fold dq into the residual channel: residualScale·action = dq -> action = dq/scale.
            Eigen::VectorXd action = dq / std::max(1e-9, cfg.residualScale);
            StepResult r = envSteer.step(action);
            total += r.reward;
        }
        steerTask = total;
        // A residual steering toward the target must beat the zero residual (less-negative task reward).
        taskOk = steerTask > zeroTask + 1e-9;
        printf("[rlenv]   (3) task reward (taskWeight>0, imitationWeight=0): zero-residual=%.6f  "
               "steering-residual=%.6f (residual improves task) %s\n",
               zeroTask, steerTask, taskOk ? "PASS" : "FAIL");
    }

    // -------------------------------------------------------------------
    // NEG-CTRL: scrambleReward flips the imitation sign, so the pure reference
    // NO LONGER maximizes it -- a residual must score HIGHER. If the reference
    // still won here, the imitation reward isn't really tracking q_ref and check
    // (2) would be vacuous.
    // -------------------------------------------------------------------
    bool negOk = false; double scramRef = 0.0, scramBestResid = -std::numeric_limits<double>::infinity();
    {
        RlEnvConfig cfg; cfg.scrambleReward = true;   // reward = +‖q−q_ref‖²
        RlEnv env(chain, ref, cfg);
        scramRef = rollout(env, /*amp*/0.0, /*pat*/0);         // pure reference -> ~0 (still the WORST now)
        const double amps[3] = { 0.10, 0.25, 0.40 };
        const int    pats[3] = { 0, 1, 2 };
        for (int i = 0; i < 3; ++i) {
            RlEnv e2(chain, ref, cfg);
            scramBestResid = std::max(scramBestResid, rollout(e2, amps[i], pats[i]));
        }
        // With the sign flipped a residual scores strictly higher than the reference.
        negOk = scramBestResid > scramRef + 1e-9;
        printf("[rlenv]   NEG-CTRL scrambleReward: reference total=%.6f  best residual total=%.6f "
               "(residual > reference, so reference no longer wins) %s\n",
               scramRef, scramBestResid, negOk ? "REJECTS" : "VACUOUS!");
    }

    const bool pass = dimsOk && resetOk && determOk && imitOk && taskOk && negOk;
    printf("[rlenv] %s\n", pass
        ? "ALL PASS (deterministic gym env; reference maximizes imitation; a residual steers the task; NEG-CTRL non-vacuous)"
        : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::rl
