#include "RobotDynamics.hpp"

#include <Eigen/Geometry>
#include <cmath>
#include <cstdio>
#include <random>
#include <algorithm>
#include <limits>

namespace krs::dyn {

// ---------------------------------------------------------------------------
// spatial-algebra helpers ([angular(3); linear(3)] convention, Featherstone)
// ---------------------------------------------------------------------------
namespace {
inline Eigen::Matrix3d skew(const Eigen::Vector3d& v) {
    Eigen::Matrix3d m;
    m <<      0, -v.z(),  v.y(),
          v.z(),      0, -v.x(),
         -v.y(),  v.x(),      0;
    return m;
}
// Motion transform parent->child (6x6): child rotation R (child-in-parent),
// child origin p (in parent).  v_child = X * v_parent.
inline Eigen::Matrix<double,6,6> Xmotion(const Eigen::Matrix3d& R, const Eigen::Vector3d& p) {
    const Eigen::Matrix3d E = R.transpose();              // parent->child rotation
    Eigen::Matrix<double,6,6> X = Eigen::Matrix<double,6,6>::Zero();
    X.topLeftCorner<3,3>()     = E;
    X.bottomRightCorner<3,3>() = E;
    X.bottomLeftCorner<3,3>()  = -E * skew(p);
    return X;
}
// Spatial cross-product matrices for v=[w;vl].
inline Eigen::Matrix<double,6,6> crm(const Eigen::Matrix<double,6,1>& v) {
    const Eigen::Vector3d w = v.head<3>(), vl = v.tail<3>();
    Eigen::Matrix<double,6,6> m = Eigen::Matrix<double,6,6>::Zero();
    m.topLeftCorner<3,3>()     = skew(w);
    m.bottomRightCorner<3,3>() = skew(w);
    m.bottomLeftCorner<3,3>()  = skew(vl);
    return m;
}
inline Eigen::Matrix<double,6,6> crf(const Eigen::Matrix<double,6,1>& v) {
    return -crm(v).transpose();
}
// Spatial inertia (6x6) in body frame: mass m, COM c, inertia-about-COM Ic.
inline Eigen::Matrix<double,6,6> spatialInertia(double m, const Eigen::Vector3d& c,
                                                const Eigen::Matrix3d& Ic) {
    const Eigen::Matrix3d S = skew(c);
    const Eigen::Matrix3d Io = Ic - m * S * S;            // inertia about body-frame origin
    Eigen::Matrix<double,6,6> I = Eigen::Matrix<double,6,6>::Zero();
    I.topLeftCorner<3,3>()     = Io;
    I.topRightCorner<3,3>()    = m * S;
    I.bottomLeftCorner<3,3>()  = m * S.transpose();
    I.bottomRightCorner<3,3>() = m * Eigen::Matrix3d::Identity();
    return I;
}
} // namespace

// ---------------------------------------------------------------------------
int SerialChain::addBody(const DynJoint& joint, const DynBody& body) {
    joints_.push_back(joint);
    bodies_.push_back(body);
    const int idx = int(bodies_.size()) - 1;
    if (joint.type == JType::Fixed) dofIndex_.push_back(-1);
    else                            dofIndex_.push_back(ndof_++);
    return idx;
}

void SerialChain::jointTransform(int b, double qv, Eigen::Matrix3d& R, Eigen::Vector3d& p) const {
    const DynJoint& j = joints_[b];
    Eigen::Matrix3d Rj = Eigen::Matrix3d::Identity();
    Eigen::Vector3d pj = Eigen::Vector3d::Zero();
    if (j.type == JType::Revolute)
        Rj = Eigen::AngleAxisd(qv, j.axis.normalized()).toRotationMatrix();
    else if (j.type == JType::Prismatic)
        pj = j.axis.normalized() * qv;
    R = j.Rtree * Rj;
    if (j.type == JType::Revolute && j.hasAxisPoint) {
        // Rotate the child RIGIDLY about the axis line through axisPoint A (parent frame): the origin
        // stays at ptree at q=0 and circles A as q turns. Rrot = the parent-frame rotation by q about
        // the axis. p(q) = A + Rrot*(ptree - A). At q=0 -> ptree; with A==ptree -> ptree (classic model).
        const Eigen::Matrix3d Rrot = j.Rtree * Rj * j.Rtree.transpose();
        p = j.axisPoint + Rrot * (j.ptree - j.axisPoint);
    } else {
        p = j.ptree + j.Rtree * pj;
    }
}

void SerialChain::fk(const Eigen::VectorXd& q, std::vector<Pose>& wp) const {
    wp.assign(bodies_.size(), Pose());
    for (int b = 0; b < int(bodies_.size()); ++b) {
        const int d = dofIndex_[b];
        const double qv = (d >= 0) ? q[d] : 0.0;
        Eigen::Matrix3d Rrel; Eigen::Vector3d prel;
        jointTransform(b, qv, Rrel, prel);
        const int par = joints_[b].parent;
        if (par < 0) { wp[b].R = Rrel; wp[b].p = prel; }
        else { wp[b].R = wp[par].R * Rrel; wp[b].p = wp[par].p + wp[par].R * prel; }
    }
}

Pose SerialChain::bodyPose(const Eigen::VectorXd& q, int body) const {
    std::vector<Pose> wp; fk(q, wp); return wp[body];
}

Eigen::MatrixXd SerialChain::jacobian(const Eigen::VectorXd& q, int body,
                                      const Eigen::Vector3d& pLocal) const {
    std::vector<Pose> wp; fk(q, wp);
    const Eigen::Vector3d pw = wp[body].p + wp[body].R * pLocal;
    Eigen::MatrixXd J = Eigen::MatrixXd::Zero(6, ndof_);
    // walk ancestors of `body` (inclusive); assign a column per movable joint.
    for (int b = body; b >= 0; b = joints_[b].parent) {
        const int d = dofIndex_[b];
        if (d < 0) continue;
        const Eigen::Vector3d aw = (wp[b].R * joints_[b].axis.normalized()).normalized();
        // Pivot = a point on the axis in WORLD. With an axisPoint the axis passes through it (parent
        // frame -> world), NOT through the child origin; else it is the child origin (classic no-op).
        Eigen::Vector3d ow = wp[b].p;
        if (joints_[b].hasAxisPoint) {
            const int par = joints_[b].parent;
            ow = (par < 0) ? joints_[b].axisPoint : (wp[par].p + wp[par].R * joints_[b].axisPoint);
        }
        if (joints_[b].type == JType::Revolute) {
            J.block<3,1>(0, d) = aw.cross(pw - ow);   // linear
            J.block<3,1>(3, d) = aw;                  // angular
        } else { // prismatic
            J.block<3,1>(0, d) = aw;
            J.block<3,1>(3, d) = Eigen::Vector3d::Zero();
        }
    }
    return J;
}

// --- RNEA inverse dynamics: tau = ID(q, qd, qdd, gravity) -------------------
Eigen::VectorXd SerialChain::rnea(const Eigen::VectorXd& q, const Eigen::VectorXd& qd,
                                  const Eigen::VectorXd& qdd, const Eigen::Vector3d& gravity) const {
    const int nb = int(bodies_.size());
    std::vector<Eigen::Matrix<double,6,6>> X(nb);
    std::vector<Eigen::Matrix<double,6,1>> S(nb), v(nb), a(nb), f(nb);
    // base spatial acceleration carries gravity: a0_linear = -gravity.
    Eigen::Matrix<double,6,1> a0; a0.setZero(); a0.tail<3>() = -gravity;
    Eigen::Matrix<double,6,1> v0; v0.setZero();

    for (int b = 0; b < nb; ++b) {
        const int d = dofIndex_[b];
        const double qv = (d >= 0) ? q[d] : 0.0;
        const double dqv = (d >= 0) ? qd[d] : 0.0;
        const double ddqv = (d >= 0) ? qdd[d] : 0.0;
        Eigen::Matrix3d Rrel; Eigen::Vector3d prel;
        jointTransform(b, qv, Rrel, prel);
        X[b] = Xmotion(Rrel, prel);
        // motion subspace in body frame
        S[b].setZero();
        const Eigen::Vector3d ax = joints_[b].axis.normalized();
        if (joints_[b].type == JType::Revolute)  S[b].head<3>() = ax;
        else if (joints_[b].type == JType::Prismatic) S[b].tail<3>() = ax;
        const int par = joints_[b].parent;
        const Eigen::Matrix<double,6,1>& vp = (par < 0) ? v0 : v[par];
        const Eigen::Matrix<double,6,1>& ap = (par < 0) ? a0 : a[par];
        const Eigen::Matrix<double,6,1> vJ = S[b] * dqv;
        v[b] = X[b] * vp + vJ;
        a[b] = X[b] * ap + S[b] * ddqv + crm(v[b]) * vJ;
        const Eigen::Matrix<double,6,6> I = spatialInertia(bodies_[b].mass, bodies_[b].com,
                                                           bodies_[b].inertiaCom);
        f[b] = I * a[b] + crf(v[b]) * (I * v[b]);
    }
    Eigen::VectorXd tau = Eigen::VectorXd::Zero(ndof_);
    for (int b = nb - 1; b >= 0; --b) {
        const int d = dofIndex_[b];
        if (d >= 0) tau[d] = S[b].dot(f[b]);
        const int par = joints_[b].parent;
        if (par >= 0) f[par] += X[b].transpose() * f[b];
    }
    return tau;
}

// --- CRBA mass matrix (independent of RNEA) ---------------------------------
Eigen::MatrixXd SerialChain::massMatrix(const Eigen::VectorXd& q) const {
    const int nb = int(bodies_.size());
    std::vector<Eigen::Matrix<double,6,6>> X(nb), Ic(nb);
    std::vector<Eigen::Matrix<double,6,1>> S(nb);
    for (int b = 0; b < nb; ++b) {
        const int d = dofIndex_[b];
        const double qv = (d >= 0) ? q[d] : 0.0;
        Eigen::Matrix3d Rrel; Eigen::Vector3d prel;
        jointTransform(b, qv, Rrel, prel);
        X[b] = Xmotion(Rrel, prel);
        S[b].setZero();
        const Eigen::Vector3d ax = joints_[b].axis.normalized();
        if (joints_[b].type == JType::Revolute)  S[b].head<3>() = ax;
        else if (joints_[b].type == JType::Prismatic) S[b].tail<3>() = ax;
        Ic[b] = spatialInertia(bodies_[b].mass, bodies_[b].com, bodies_[b].inertiaCom);
    }
    // composite inertia, leaves -> root
    for (int b = nb - 1; b >= 0; --b) {
        const int par = joints_[b].parent;
        if (par >= 0) Ic[par] += X[b].transpose() * Ic[b] * X[b];
    }
    Eigen::MatrixXd H = Eigen::MatrixXd::Zero(ndof_, ndof_);
    for (int b = 0; b < nb; ++b) {
        const int di = dofIndex_[b];
        if (di < 0) continue;
        Eigen::Matrix<double,6,1> F = Ic[b] * S[b];
        H(di, di) = S[b].dot(F);
        int j = b;
        while (joints_[j].parent >= 0) {
            F = X[j].transpose() * F;
            j = joints_[j].parent;
            const int dj = dofIndex_[j];
            if (dj < 0) continue;
            H(di, dj) = S[j].dot(F);
            H(dj, di) = H(di, dj);
        }
    }
    return H;
}

Eigen::VectorXd SerialChain::biasForces(const Eigen::VectorXd& q, const Eigen::VectorXd& qd,
                                        const Eigen::Vector3d& gravity) const {
    return rnea(q, qd, Eigen::VectorXd::Zero(ndof_), gravity);
}

Eigen::VectorXd SerialChain::forwardDynamics(const Eigen::VectorXd& q, const Eigen::VectorXd& qd,
                                             const Eigen::VectorXd& tau, const Eigen::Vector3d& gravity) const {
    const Eigen::MatrixXd M = massMatrix(q);
    const Eigen::VectorXd b = biasForces(q, qd, gravity);
    return M.ldlt().solve(tau - b);
}

// --- DLS inverse kinematics -------------------------------------------------
SerialChain::IKResult SerialChain::ik(const Pose& target, int body, Eigen::VectorXd& q,
                                      double lambda, int maxIters, double tol,
                                      const Eigen::VectorXd* qSeed, double holdWeight, double rotWeight) const {
    IKResult r;
    r.bestQ = q;                      // closest-reachable seen so far (starts at the seed pose)
    const double maxLinStep = 0.05;   // m per iteration
    const double maxAngStep = 0.20;   // rad per iteration

    // REACH CLAMP: a target GROSSLY beyond the kinematic reach is projected inward before solving, so
    // a 10 m drag converges to a bounded pose instead of commanding every joint toward its stop -- the
    // "arm extends forward violently" case. The threshold is a GENEROUS over-estimate (1.5x the summed
    // joint offsets + margin) so only pathological over-drags are touched; a legitimately reachable or
    // near-reach target is NEVER clamped (bit-exact for the round-trip self-test + normal drags). The
    // real no-fling guarantee is bestQ (closest-reachable commit) + clampDof; this just tames NaN-scale
    // targets and reports the clamp for the gate.
    Pose tgt = target;
    {
        double reach = 0.0;
        for (int b = body; b >= 0; b = joints_[b].parent) reach += joints_[b].ptree.norm();
        const double cap = 1.5 * reach + 0.10;    // generous: only gross over-reach clamps
        const double d = tgt.p.norm();
        if (cap > 0.0 && d > cap) { tgt.p *= (cap * 0.999) / d; r.clampedToReach = true; }
    }

    double bestErr = std::numeric_limits<double>::max();
    for (int it = 0; it < maxIters; ++it) {
        std::vector<Pose> wp; fk(q, wp);
        const Eigen::Vector3d ep = tgt.p - wp[body].p;
        const Eigen::Matrix3d Rerr = tgt.R * wp[body].R.transpose();
        Eigen::AngleAxisd aa(Rerr);
        Eigen::Vector3d eo = aa.axis() * aa.angle();
        r.posErr = ep.norm(); r.rotErr = eo.norm(); r.iters = it;
        // best-Q is POSITION-DOMINANT: a drag holds orientation only softly, so reaching the target
        // point must not be vetoed by the orientation drift it necessarily incurs (equal weighting
        // would pick "don't move" as best for a positional drag). rotErr is a mild tiebreaker.
        const double sel = r.posErr + rotWeight * r.rotErr;
        if (sel < bestErr) { bestErr = sel; r.bestQ = q; }
        if (r.posErr < tol && r.rotErr < tol) { r.ok = true; r.bestQ = q; return r; }
        Eigen::Matrix<double,6,1> e;
        // clamp the error so a far/unreachable target can't drive a huge step
        Eigen::Vector3d epc = ep, eoc = eo;
        if (epc.norm() > maxLinStep) epc *= maxLinStep / epc.norm();
        if (eoc.norm() > maxAngStep) eoc *= maxAngStep / eoc.norm();
        e << epc, eoc;
        const Eigen::MatrixXd J = jacobian(q, body);
        const Eigen::LDLT<Eigen::MatrixXd> ldlt(J * J.transpose()
                                  + lambda * lambda * Eigen::MatrixXd::Identity(6, 6));
        Eigen::VectorXd dq = J.transpose() * ldlt.solve(e);
        // NULL-SPACE HOLD-POSTURE: pull the undragged dofs toward qSeed through the SAME damped
        // inverse (no fresh pseudo-inverse). (I - J^+ J) z lives in the task null space, so the
        // primary reach is undisturbed while redundant dofs resist being swept -- the arm "tries
        // to hold its position". Disabled (bit-exact) when holdWeight==0 or qSeed is null/mismatched.
        if (qSeed && holdWeight > 0.0 && qSeed->size() == q.size()) {
            const Eigen::VectorXd z = holdWeight * (*qSeed - q);
            dq += z - J.transpose() * ldlt.solve(J * z);
        }
        const double dqn = dq.norm();
        if (dqn > maxAngStep) dq *= maxAngStep / dqn;   // per-step joint clamp
        q += dq;
        for (int b = 0; b < int(bodies_.size()); ++b) {     // respect joint limits
            const int d = dofIndex_[b];
            if (d >= 0) q[d] = std::min(std::max(q[d], joints_[b].qLower), joints_[b].qUpper);
        }
        if (!q.allFinite()) { r.ok = false; return r; }     // never propagate NaN (bestQ stays finite)
    }
    // final residual after the loop
    std::vector<Pose> wp; fk(q, wp);
    r.posErr = (tgt.p - wp[body].p).norm();
    Eigen::AngleAxisd aa(tgt.R * wp[body].R.transpose());
    r.rotErr = (aa.axis() * aa.angle()).norm();
    if (r.posErr + rotWeight * r.rotErr < bestErr) { r.bestQ = q; }
    r.ok = (r.posErr < tol && r.rotErr < tol);
    return r;
}

// --- loop closure -----------------------------------------------------------
Eigen::Matrix<double,6,1> SerialChain::loopResidual(const LoopConstraint& c,
                                                    const Eigen::VectorXd& q) const {
    std::vector<Pose> wp; fk(q, wp);
    auto frameWorld = [&](int body, const Eigen::Matrix3d& R, const Eigen::Vector3d& p,
                          Eigen::Matrix3d& Rw, Eigen::Vector3d& pw) {
        if (body < 0) { Rw = R; pw = p; }
        else { Rw = wp[body].R * R; pw = wp[body].p + wp[body].R * p; }
    };
    Eigen::Matrix3d RAw, RBw; Eigen::Vector3d pAw, pBw;
    frameWorld(c.bodyA, c.RA, c.pA, RAw, pAw);
    frameWorld(c.bodyB, c.RB, c.pB, RBw, pBw);
    Eigen::Matrix<double,6,1> res;
    res.head<3>() = pBw - pAw;
    Eigen::AngleAxisd aa(RAw.transpose() * RBw);
    res.tail<3>() = aa.axis() * aa.angle();
    return res;
}

double SerialChain::closeLoops(const std::vector<LoopConstraint>& cons, const std::vector<int>& dep,
                               Eigen::VectorXd& q, int maxIters, double tol) const {
    const int m = int(cons.size()) * 6;
    const int n = int(dep.size());
    const double h = 1e-7;
    double last = 1e30;
    for (int it = 0; it < maxIters; ++it) {
        Eigen::VectorXd r(m);
        for (int c = 0; c < int(cons.size()); ++c) r.segment<6>(6 * c) = loopResidual(cons[c], q);
        last = r.cwiseAbs().maxCoeff();
        if (last < tol) break;
        Eigen::MatrixXd Jr(m, n);
        for (int k = 0; k < n; ++k) {
            Eigen::VectorXd qp = q; qp[dep[k]] += h;
            Eigen::VectorXd rp(m);
            for (int c = 0; c < int(cons.size()); ++c) rp.segment<6>(6 * c) = loopResidual(cons[c], qp);
            Jr.col(k) = (rp - r) / h;
        }
        // Gauss–Newton step on the dependent coords (damped for rank safety).
        const Eigen::MatrixXd JtJ = Jr.transpose() * Jr
                                  + 1e-12 * Eigen::MatrixXd::Identity(n, n);
        const Eigen::VectorXd step = JtJ.ldlt().solve(Jr.transpose() * r);
        for (int k = 0; k < n; ++k) q[dep[k]] -= step[k];
        if (!q.allFinite()) break;
    }
    return last;
}

// ===========================================================================
// GATE-A self-test battery
// ===========================================================================
namespace {
// Build a random branch-free chain of `n` revolute joints with random axes,
// link offsets, masses and inertias (used for FK/M/dynamics cross-checks).
SerialChain randomChain(int n, std::mt19937& rng) {
    std::uniform_real_distribution<double> U(-1.0, 1.0), P(0.1, 0.6), Mu(0.5, 3.0);
    SerialChain c;
    for (int i = 0; i < n; ++i) {
        DynJoint j; j.type = JType::Revolute;
        j.parent = i - 1;
        Eigen::Vector3d ax(U(rng), U(rng), U(rng));
        if (ax.norm() < 1e-3) ax = Eigen::Vector3d::UnitZ();
        j.axis = ax.normalized();
        j.Rtree = Eigen::AngleAxisd(U(rng) * 3.14159, Eigen::Vector3d(U(rng),U(rng),U(rng)).normalized()).toRotationMatrix();
        j.ptree = Eigen::Vector3d(P(rng), U(rng)*0.2, U(rng)*0.2);
        DynBody b; b.mass = Mu(rng);
        b.com = Eigen::Vector3d(U(rng)*0.1, U(rng)*0.1, U(rng)*0.1);
        // SPD random inertia: A A^T + diag
        Eigen::Matrix3d A; for (int r=0;r<3;++r) for (int cc=0;cc<3;++cc) A(r,cc)=U(rng);
        b.inertiaCom = A*A.transpose()*0.05 + Eigen::Matrix3d::Identity()*0.02;
        c.addBody(j, b);
    }
    return c;
}

// Random BRANCHING tree mixing revolute / prismatic / fixed joints (property/fuzz):
// parent[i] is any earlier body (or world) so the tree can branch.
SerialChain randomTree(int n, std::mt19937& rng, bool allowPrismatic, bool allowFixed) {
    std::uniform_real_distribution<double> U(-1.0, 1.0), P(0.05, 0.5), Mu(0.4, 3.0);
    SerialChain c;
    for (int i = 0; i < n; ++i) {
        DynJoint j;
        std::uniform_int_distribution<int> par(-1, i - 1);
        j.parent = (i == 0) ? -1 : par(rng);              // branching
        const double r = U(rng);
        if (allowFixed && r > 0.7)      j.type = JType::Fixed;
        else if (allowPrismatic && r < -0.4) j.type = JType::Prismatic;
        else                            j.type = JType::Revolute;
        Eigen::Vector3d ax(U(rng), U(rng), U(rng));
        if (ax.norm() < 1e-3) ax = Eigen::Vector3d::UnitZ();
        j.axis = ax.normalized();
        j.Rtree = Eigen::AngleAxisd(U(rng) * 3.14159,
                    Eigen::Vector3d(U(rng), U(rng), U(rng)).normalized()).toRotationMatrix();
        j.ptree = Eigen::Vector3d(P(rng), U(rng) * 0.2, U(rng) * 0.2);
        DynBody b; b.mass = Mu(rng);
        b.com = Eigen::Vector3d(U(rng) * 0.1, U(rng) * 0.1, U(rng) * 0.1);
        Eigen::Matrix3d A; for (int r2 = 0; r2 < 3; ++r2) for (int cc = 0; cc < 3; ++cc) A(r2, cc) = U(rng);
        b.inertiaCom = A * A.transpose() * 0.05 + Eigen::Matrix3d::Identity() * 0.02;
        c.addBody(j, b);
    }
    return c;
}
} // namespace

bool runSelfTests() {
    using std::printf;
    std::mt19937 rng(12345);
    bool allPass = true;
    printf("[dyn selftest] Eigen-native Featherstone oracle (GATE A reference)\n");

    // --- A1: FK vs closed-form planar RR (1e-6 target; expect ~1e-12) -------
    {
        const double L1 = 1.0, L2 = 0.7;
        SerialChain c;
        DynJoint j1; j1.type=JType::Revolute; j1.parent=-1; j1.axis=Eigen::Vector3d::UnitZ();
        DynBody b1; b1.mass=1.0; c.addBody(j1,b1);
        DynJoint j2; j2.type=JType::Revolute; j2.parent=0; j2.axis=Eigen::Vector3d::UnitZ();
        j2.ptree=Eigen::Vector3d(L1,0,0);
        DynBody b2; b2.mass=1.0; c.addBody(j2,b2);
        std::uniform_real_distribution<double> A(-3.14,3.14);
        double maxErr = 0;
        for (int t=0;t<100;++t) {
            Eigen::VectorXd q(2); q<<A(rng),A(rng);
            const Pose ee = c.bodyPose(q,1);
            const Eigen::Vector3d p = ee.p + ee.R*Eigen::Vector3d(L2,0,0);
            const double xc = L1*std::cos(q[0]) + L2*std::cos(q[0]+q[1]);
            const double yc = L1*std::sin(q[0]) + L2*std::sin(q[0]+q[1]);
            maxErr = std::max(maxErr, (p - Eigen::Vector3d(xc,yc,0)).norm());
        }
        const bool pass = maxErr < 1e-12;   // exact fp composition; measured ~5e-16
        printf("[dyn selftest]  A1 FK vs closed-form RR: maxErr=%.3e  %s\n", maxErr, pass?"PASS":"FAIL");
        allPass &= pass;
    }

    // --- A1b: FK two independent ways on a random 4-link spatial chain ------
    {
        double maxErr = 0;
        for (int trial=0; trial<20; ++trial) {
            SerialChain c = randomChain(4, rng);
            std::uniform_real_distribution<double> A(-3.0,3.0);
            Eigen::VectorXd q(4); for (int i=0;i<4;++i) q[i]=A(rng);
            std::vector<Pose> wp; c.fk(q,wp);
            // independent recomputation by explicit homogeneous product
            Eigen::Matrix3d R=Eigen::Matrix3d::Identity(); Eigen::Vector3d p=Eigen::Vector3d::Zero();
            for (int b=0;b<4;++b) {
                const DynJoint& j=c.joint(b);
                Eigen::Matrix3d Rrel=j.Rtree*Eigen::AngleAxisd(q[b],j.axis.normalized()).toRotationMatrix();
                Eigen::Vector3d prel=j.ptree;
                p = p + R*prel; R = R*Rrel;
                maxErr=std::max(maxErr,(p-wp[b].p).norm());
                maxErr=std::max(maxErr,(R-wp[b].R).norm());
            }
        }
        const bool pass = maxErr < 1e-12;
        printf("[dyn selftest]  A1b FK two-way agreement: maxErr=%.3e  %s\n", maxErr, pass?"PASS":"FAIL");
        allPass &= pass;
    }

    // --- mass-matrix cross-check: CRBA vs RNEA columns (1e-9) ---------------
    {
        double maxErr = 0, maxAsym = 0;
        for (int trial=0; trial<20; ++trial) {
            const int n = 5;
            SerialChain c = randomChain(n, rng);
            std::uniform_real_distribution<double> A(-2.0,2.0);
            Eigen::VectorXd q(n); for (int i=0;i<n;++i) q[i]=A(rng);
            const Eigen::MatrixXd Mc = c.massMatrix(q);
            Eigen::MatrixXd Mr(n,n);
            for (int i=0;i<n;++i) {
                Eigen::VectorXd e=Eigen::VectorXd::Zero(n); e[i]=1.0;
                Mr.col(i) = c.rnea(q, Eigen::VectorXd::Zero(n), e, Eigen::Vector3d::Zero());
            }
            maxErr = std::max(maxErr, (Mc-Mr).cwiseAbs().maxCoeff());
            maxAsym = std::max(maxAsym, (Mc-Mc.transpose()).cwiseAbs().maxCoeff());
        }
        const bool pass = maxErr < 1e-9 && maxAsym < 1e-9;
        printf("[dyn selftest]  M: CRBA vs RNEA-cols err=%.3e  asym=%.3e  %s\n",
               maxErr, maxAsym, pass?"PASS":"FAIL");
        allPass &= pass;
    }

    // --- dynamics: pendulum closed form + RNEA<->forwardDynamics round-trip -
    {
        // single pendulum: revolute Z at origin, point mass m at (r,0,0), g=-Y.
        const double r=0.8, m=2.0, g=9.81;
        SerialChain c;
        DynJoint j; j.type=JType::Revolute; j.parent=-1; j.axis=Eigen::Vector3d::UnitZ();
        DynBody b; b.mass=m; b.com=Eigen::Vector3d(r,0,0); b.inertiaCom=Eigen::Matrix3d::Zero();
        c.addBody(j,b);
        std::uniform_real_distribution<double> A(-3.0,3.0);
        double maxErr=0;
        for (int t=0;t<50;++t) {
            const double th=A(rng);
            Eigen::VectorXd q(1); q<<th;
            Eigen::VectorXd qd=Eigen::VectorXd::Zero(1), tau=Eigen::VectorXd::Zero(1);
            const Eigen::VectorXd qdd = c.forwardDynamics(q,qd,tau,Eigen::Vector3d(0,-g,0));
            const double closed = -(g/r)*std::cos(th);   // theta_ddot = -(g/r)cos(theta)
            maxErr=std::max(maxErr,std::abs(qdd[0]-closed));
        }
        const bool passP = maxErr < 1e-9;
        printf("[dyn selftest]  pendulum qdd vs closed form: maxErr=%.3e  %s\n", maxErr, passP?"PASS":"FAIL");
        allPass &= passP;

        // round-trip on a random chain
        double maxRt=0;
        for (int trial=0; trial<20; ++trial) {
            const int n=5; SerialChain cc=randomChain(n,rng);
            Eigen::VectorXd q(n),qd(n),qdd(n);
            for (int i=0;i<n;++i){q[i]=A(rng);qd[i]=A(rng);qdd[i]=A(rng);}
            const Eigen::Vector3d grav(0,-g,0);
            const Eigen::VectorXd tau=cc.rnea(q,qd,qdd,grav);
            const Eigen::VectorXd qdd2=cc.forwardDynamics(q,qd,tau,grav);
            maxRt=std::max(maxRt,(qdd-qdd2).cwiseAbs().maxCoeff());
        }
        const bool passR = maxRt < 1e-8;
        printf("[dyn selftest]  RNEA<->ABA round-trip: maxErr=%.3e  %s\n", maxRt, passR?"PASS":"FAIL");
        allPass &= passR;
    }

    // --- A4: IK round-trip FK(IK(pose))~pose (1e-4) over 50 targets ---------
    {
        const int n=6; SerialChain c=randomChain(n,rng);
        std::uniform_real_distribution<double> A(-2.5,2.5);
        int ok=0; double maxErr=0;
        for (int t=0;t<50;++t) {
            Eigen::VectorXd qt(n); for (int i=0;i<n;++i) qt[i]=A(rng);
            const Pose target=c.bodyPose(qt,n-1);
            Eigen::VectorXd q(n); for (int i=0;i<n;++i) q[i]=qt[i]+0.3*A(rng); // perturbed seed
            const auto res=c.ik(target,n-1,q,0.02,300,1e-7);
            const Pose got=c.bodyPose(q,n-1);
            const double pe=(target.p-got.p).norm();
            Eigen::AngleAxisd aa(target.R*got.R.transpose());
            const double re=(aa.axis()*aa.angle()).norm();
            const double e=std::max(pe,re);
            if (e<1e-4) ++ok;
            maxErr=std::max(maxErr, (e<1e-4)? e : 0.0);
        }
        const bool pass = ok >= 48;   // >=96% of reachable round-trips converge
        printf("[dyn selftest]  A4 IK round-trip: %d/50 < 1e-4 (worst-of-converged %.2e)  %s\n",
               ok, maxErr, pass?"PASS":"FAIL");
        allPass &= pass;

        // unreachable target must fail cleanly (no NaN)
        Pose far; far.p=Eigen::Vector3d(100,100,100);
        Eigen::VectorXd q=Eigen::VectorXd::Zero(n);
        const auto res=c.ik(far,n-1,q,0.02,200,1e-7);
        const bool clean = q.allFinite() && !res.ok;
        printf("[dyn selftest]  IK unreachable handled cleanly (finite, ok=false): %s\n", clean?"PASS":"FAIL");
        allPass &= clean;
    }

    // --- loop closure: planar parallelogram 4-bar, cut joint, close --------
    {
        // Spanning tree: ground(-1) -> A(rev@origin) -> B(rev@ (a,0)) ; and
        // ground -> C(rev@ (d,0)). The bar from B must meet the bar from C.
        // We model a serial open chain ground->L1->L2->L3 and close L3 tip back
        // to a fixed ground pivot, emulating the closed parallelogram loop.
        const double a=1.0;               // crank/coupler length
        SerialChain c;
        DynJoint j1; j1.type=JType::Revolute; j1.parent=-1; j1.axis=Eigen::Vector3d::UnitZ();
        c.addBody(j1, DynBody{});                              // body0 @ origin
        DynJoint j2; j2.type=JType::Revolute; j2.parent=0; j2.axis=Eigen::Vector3d::UnitZ(); j2.ptree=Eigen::Vector3d(a,0,0);
        c.addBody(j2, DynBody{});                              // body1
        DynJoint j3; j3.type=JType::Revolute; j3.parent=1; j3.axis=Eigen::Vector3d::UnitZ(); j3.ptree=Eigen::Vector3d(a,0,0);
        c.addBody(j3, DynBody{});                              // body2 (tip joint)
        // close: tip of body2 (at local (a,0,0)) must coincide with the ground
        // pivot at world (a,0,0) — forming a rhombus loop of side a.
        LoopConstraint lc; lc.bodyA=2; lc.pA=Eigen::Vector3d(a,0,0);
        lc.bodyB=-1; lc.pB=Eigen::Vector3d(a,0,0);
        // drive body0 (independent), solve body1,body2 (dependent).
        double maxRes=0;
        std::uniform_real_distribution<double> A(0.3,1.2);
        for (int t=0;t<10;++t) {
            Eigen::VectorXd q=Eigen::VectorXd::Zero(3);
            q[0]=A(rng);                                       // independent crank angle
            q[1]=-q[0]*1.3; q[2]=q[0]*0.5;                     // seed
            const double res=c.closeLoops({lc}, {1,2}, q, 100, 1e-12);
            maxRes=std::max(maxRes,res);
        }
        const bool pass = maxRes < 1e-4;   // GATE A3 bound (expect ~1e-12)
        printf("[dyn selftest]  loop-closure (4-bar) residual: max=%.3e  %s\n", maxRes, pass?"PASS":"FAIL");
        allPass &= pass;
    }

    // --- Jacobian vs finite-difference of FK (independent geometric-Jacobian check) ---
    {
        const double h = 1e-6;
        double maxErr = 0;
        std::uniform_real_distribution<double> A(-2.5,2.5), Pl(-0.2,0.2);
        for (int trial=0; trial<25; ++trial) {
            const int n = 4 + (trial % 3);
            SerialChain c = randomChain(n, rng);
            Eigen::VectorXd q(n); for (int i=0;i<n;++i) q[i]=A(rng);
            const int body = n-1;
            const Eigen::Vector3d pl(Pl(rng),Pl(rng),Pl(rng));
            const Eigen::MatrixXd J = c.jacobian(q, body, pl);
            const Pose p0 = c.bodyPose(q, body);
            const Eigen::Vector3d pw0 = p0.p + p0.R*pl;
            for (int j=0;j<n;++j) {
                Eigen::VectorXd qp=q; qp[j]+=h;
                const Pose pp = c.bodyPose(qp, body);
                const Eigen::Vector3d pwp = pp.p + pp.R*pl;
                const Eigen::Vector3d dlin = (pwp - pw0)/h;                 // ∂(point)/∂q_j
                Eigen::AngleAxisd aa(pp.R * p0.R.transpose());
                const Eigen::Vector3d dang = aa.axis()*aa.angle()/h;        // ∂(orientation)/∂q_j
                maxErr = std::max(maxErr, (J.block<3,1>(0,j)-dlin).norm()); // linear rows
                maxErr = std::max(maxErr, (J.block<3,1>(3,j)-dang).norm()); // angular rows
            }
        }
        const bool pass = maxErr < 1e-5;   // FD truncation floor
        printf("[dyn selftest]  Jacobian vs FK finite-diff: maxErr=%.3e  %s\n", maxErr, pass?"PASS":"FAIL");
        allPass &= pass;
    }

    // --- property/fuzz: random BRANCHING trees mixing revolute/prismatic/FIXED joints ---
    {
        double maxFk=0, maxM=0, maxAsym=0, maxRt=0; int built=0;
        const Eigen::Vector3d grav(0,-9.81,0);
        std::uniform_real_distribution<double> A(-2.0,2.0);
        for (int trial=0; trial<60; ++trial) {
            const int nb = 3 + (trial % 6);
            SerialChain c = randomTree(nb, rng, /*prismatic*/true, /*fixed*/true);
            const int nq = c.nq();
            if (nq == 0) continue;
            ++built;
            Eigen::VectorXd q(nq),qd(nq),qdd(nq);
            for (int i=0;i<nq;++i){q[i]=A(rng);qd[i]=A(rng);qdd[i]=A(rng);}
            std::vector<Pose> wp; c.fk(q,wp);
            for (int b=0;b<c.nbody();++b) {                                 // FK two independent ways
                const int d=c.dofOf(b); const double qv=(d>=0)?q[d]:0.0;
                const DynJoint& j=c.joint(b);
                Eigen::Matrix3d Rj=Eigen::Matrix3d::Identity(); Eigen::Vector3d pj=Eigen::Vector3d::Zero();
                if (j.type==JType::Revolute) Rj=Eigen::AngleAxisd(qv,j.axis.normalized()).toRotationMatrix();
                else if (j.type==JType::Prismatic) pj=j.axis.normalized()*qv;
                const Eigen::Matrix3d Rr=j.Rtree*Rj; const Eigen::Vector3d pr=j.ptree+j.Rtree*pj;
                Pose exp;
                if (j.parent<0){exp.R=Rr;exp.p=pr;}
                else {exp.R=wp[j.parent].R*Rr; exp.p=wp[j.parent].p+wp[j.parent].R*pr;}
                maxFk=std::max(maxFk,(exp.p-wp[b].p).norm());
                maxFk=std::max(maxFk,(exp.R-wp[b].R).norm());
            }
            const Eigen::MatrixXd Mc=c.massMatrix(q);                       // CRBA vs RNEA columns
            Eigen::MatrixXd Mr(nq,nq);
            for (int i=0;i<nq;++i){Eigen::VectorXd e=Eigen::VectorXd::Zero(nq);e[i]=1.0;
                Mr.col(i)=c.rnea(q,Eigen::VectorXd::Zero(nq),e,Eigen::Vector3d::Zero());}
            maxM=std::max(maxM,(Mc-Mr).cwiseAbs().maxCoeff());
            maxAsym=std::max(maxAsym,(Mc-Mc.transpose()).cwiseAbs().maxCoeff());
            const Eigen::VectorXd tau=c.rnea(q,qd,qdd,grav);                 // RNEA<->ABA round-trip
            const Eigen::VectorXd qdd2=c.forwardDynamics(q,qd,tau,grav);
            maxRt=std::max(maxRt,(qdd-qdd2).cwiseAbs().maxCoeff());
        }
        const bool pass = maxFk<1e-12 && maxM<1e-8 && maxAsym<1e-8 && maxRt<1e-7;
        printf("[dyn selftest]  fuzz %d branching trees (rev/prism/fixed): FK=%.2e M=%.2e asym=%.2e RNEAvABA=%.2e  %s\n",
               built, maxFk, maxM, maxAsym, maxRt, pass?"PASS":"FAIL");
        allPass &= pass;
    }

    // --- mass-scaling robustness (1e6x and 1e-6x masses) ---
    {
        double maxRt=0;
        const Eigen::Vector3d grav(0,-9.81,0);
        std::uniform_real_distribution<double> A(-2.0,2.0);
        for (double sc : {1e6, 1e-6}) {
            SerialChain c; const int n=4;
            for (int i=0;i<n;++i){DynJoint j; j.type=JType::Revolute; j.parent=i-1;
                j.axis=Eigen::Vector3d(A(rng),A(rng),A(rng)).normalized(); j.ptree=Eigen::Vector3d(0.3,0,0);
                DynBody b; b.mass=2.0*sc; b.inertiaCom=Eigen::Matrix3d::Identity()*0.1*sc; c.addBody(j,b);}
            Eigen::VectorXd q(n),qd(n),qdd(n); for(int i=0;i<n;++i){q[i]=A(rng);qd[i]=A(rng);qdd[i]=A(rng);}
            const Eigen::VectorXd tau=c.rnea(q,qd,qdd,grav);
            const Eigen::VectorXd qdd2=c.forwardDynamics(q,qd,tau,grav);
            maxRt=std::max(maxRt,(qdd-qdd2).cwiseAbs().maxCoeff());
        }
        const bool pass = maxRt < 1e-6;
        printf("[dyn selftest]  mass-scaling (1e6x / 1e-6x) RNEA<->ABA: maxErr=%.3e  %s\n", maxRt, pass?"PASS":"FAIL");
        allPass &= pass;
    }

    // --- near-singular IK must stay finite (no NaN at a fully-extended config) ---
    {
        SerialChain c;       // planar 3R, links of length 1, reach ~3
        for (int i=0;i<3;++i){DynJoint j; j.type=JType::Revolute; j.parent=i-1; j.axis=Eigen::Vector3d::UnitZ();
            if(i>0)j.ptree=Eigen::Vector3d(1.0,0,0); DynBody b; b.mass=1.0; c.addBody(j,b);}
        Pose target; target.p=Eigen::Vector3d(3.5,0,0);   // just beyond reach -> singular boundary
        Eigen::VectorXd q=Eigen::VectorXd::Zero(3);
        const auto res=c.ik(target,2,q,0.05,200,1e-7);
        const bool pass = q.allFinite();                  // graceful: finite regardless of ok
        printf("[dyn selftest]  near-singular IK stays finite (posErr=%.3e, ok=%d): %s\n",
               res.posErr, int(res.ok), pass?"PASS":"FAIL");
        allPass &= pass;
    }

    printf("[dyn selftest] %s\n", allPass ? "ALL PASS" : "FAILURES PRESENT");
    fflush(stdout);
    return allPass;
}

} // namespace krs::dyn
