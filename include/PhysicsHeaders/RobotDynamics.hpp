#pragma once
// ===========================================================================
// Phase A — Eigen-native rigid-body dynamics oracle (krs::dyn).
//
// A self-contained, permissive (no third-party robotics dep) reference for
// serial / branching kinematic trees PLUS explicit loop-closure constraints
// (the FANUC parallelogram). It is the analytical ground truth that GATE A
// validates the PhysX articulation against, and the FK cross-check oracle the
// amendment requires kept independent of any library.
//
// Algorithms (textbook-standard, Featherstone "Rigid Body Dynamics Algorithms"
// + Buss 2009 IK survey):
//   * FK            : homogeneous SE(3) frame composition (exact to fp).
//   * Jacobian      : geometric, per-column (revolute: a×(p−o), a;
//                                            prismatic: a, 0).
//   * RNEA          : spatial Newton–Euler inverse dynamics  τ = ID(q,q̇,q̈,g).
//   * CRBA          : composite-rigid-body mass matrix  M(q)  (independent of
//                     RNEA, so M_CRBA vs M_RNEA-columns is a genuine cross-check).
//   * Fwd dynamics  : q̈ = M⁻¹(τ − b),  b = RNEA(q,q̇,0,g)   (small n → trivial).
//   * DLS IK        : Δq = Jᵀ(JJᵀ+λ²I)⁻¹ e  with e- and step-clamping.
//   * Loop closure  : Newton solve of a frame-coincidence constraint between two
//                     tree bodies (cut-joint), + the constraint Jacobian — makes
//                     the oracle "constraint-aware" for the closed-loop robot.
//
// Frames: spatial 6-vectors are [angular(3); linear(3)]. The geometric Jacobian
// returned by jacobian() is [linear(3); angular(3)] rows (the IK convention).
// All SI: metres, radians, kg, N, N·m.
// ===========================================================================
#include <Eigen/Dense>
#include <vector>
#include <string>

namespace krs::dyn {

enum class JType { Fixed, Revolute, Prismatic };

// Inertial properties of a tree body, expressed in the body (post-joint) frame.
struct DynBody {
    double mass = 1.0;
    Eigen::Vector3d com = Eigen::Vector3d::Zero();              // COM in body frame
    Eigen::Matrix3d inertiaCom = Eigen::Matrix3d::Identity();   // I about COM, body axes
};

// Tree edge: the joint that attaches a body to its parent.
struct DynJoint {
    JType type = JType::Revolute;
    int parent = -1;                                            // parent body index, −1 = world
    Eigen::Matrix3d Rtree = Eigen::Matrix3d::Identity();        // parent → joint-frame rotation (q=0)
    Eigen::Vector3d ptree = Eigen::Vector3d::Zero();            // parent → joint-frame origin   (q=0)
    Eigen::Vector3d axis = Eigen::Vector3d::UnitZ();            // joint axis in the joint frame (unit)
    double qLower = -1e30, qUpper = 1e30;                       // position limits (IK clamps to these)
};

// Frame-coincidence loop-closure constraint: frame `frameA` rigidly fixed on
// body `bodyA` must coincide with `frameB` on body `bodyB`. (bodyA/bodyB = −1
// means the world frame.) Used to close the cut parallelogram loop.
struct LoopConstraint {
    int bodyA = -1; Eigen::Matrix3d RA = Eigen::Matrix3d::Identity(); Eigen::Vector3d pA = Eigen::Vector3d::Zero();
    int bodyB = -1; Eigen::Matrix3d RB = Eigen::Matrix3d::Identity(); Eigen::Vector3d pB = Eigen::Vector3d::Zero();
};

struct Pose { Eigen::Matrix3d R = Eigen::Matrix3d::Identity(); Eigen::Vector3d p = Eigen::Vector3d::Zero(); };

class SerialChain {
public:
    // Build (parent must already exist; fixed joints carry no dof). Returns body index.
    int addBody(const DynJoint& joint, const DynBody& body);

    int nbody() const { return int(bodies_.size()); }
    int nq() const { return ndof_; }                  // # of movable dofs (1 per revolute/prismatic)
    int dofOf(int body) const { return dofIndex_[body]; }  // −1 if fixed
    const DynJoint& joint(int b) const { return joints_[b]; }

    // --- kinematics ---
    void fk(const Eigen::VectorXd& q, std::vector<Pose>& worldPose) const;  // per-body world pose
    Pose bodyPose(const Eigen::VectorXd& q, int body) const;
    // 6×nq geometric Jacobian (rows [linear;angular]) of point pLocal (body frame) on `body`.
    Eigen::MatrixXd jacobian(const Eigen::VectorXd& q, int body,
                             const Eigen::Vector3d& pLocal = Eigen::Vector3d::Zero()) const;

    // --- dynamics ---
    Eigen::VectorXd rnea(const Eigen::VectorXd& q, const Eigen::VectorXd& qd,
                         const Eigen::VectorXd& qdd, const Eigen::Vector3d& gravity) const;
    Eigen::MatrixXd massMatrix(const Eigen::VectorXd& q) const;                 // CRBA
    Eigen::VectorXd biasForces(const Eigen::VectorXd& q, const Eigen::VectorXd& qd,
                               const Eigen::Vector3d& gravity) const;           // C q̇ + g  = RNEA(…,q̈=0)
    Eigen::VectorXd forwardDynamics(const Eigen::VectorXd& q, const Eigen::VectorXd& qd,
                                    const Eigen::VectorXd& tau, const Eigen::Vector3d& gravity) const;

    // --- DLS inverse kinematics for a target pose of `body` ---
    struct IKResult { bool ok = false; int iters = 0; double posErr = 0, rotErr = 0; };
    IKResult ik(const Pose& target, int body, Eigen::VectorXd& q,
                double lambda = 0.05, int maxIters = 200, double tol = 1e-6) const;

    // --- loop closure (constraint-aware) ---
    // 6-vector residual [Δp(3); Δrot(3)] of constraint c at configuration q.
    Eigen::Matrix<double,6,1> loopResidual(const LoopConstraint& c, const Eigen::VectorXd& q) const;
    // Newton-solve the `dep` dofs so every constraint residual → 0, holding all
    // other dofs fixed. Returns final max residual norm.
    double closeLoops(const std::vector<LoopConstraint>& cons, const std::vector<int>& dep,
                      Eigen::VectorXd& q, int maxIters = 100, double tol = 1e-12) const;

private:
    // local joint transform parent→body for dof value qv
    void jointTransform(int b, double qv, Eigen::Matrix3d& R, Eigen::Vector3d& p) const;
    std::vector<DynJoint> joints_;
    std::vector<DynBody>  bodies_;
    std::vector<int>      dofIndex_;   // body → dof column (−1 if fixed)
    int ndof_ = 0;
};

// GATE-A self-test battery (A1 FK, mass-matrix cross-check, dynamics round-trip,
// A4 IK round-trip, loop-closure residual). Pure CPU/Eigen, no GL, no PhysX.
// Prints "[dyn selftest] ... PASS/FAIL"; returns true iff all sub-tests pass.
bool runSelfTests();

} // namespace krs::dyn
