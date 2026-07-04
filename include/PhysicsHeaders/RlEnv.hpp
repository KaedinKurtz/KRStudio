#pragma once
// ===========================================================================
// RlEnv.hpp -- RL environment interface (gym-style step API), krs::rl.
//
// docs/POLICY_AND_LEARNING.md step 6 ("RL environment interface"): a gym-style
// step API where the POLICY outputs a small RESIDUAL Δ on top of a reference
// joint trajectory q_ref(t). The base action is the reference feedforward, so
// the worst-case behavior (residual → 0) is "just track the reference," which
// is already safe -- the residual only has to learn what the model misses. The
// OPTIMIZER stays external; this file is ONLY the environment interface (the
// deterministic dynamics of reset()/step(), the obs/reward/done contract).
//
// The env is deterministic: NO Date/rand; step() is a pure function of
// (state, action). The reward tracks imitation (−w‖q−q_ref‖²) plus an optional
// task term that pulls the end-effector toward a Cartesian target via the
// existing SerialChain FK (krs::dyn). Joint limits clamp q_next.
//
// All SI: metres, radians, seconds. Built on RobotDynamics.hpp (no new deps).
// ===========================================================================
#include "RobotDynamics.hpp"
#include <Eigen/Dense>
#include <vector>

namespace krs::rl {

// -------------------------------------------------------------------------
// Reference joint trajectory: a set of q_ref samples q[k] at monotonically
// increasing times t[k]. q(t) is linear interpolation between the bracketing
// samples, clamped to the endpoints outside [t.front(), t.back()].
// -------------------------------------------------------------------------
struct RefTrajectory {
    std::vector<double>          t;   // sample times (strictly increasing, size == q.size())
    std::vector<Eigen::VectorXd> q;   // q_ref samples (all same dimension nq)
};

// Linearly interpolate the reference at time `time`, clamping at the ends.
Eigen::VectorXd sampleRef(const RefTrajectory& ref, double time);

// -------------------------------------------------------------------------
// One environment transition result (the gym (obs, reward, done) tuple).
// -------------------------------------------------------------------------
struct StepResult {
    Eigen::VectorXd obs;       // observation AFTER the step (see obsDim())
    double          reward = 0.0;
    bool            done   = false;
};

// -------------------------------------------------------------------------
// Environment configuration knobs.
//   reward = −imitationWeight·‖q−q_ref(t)‖²  +  taskWeight·(task term)
// The task term rewards driving the EE toward eeTarget (negative squared
// Cartesian distance), so a residual that steers the EE toward the target
// raises it. scrambleReward is the NEG-CTRL knob: it flips the imitation-term
// sign so the pure reference no longer maximizes reward (proving the real
// reward genuinely tracks the reference and the imitation test is non-vacuous).
// -------------------------------------------------------------------------
struct RlEnvConfig {
    double dt             = 0.02;   // control period (s); each step advances t by dt
    double residualScale  = 0.2;    // action is scaled by this before adding to q_ref
    double imitationWeight = 1.0;   // weight on −‖q−q_ref‖² imitation reward
    double taskWeight      = 0.0;   // weight on the (optional) Cartesian task reward
    Eigen::Vector3d eeTarget = Eigen::Vector3d::Zero();  // Cartesian EE target (task term)
    int    eeBody = -1;             // body index for the EE pose (SerialChain::bodyPose)
    bool   scrambleReward = false;  // NEG-CTRL: flip the imitation-term sign (reward = +‖q−q_ref‖²)
};

// -------------------------------------------------------------------------
// The gym-style environment. Holds a copy of the chain + reference + config
// and the mutable rollout state (q, time). Deterministic across constructions:
// two envs stepped through the same action sequence produce identical streams.
// -------------------------------------------------------------------------
class RlEnv {
public:
    RlEnv(krs::dyn::SerialChain chain, RefTrajectory ref, RlEnvConfig cfg);

    // Reset to t=0, q=q_ref(0); returns the initial observation.
    Eigen::VectorXd reset();

    // Apply an action (a residual of dimension nq). The commanded config is
    //   q_next = clamp( q_ref(t+dt) + residualScale·action )
    // clamped to the joint limits (qLower/qUpper) of the chain's DynJoints, then
    // time advances by dt. Returns the (obs, reward, done) tuple.
    StepResult step(const Eigen::VectorXd& action);

    int    actionDim() const;   // == nq (one residual per movable dof)
    int    obsDim()    const;   // == 2*nq + 1  (obs = concat[ q, q_ref(t)−q, t/duration ])
    double duration()  const;   // ref.t.back() (0 if <2 samples)

private:
    // Assemble the observation at the current state: concat[ q, q_ref(t)−q, t/duration ].
    Eigen::VectorXd observe() const;
    // The scalar reward for the current q at the current time.
    double reward() const;
    // Clamp a config to the chain's per-joint position limits.
    Eigen::VectorXd clampToLimits(const Eigen::VectorXd& q) const;

    krs::dyn::SerialChain chain_;   // kinematics for the task term + limits
    RefTrajectory         ref_;     // reference trajectory
    RlEnvConfig           cfg_;      // config knobs
    Eigen::VectorXd       q_;        // current joint config
    double                time_ = 0.0;  // current time (s)
    int                   nq_   = 0;    // # movable dofs
    std::vector<double>   qLower_, qUpper_;  // per-dof limits pulled from the chain
};

// GATE (env KRS_RLENV_SELFTEST): determinism, imitation-maximized-by-reference,
// residual-improves-task, dims/reset, + a NEG-CTRL (scrambleReward) that flips
// the imitation reward so the reference no longer wins (proves non-vacuity).
// Pure CPU/Eigen, no GL/PhysX. Prints "[rlenv] ..."; returns true iff all pass.
bool runRlEnvGate();

} // namespace krs::rl
