#include "ArticulationGate.hpp"

#if !defined(KR_WITH_PHYSX)
namespace krs::dyn {
bool runArticulationGate() { return true; }      // vacuous pass
bool runArticulationLiveGate() { return true; }  // vacuous pass
bool runDemoGateD() { return true; }             // vacuous pass
}
#else

#include "RobotDynamics.hpp"
#include <PxPhysicsAPI.h>
#include <Eigen/Geometry>
#include <vector>
#include <cstdio>
#include <cmath>
#include <random>
#include <algorithm>
#include "SimulationController.hpp"   // Phase G: drive the LIVE articulation path
#include "Scene.hpp"
#include "SysMem.hpp"                 // Phase A GATE D: D3 resource-growth probe

using namespace physx;

namespace krs::dyn {
namespace {

// --- spec shared by oracle + PhysX so the comparison is apples-to-apples -----
struct GateSpec {
    int parent = -1;
    bool revolute = true;
    Eigen::Vector3d axis  = Eigen::Vector3d::UnitZ();
    Eigen::Matrix3d Rtree = Eigen::Matrix3d::Identity();
    Eigen::Vector3d ptree = Eigen::Vector3d::Zero();
    double mass = 1.0;
    Eigen::Vector3d com = Eigen::Vector3d::Zero();
    Eigen::Vector3d inertiaDiag = Eigen::Vector3d(0.1, 0.1, 0.1);
};

SerialChain makeOracle(const std::vector<GateSpec>& s) {
    SerialChain c;
    for (const auto& g : s) {
        DynJoint j;
        j.type = g.revolute ? JType::Revolute : JType::Prismatic;
        j.parent = g.parent; j.axis = g.axis; j.Rtree = g.Rtree; j.ptree = g.ptree;
        DynBody b; b.mass = g.mass; b.com = g.com;
        b.inertiaCom = g.inertiaDiag.asDiagonal();
        c.addBody(j, b);
    }
    return c;
}

inline PxVec3 V(const Eigen::Vector3d& v) { return PxVec3(float(v.x()), float(v.y()), float(v.z())); }
inline PxQuat Q(const Eigen::Matrix3d& R) {
    Eigen::Quaterniond q(R); q.normalize();
    return PxQuat(float(q.x()), float(q.y()), float(q.z()), float(q.w()));
}
inline PxQuat QXto(const Eigen::Vector3d& axis) {
    Eigen::Quaterniond q = Eigen::Quaterniond::FromTwoVectors(Eigen::Vector3d::UnitX(), axis.normalized());
    q.normalize();
    return PxQuat(float(q.x()), float(q.y()), float(q.z()), float(q.w()));
}
inline Eigen::Vector3d E(const PxVec3& v) { return Eigen::Vector3d(v.x, v.y, v.z); }
inline Eigen::Matrix3d E(const PxQuat& q) {
    return Eigen::Quaterniond(q.w, q.x, q.y, q.z).toRotationMatrix();
}

struct Artic {
    PxArticulationReducedCoordinate* art = nullptr;
    std::vector<PxArticulationLink*> links;   // [0]=fixed root, [b+1]=oracle body b
};

void attachMass(PxArticulationLink* link, PxMaterial* mat, double mass,
                const Eigen::Vector3d& inertiaDiag, const Eigen::Vector3d& com) {
    PxShape* sh = PxRigidActorExt::createExclusiveShape(*link, PxSphereGeometry(0.02f), *mat);
    (void)sh;
    link->setCMassLocalPose(PxTransform(V(com)));
    link->setMass(PxReal(mass));
    link->setMassSpaceInertiaTensor(V(inertiaDiag));
}

Artic buildArtic(PxPhysics* phys, PxMaterial* mat, const std::vector<GateSpec>& s,
                 const std::vector<Pose>& wp0) {
    Artic a;
    a.art = phys->createArticulationReducedCoordinate();
    a.art->setArticulationFlag(PxArticulationFlag::eFIX_BASE, true);
    a.art->setSolverIterationCounts(64, 16);
    PxArticulationLink* root = a.art->createLink(nullptr, PxTransform(PxIDENTITY::PxIdentity));
    attachMass(root, mat, 1.0, Eigen::Vector3d(1,1,1), Eigen::Vector3d::Zero());
    a.links.push_back(root);
    for (int b = 0; b < int(s.size()); ++b) {
        PxArticulationLink* parent = (s[b].parent < 0) ? root : a.links[s[b].parent + 1];
        const PxTransform world(V(wp0[b].p), Q(wp0[b].R));
        PxArticulationLink* link = a.art->createLink(parent, world);
        attachMass(link, mat, s[b].mass, s[b].inertiaDiag, s[b].com);
        PxArticulationJointReducedCoordinate* j = link->getInboundJoint();
        const PxQuat qa = QXto(s[b].axis);
        if (s[b].revolute) j->setJointType(PxArticulationJointType::eREVOLUTE);
        else               j->setJointType(PxArticulationJointType::ePRISMATIC);
        j->setParentPose(PxTransform(V(s[b].ptree), Q(s[b].Rtree) * qa));
        j->setChildPose(PxTransform(PxVec3(0,0,0), qa));
        if (s[b].revolute) j->setMotion(PxArticulationAxis::eTWIST, PxArticulationMotion::eFREE);
        else               j->setMotion(PxArticulationAxis::eX, PxArticulationMotion::eFREE);
        a.links.push_back(link);   // index b+1 — parents resolve against this
    }
    return a;
}

} // namespace

bool runArticulationGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);   // unbuffered: output survives early return / external kill
    printf("[artic gate] PhysX PxArticulationReducedCoordinate vs Eigen oracle (GATE A plant track)\n");

    // Only one PxFoundation/PxPhysics is allowed per process and the app created
    // it at startup (SimulationController ctor -> ensureCore). Reuse that singleton
    // and build only our own throwaway scene. PxD6Joint needs the extensions, which
    // the app never inits, so init them once here.
    PxPhysics* phys = &PxGetPhysics();
    SimulationController::ensurePhysxExtensions();   // single guarded init (shared with the live path)
    PxDefaultCpuDispatcher* disp = PxDefaultCpuDispatcherCreate(2);
    PxSceneDesc sd(phys->getTolerancesScale());
    sd.gravity = PxVec3(0.0f, -9.81f, 0.0f);
    sd.cpuDispatcher = disp;
    sd.filterShader = PxDefaultSimulationFilterShader;
    PxScene* scene = phys->createScene(sd);
    PxMaterial* mat = phys->createMaterial(0.5f, 0.5f, 0.0f);
    bool allPass = true;
    std::mt19937 rng(777);

    // ---- A1: PhysX FK (applyCache ePOSITION -> getGlobalPose) vs oracle FK ----
    {
        // 4-link spatial chain in metres, derived from the FANUC's extracted axes.
        std::vector<GateSpec> s(4);
        s[0].parent=-1; s[0].axis=Eigen::Vector3d(0,1,0); s[0].ptree=Eigen::Vector3d(0,0,0);            // J1 base yaw (Y)
        s[1].parent=0;  s[1].axis=Eigen::Vector3d(1,0,0); s[1].ptree=Eigen::Vector3d(0,0.74,0.305);      // lower X pivot
        s[2].parent=1;  s[2].axis=Eigen::Vector3d(1,0,0); s[2].ptree=Eigen::Vector3d(0,1.075,0);         // arm top X pivot
        s[3].parent=2;  s[3].axis=Eigen::Vector3d(0,0,1); s[3].ptree=Eigen::Vector3d(0,0.25,0);          // wrist Z
        SerialChain oc = makeOracle(s);
        std::vector<Pose> wp0; oc.fk(Eigen::VectorXd::Zero(4), wp0);
        Artic a = buildArtic(phys, mat, s, wp0);
        scene->addArticulation(*a.art);
        PxArticulationCache* cache = a.art->createCache();
        const PxU32 nDof = a.art->getDofs();
        double maxPos=0, maxRot=0;
        std::uniform_real_distribution<double> U(-2.5, 2.5);
        for (int t=0;t<100;++t) {
            Eigen::VectorXd q(4); for (int i=0;i<4;++i) q[i]=U(rng);
            for (PxU32 d=0; d<nDof; ++d) cache->jointPosition[d] = PxReal(q[d]);
            a.art->applyCache(*cache, PxArticulationCacheFlag::ePOSITION);
            std::vector<Pose> wp; oc.fk(q, wp);
            for (int b=0;b<4;++b) {
                const PxTransform gp = a.links[b+1]->getGlobalPose();
                maxPos = std::max(maxPos, (E(gp.p) - wp[b].p).norm());
                Eigen::AngleAxisd aa(E(gp.q).transpose() * wp[b].R);
                maxRot = std::max(maxRot, std::abs(aa.angle()));
            }
        }
        const bool pass = maxPos < 1e-4 && maxRot < 1e-4;
        printf("[artic gate]  A1 FK PhysX vs oracle (100 cfg, 4-link): maxPos=%.3e m  maxRot=%.3e rad  %s\n",
               maxPos, maxRot, pass?"PASS":"FAIL");
        allPass &= pass;
        cache->release();
        scene->removeArticulation(*a.art);
        a.art->release();
    }

    // ---- A2: revolute on a DETECTED cylinder axis (FANUC arm pivot, X @ (0,1.815,0.305)) ----
    {
        std::vector<GateSpec> s(1);
        s[0].parent=-1; s[0].axis=Eigen::Vector3d(1,0,0); s[0].ptree=Eigen::Vector3d(0,1.815,0.305);
        SerialChain oc = makeOracle(s);
        std::vector<Pose> wp0; oc.fk(Eigen::VectorXd::Zero(1), wp0);
        Artic a = buildArtic(phys, mat, s, wp0);
        scene->addArticulation(*a.art);
        PxArticulationCache* cache = a.art->createCache();
        const Eigen::Vector3d pivot(0,1.815,0.305), axis(1,0,0), pl(0,0.3,0);
        double maxDev=0, minR=1e9, maxR=-1e9;
        const int N=91;
        for (int k=0;k<N;++k) {
            const double th = 1.5707963267948966 * double(k)/double(N-1);   // 0..90 deg
            cache->jointPosition[0] = PxReal(th);
            a.art->applyCache(*cache, PxArticulationCacheFlag::ePOSITION);
            const PxTransform gp = a.links[1]->getGlobalPose();
            const Eigen::Vector3d pw = E(gp.p) + E(gp.q) * pl;
            // analytic: rotate pl about X through pivot
            Eigen::Vector3d an = pivot + Eigen::AngleAxisd(th, axis).toRotationMatrix() * pl;
            maxDev = std::max(maxDev, (pw - an).norm());
            const double r = std::sqrt((pw.y()-pivot.y())*(pw.y()-pivot.y()) + (pw.z()-pivot.z())*(pw.z()-pivot.z()));
            minR=std::min(minR,r); maxR=std::max(maxR,r);
        }
        const bool pass = maxDev < 1e-4 && (maxR-minR) < 1e-4;   // <0.1 mm
        printf("[artic gate]  A2 revolute on detected axis: maxDev=%.3e m  radiusVar=%.3e m  %s\n",
               maxDev, (maxR-minR), pass?"PASS":"FAIL");
        allPass &= pass;
        cache->release();
        scene->removeArticulation(*a.art);
        a.art->release();
    }

    // ---- A5: commanded joint torque -> joint acceleration vs oracle ABA ----
    {
        std::vector<GateSpec> s(3);
        s[0].parent=-1; s[0].axis=Eigen::Vector3d(0,0,1); s[0].ptree=Eigen::Vector3d(0,0,0);
        s[0].mass=2.0; s[0].com=Eigen::Vector3d(0.2,0,0); s[0].inertiaDiag=Eigen::Vector3d(0.02,0.05,0.06);
        s[1].parent=0;  s[1].axis=Eigen::Vector3d(0,0,1); s[1].ptree=Eigen::Vector3d(0.4,0,0);
        s[1].mass=1.5; s[1].com=Eigen::Vector3d(0.15,0,0); s[1].inertiaDiag=Eigen::Vector3d(0.015,0.03,0.04);
        s[2].parent=1;  s[2].axis=Eigen::Vector3d(0,0,1); s[2].ptree=Eigen::Vector3d(0.3,0,0);
        s[2].mass=1.0; s[2].com=Eigen::Vector3d(0.1,0,0); s[2].inertiaDiag=Eigen::Vector3d(0.008,0.02,0.025);
        SerialChain oc = makeOracle(s);
        std::vector<Pose> wp0; oc.fk(Eigen::VectorXd::Zero(3), wp0);
        Artic a = buildArtic(phys, mat, s, wp0);
        scene->addArticulation(*a.art);
        PxArticulationCache* cache = a.art->createCache();
        const PxU32 nDof = a.art->getDofs();
        const Eigen::Vector3d grav(0,-9.81,0);
        std::uniform_real_distribution<double> U(-1.0,1.0);
        double maxRel=0;
        for (int t=0;t<20;++t) {
            Eigen::VectorXd q(3),qd(3),tau(3);
            for (int i=0;i<3;++i){q[i]=U(rng);qd[i]=0.5*U(rng);tau[i]=U(rng);}
            for (PxU32 d=0; d<nDof; ++d){ cache->jointPosition[d]=PxReal(q[d]); cache->jointVelocity[d]=PxReal(qd[d]); }
            a.art->applyCache(*cache, PxArticulationCacheFlag::ePOSITION);
            a.art->applyCache(*cache, PxArticulationCacheFlag::eVELOCITY);
            for (PxU32 d=0; d<nDof; ++d) cache->jointForce[d]=PxReal(tau[d]);
            a.art->commonInit();
            a.art->computeJointAcceleration(*cache);     // qdd incl. gravity + Coriolis
            Eigen::VectorXd qddPx(3); for (int i=0;i<3;++i) qddPx[i]=cache->jointAcceleration[i];
            const Eigen::VectorXd qddOr = oc.forwardDynamics(q,qd,tau,grav);
            const double rel = (qddPx - qddOr).norm() / std::max(1e-6, qddOr.norm());
            maxRel = std::max(maxRel, rel);
        }
        const bool pass = maxRel < 0.01;     // <1%
        printf("[artic gate]  A5 torque->accel PhysX vs oracle ABA: maxRel=%.3e  %s\n", maxRel, pass?"PASS":"FAIL");
        allPass &= pass;
        cache->release();
        scene->removeArticulation(*a.art);
        a.art->release();
    }

    // ---- A3: FANUC parallelogram closed loop via PxD6Joint, residual + FK across motion ----
    // Real FANUC geometry from the STEP extraction: the 1425 mm main arm carries two
    // PARALLEL pivots about the X axis at Z=0.305 m — the lower at (0,0.74,0.305) and the
    // top at (0,1.815,0.305), 1.075 m apart (the parallelogram bar). The three bars rotate
    // about X (the detected pivot axis), the loop lies in the Y-Z plane, and the cut joint
    // is pinned back to the REAL top pivot — so this is the FANUC's arm parallelogram, not
    // a generic unit rhombus. (Coupler width / 2nd ground pivot of the full 4-bar are not
    // cleanly extractable from the bolt-vs-bearing-ambiguous bores — documented in ROADMAP M.)
    {
        const double L = 1.075;                              // real arm-bar length (Y span)
        const Eigen::Vector3d base(0.0, 0.74, 0.305);        // real lower pivot
        const Eigen::Vector3d topPivot(0.0, 0.74 + L, 0.305);// real top pivot (0,1.815,0.305)
        const Eigen::Vector3d axisX(1,0,0), bar(0, L, 0);
        std::vector<GateSpec> s(3);
        s[0].parent=-1; s[0].axis=axisX; s[0].ptree=base;
        s[1].parent=0;  s[1].axis=axisX; s[1].ptree=bar;
        s[2].parent=1;  s[2].axis=axisX; s[2].ptree=bar;
        for (auto& g : s){ g.mass=160.0; g.com=Eigen::Vector3d(0,0.5*L,0); g.inertiaDiag=Eigen::Vector3d(60,2,60); }
        SerialChain oc = makeOracle(s);
        std::vector<Pose> wp0; oc.fk(Eigen::VectorXd::Zero(3), wp0);
        Artic a = buildArtic(phys, mat, s, wp0);
        // close: tip of body2 (local +bar) pinned to the REAL top pivot. Position pin:
        // lock 3 translations, free all rotations (locking the planar rotation the 4-bar
        // needs would over-constrain it and the solver would fight the natural config).
        const PxTransform tipLocal(V(bar));
        const PxTransform anchorWorld(V(topPivot));
        PxD6Joint* d6 = PxD6JointCreate(*phys, nullptr, anchorWorld, a.links[3], tipLocal);
        d6->setMotion(PxD6Axis::eX, PxD6Motion::eLOCKED);
        d6->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
        d6->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
        d6->setMotion(PxD6Axis::eSWING1, PxD6Motion::eFREE);
        d6->setMotion(PxD6Axis::eSWING2, PxD6Motion::eFREE);
        d6->setMotion(PxD6Axis::eTWIST,  PxD6Motion::eFREE);
        scene->addArticulation(*a.art);
        PxArticulationCache* cache = a.art->createCache();
        const PxU32 nDof = a.art->getDofs();
        scene->setGravity(PxVec3(0,0,0));        // isolate the constraint (no sag)
        double maxRes = 0, maxFkPos = 0, maxFkRot = 0;
        const double cranks[3] = {0.4, 0.7, 1.0};            // sweep the crank "across motion"
        for (double crank : cranks) {
            Eigen::VectorXd q = Eigen::VectorXd::Zero(3); q[0]=crank; q[1]=-crank*1.2; q[2]=crank*0.4;
            LoopConstraint lc; lc.bodyA=2; lc.pA=bar; lc.bodyB=-1; lc.pB=topPivot;
            oc.closeLoops({lc}, {1,2}, q, 100, 1e-12);
            for (PxU32 d=0; d<nDof; ++d) { cache->jointPosition[d]=PxReal(q[d]); cache->jointVelocity[d]=0.0f; }
            a.art->applyCache(*cache, PxArticulationCacheFlag::ePOSITION);
            a.art->applyCache(*cache, PxArticulationCacheFlag::eVELOCITY);
            for (int step=0; step<240; ++step) { scene->simulate(1.0f/240.0f); scene->fetchResults(true); }
            const PxTransform tipW = a.links[3]->getGlobalPose().transform(tipLocal);
            maxRes = std::max(maxRes, (E(tipW.p) - topPivot).norm());
            // FK cross-check on the SETTLED constrained state: oracle FK vs PhysX per link
            a.art->copyInternalStateToCache(*cache, PxArticulationCacheFlag::ePOSITION);
            Eigen::VectorXd qs(nDof); for (PxU32 d=0; d<nDof; ++d) qs[d]=cache->jointPosition[d];
            std::vector<Pose> wps; oc.fk(qs, wps);
            for (int b=0;b<3;++b) {
                const PxTransform gp = a.links[b+1]->getGlobalPose();
                maxFkPos = std::max(maxFkPos, (E(gp.p) - wps[b].p).norm());
                Eigen::AngleAxisd aa(E(gp.q).transpose() * wps[b].R);
                maxFkRot = std::max(maxFkRot, std::abs(aa.angle()));
            }
        }
        const bool pass = maxRes < 1e-4 && maxFkPos < 1e-4 && maxFkRot < 1e-4;
        printf("[artic gate]  A3 FANUC parallelogram loop: residual=%.3e m  settled-FK pos=%.3e m rot=%.3e rad  %s\n",
               maxRes, maxFkPos, maxFkRot, pass?"PASS":"FAIL");
        allPass &= pass;
        cache->release();
        d6->release();
        scene->removeArticulation(*a.art);
        a.art->release();
        scene->setGravity(PxVec3(0,-9.81f,0));
    }

    printf("[artic gate] %s\n", allPass ? "ALL PASS" : "FAILURES PRESENT");
    fflush(stdout);

    mat->release();
    scene->release();
    disp->release();
    // phys / foundation / extensions are owned by the app — do not release them.
    return allPass;
}

// ===========================================================================
// Phase G — GATE H: validate the LIVE SimulationController articulation path.
// Distinct from GATE A (which builds a throwaway articulation): here the FANUC
// tree is assembled by SimulationController::buildArticulation inside the live
// buildPhysicsWorld, and we validate THAT live tree against the Eigen oracle.
//   H1  FK of the live assembled tree vs oracle FK  (< 1e-4, >= 50 configs)
// (H2 torque->accel and H3 live parallelogram loop land in G.2/G.3.)
// ===========================================================================
bool runArticulationLiveGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[artic live] GATE H — live SimulationController articulation vs Eigen oracle\n");

    // FANUC 4-link chain: the §M.1a detected-axis spec, identical to GATE A1.
    std::vector<GateSpec> s(4);
    s[0].parent = -1; s[0].axis = Eigen::Vector3d(0,1,0); s[0].ptree = Eigen::Vector3d(0,0,0);
    s[1].parent =  0; s[1].axis = Eigen::Vector3d(1,0,0); s[1].ptree = Eigen::Vector3d(0,0.74,0.305);
    s[2].parent =  1; s[2].axis = Eigen::Vector3d(1,0,0); s[2].ptree = Eigen::Vector3d(0,1.075,0);
    s[3].parent =  2; s[3].axis = Eigen::Vector3d(0,0,1); s[3].ptree = Eigen::Vector3d(0,0.25,0);

    // Convert the (Eigen) GateSpec into the POD spec the live build consumes.
    krs::dyn::RobotArticSpec spec; spec.fixBase = true;
    for (const auto& g : s) {
        krs::dyn::ArticJointSpec j;
        j.parent = g.parent; j.revolute = g.revolute;
        j.axis = { float(g.axis.x()), float(g.axis.y()), float(g.axis.z()) };
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) j.Rtree[r*3+c] = float(g.Rtree(r,c));
        j.ptree = { float(g.ptree.x()), float(g.ptree.y()), float(g.ptree.z()) };
        j.mass = float(g.mass);
        j.com = { float(g.com.x()), float(g.com.y()), float(g.com.z()) };
        j.inertiaDiag = { float(g.inertiaDiag.x()), float(g.inertiaDiag.y()), float(g.inertiaDiag.z()) };
        spec.joints.push_back(j);
    }

    Scene scene;
    SimulationController sim(&scene);
    sim.setRobotArticulationSpec(spec);
    sim.play();                                  // buildPhysicsWorld -> buildArticulation (live path)

    const int dof = sim.articDofCount();
    if (dof != 4) { printf("[artic live] FAIL: live articulation dof=%d (expected 4)\n", dof); sim.stop(); return false; }

    SerialChain oc = makeOracle(s);
    std::mt19937 rng(20260614);
    std::uniform_real_distribution<double> U(-2.5, 2.5);
    double maxPos = 0, maxRot = 0;
    const int N = 60;
    for (int t = 0; t < N; ++t) {
        std::vector<float> q(4); Eigen::VectorXd qe(4);
        for (int i = 0; i < 4; ++i) { const double v = U(rng); q[i] = float(v); qe[i] = v; }
        sim.setArticJointPositions(q);
        const auto poses = sim.articLinkPoses();
        std::vector<Pose> wp; oc.fk(qe, wp);
        for (int b = 0; b < 4; ++b) {
            const Eigen::Vector3d    pp(poses[b][0], poses[b][1], poses[b][2]);
            const Eigen::Quaterniond pq(poses[b][6], poses[b][3], poses[b][4], poses[b][5]); // (w,x,y,z)
            maxPos = std::max(maxPos, (pp - wp[b].p).norm());
            const Eigen::AngleAxisd aa(pq.toRotationMatrix().transpose() * wp[b].R);
            maxRot = std::max(maxRot, std::abs(aa.angle()));
        }
    }
    sim.stop();

    const bool h1pass = maxPos < 1e-4 && maxRot < 1e-4;
    printf("[artic live] H1 live FK vs oracle (%d cfg, 4-link): maxPos=%.3e m  maxRot=%.3e rad  %s\n",
           N, maxPos, maxRot, h1pass ? "PASS" : "FAIL");

    // ---- H3: live FANUC parallelogram loop (PxD6 close), residual under motion ----
    // Real geometry from A3: 1.075 m arm bar, two parallel X-pivots at Z=0.305, cut
    // joint pinned to the real top pivot. Built + stepped by the LIVE SimulationController.
    double maxRes = 0; bool h3ran = false;
    {
        const double L = 1.075;
        const Eigen::Vector3d base(0.0, 0.74, 0.305);
        const Eigen::Vector3d topPivot(0.0, 0.74 + L, 0.305);
        const Eigen::Vector3d bar(0, L, 0), axisX(1, 0, 0);
        std::vector<GateSpec> p(3);
        p[0].parent = -1; p[0].axis = axisX; p[0].ptree = base;
        p[1].parent =  0; p[1].axis = axisX; p[1].ptree = bar;
        p[2].parent =  1; p[2].axis = axisX; p[2].ptree = bar;
        for (auto& g : p) { g.mass = 160.0; g.com = Eigen::Vector3d(0, 0.5*L, 0); g.inertiaDiag = Eigen::Vector3d(60, 2, 60); }
        krs::dyn::RobotArticSpec ps; ps.fixBase = true;
        for (const auto& g : p) {
            krs::dyn::ArticJointSpec j; j.parent = g.parent; j.revolute = true;
            j.axis = { float(g.axis.x()), float(g.axis.y()), float(g.axis.z()) };
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) j.Rtree[r*3+c] = float(g.Rtree(r,c));
            j.ptree = { float(g.ptree.x()), float(g.ptree.y()), float(g.ptree.z()) };
            j.mass = float(g.mass); j.com = { 0.f, float(0.5*L), 0.f }; j.inertiaDiag = { 60.f, 2.f, 60.f };
            ps.joints.push_back(j);
        }
        ps.loop.enabled = true; ps.loop.tipLink = 2;
        ps.loop.tipLocal    = { float(bar.x()), float(bar.y()), float(bar.z()) };
        ps.loop.anchorWorld = { float(topPivot.x()), float(topPivot.y()), float(topPivot.z()) };

        Scene scene2;
        SimulationController sim2(&scene2);
        sim2.setRobotArticulationSpec(ps);
        sim2.play();                       // buildArticulation builds chain + D6 loop
        sim2.setSceneGravity(0, 0, 0);     // isolate the constraint (like A3)
        if (sim2.articDofCount() == 3) {
            SerialChain oc2 = makeOracle(p);
            const double cranks[3] = { 0.4, 0.7, 1.0 };  // sweep the crank "across motion"
            for (double crank : cranks) {
                Eigen::VectorXd q = Eigen::VectorXd::Zero(3); q[0] = crank; q[1] = -crank*1.2; q[2] = crank*0.4;
                LoopConstraint lc; lc.bodyA = 2; lc.pA = bar; lc.bodyB = -1; lc.pB = topPivot;
                oc2.closeLoops({ lc }, { 1, 2 }, q, 100, 1e-12);
                std::vector<float> qf(3); for (int i = 0; i < 3; ++i) qf[i] = float(q[i]);
                sim2.setArticJointPositions(qf);
                for (int st = 0; st < 240; ++st) sim2.singleStep();   // settle in the LIVE sim
                const auto poses2 = sim2.articLinkPoses();            // [2] = 3rd bar
                const Eigen::Vector3d    tp(poses2[2][0], poses2[2][1], poses2[2][2]);
                const Eigen::Quaterniond tq(poses2[2][6], poses2[2][3], poses2[2][4], poses2[2][5]);
                const Eigen::Vector3d tipW = tp + tq.toRotationMatrix() * bar;
                maxRes = std::max(maxRes, (tipW - topPivot).norm());
            }
            h3ran = true;
        }
        sim2.stop();
    }
    const bool h3pass = h3ran && maxRes < 1e-4;
    printf("[artic live] H3 live parallelogram loop (D6, 3 cranks): residual=%.3e m  %s\n",
           maxRes, h3pass ? "PASS" : "FAIL");

    // ---- H2: a commanded CAN torque (codec round-trip) -> live joint accel vs oracle ABA --
    // Exercises the SAME jointForce routing the live applyCanCommands uses; the torque is
    // round-tripped through cancodec encode/decode so the gate validates the transmitted
    // (quantised) command, not the ideal one. Torques are large (>> the int16 quant) so
    // codec quantisation stays well under the 1% bound (cf. A5's note).
    double maxRel = 0; bool h2ran = false;
    {
        std::vector<GateSpec> a(3);
        a[0].parent=-1; a[0].axis=Eigen::Vector3d(0,0,1); a[0].ptree=Eigen::Vector3d(0,0,0);
        a[0].mass=2.0; a[0].com=Eigen::Vector3d(0.2,0,0); a[0].inertiaDiag=Eigen::Vector3d(0.02,0.05,0.06);
        a[1].parent=0;  a[1].axis=Eigen::Vector3d(0,0,1); a[1].ptree=Eigen::Vector3d(0.4,0,0);
        a[1].mass=1.5; a[1].com=Eigen::Vector3d(0.15,0,0); a[1].inertiaDiag=Eigen::Vector3d(0.015,0.03,0.04);
        a[2].parent=1;  a[2].axis=Eigen::Vector3d(0,0,1); a[2].ptree=Eigen::Vector3d(0.3,0,0);
        a[2].mass=1.0; a[2].com=Eigen::Vector3d(0.1,0,0); a[2].inertiaDiag=Eigen::Vector3d(0.008,0.02,0.025);
        krs::dyn::RobotArticSpec as; as.fixBase=true;
        for (const auto& g : a) {
            krs::dyn::ArticJointSpec j; j.parent=g.parent; j.revolute=true;
            j.axis={float(g.axis.x()),float(g.axis.y()),float(g.axis.z())};
            for (int r=0;r<3;++r) for (int c=0;c<3;++c) j.Rtree[r*3+c]=float(g.Rtree(r,c));
            j.ptree={float(g.ptree.x()),float(g.ptree.y()),float(g.ptree.z())};
            j.mass=float(g.mass);
            j.com={float(g.com.x()),float(g.com.y()),float(g.com.z())};
            j.inertiaDiag={float(g.inertiaDiag.x()),float(g.inertiaDiag.y()),float(g.inertiaDiag.z())};
            as.joints.push_back(j);
        }
        Scene scene3; SimulationController sim3(&scene3);
        sim3.setRobotArticulationSpec(as);
        sim3.play();
        if (sim3.articDofCount()==3) {
            SerialChain oc3 = makeOracle(a);
            const Eigen::Vector3d grav(0,-9.81,0);
            std::mt19937 rng2(424242);
            std::uniform_real_distribution<double> U(-1.0,1.0);
            for (int t=0;t<20;++t) {
                Eigen::VectorXd q(3),qd(3),tauCmd(3);
                std::vector<float> qf(3),qdf(3),tf(3);
                for (int i=0;i<3;++i) {
                    q[i]=U(rng2); qd[i]=0.5*U(rng2);
                    const float fe[3]={ float(20.0*U(rng2)), 0.f, 0.f };  // >> int16 quant
                    krs::hil::CanFrame fr2 = krs::hil::cancodec::encodeEffort(i, fe);  // CAN command
                    int ax; float fd[3]; krs::hil::cancodec::decodeEffort(fr2, ax, fd); // as received
                    tauCmd[i]=fd[0];
                    qf[i]=float(q[i]); qdf[i]=float(qd[i]); tf[i]=float(tauCmd[i]);
                }
                sim3.setArticJointPositions(qf);
                sim3.setArticJointVelocities(qdf);
                sim3.commandJointTorques(tf);                 // same routing as applyCanCommands
                const std::vector<float> qdd = sim3.articJointAccel();
                Eigen::VectorXd qddPx(3); for (int i=0;i<3;++i) qddPx[i]=qdd[i];
                const Eigen::VectorXd qddOr = oc3.forwardDynamics(q,qd,tauCmd,grav);
                maxRel = std::max(maxRel, (qddPx-qddOr).norm()/std::max(1e-6,qddOr.norm()));
            }
            h2ran=true;
        }
        sim3.stop();
    }
    const bool h2pass = h2ran && maxRel < 0.01;
    printf("[artic live] H2 CAN torque->accel vs oracle ABA (20 cfg, codec round-trip): maxRel=%.3e  %s\n",
           maxRel, h2pass ? "PASS" : "FAIL");

    const bool pass = h1pass && h2pass && h3pass;
    printf("[artic live] %s\n", pass ? "ALL PASS (H1,H2,H3)" : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

// ===========================================================================
// Phase A — GATE D: the default FANUC sandbox demo. The validated A3 parallelogram is
// driven through a repeated, smooth pick-and-place (crank A<->B) by a PD + Coriolis-
// compensation controller feeding cache.jointForce (the Phase-G command interface). PD
// drives only the crank (joint 0); the D6 loop + damping resolve the dependent bars, so
// the 1-DOF mechanism is not over-actuated. Zero gravity isolates the loop constraint
// (as GATE-H H3 does), making D1's "<1e-4 throughout the motion" a clean, non-vacuous
// measure of loop closure under ACTUAL commanded motion.
// ===========================================================================
bool runDemoGateD()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[demo gate] GATE D — FANUC parallelogram pick-and-place stability\n");

    const double L = 1.075;
    const Eigen::Vector3d base(0.0, 0.74, 0.305), topPivot(0.0, 0.74 + L, 0.305);
    const Eigen::Vector3d bar(0, L, 0), axisX(1, 0, 0);
    std::vector<GateSpec> p(3);
    p[0].parent = -1; p[0].axis = axisX; p[0].ptree = base;
    p[1].parent =  0; p[1].axis = axisX; p[1].ptree = bar;
    p[2].parent =  1; p[2].axis = axisX; p[2].ptree = bar;
    for (auto& g : p) { g.mass = 160.0; g.com = Eigen::Vector3d(0, 0.5*L, 0); g.inertiaDiag = Eigen::Vector3d(60, 2, 60); }
    krs::dyn::RobotArticSpec ps; ps.fixBase = true;
    for (const auto& g : p) {
        krs::dyn::ArticJointSpec j; j.parent = g.parent; j.revolute = true;
        j.axis = { float(g.axis.x()), float(g.axis.y()), float(g.axis.z()) };
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) j.Rtree[r*3+c] = float(g.Rtree(r,c));
        j.ptree = { float(g.ptree.x()), float(g.ptree.y()), float(g.ptree.z()) };
        j.mass = float(g.mass); j.com = { 0.f, float(0.5*L), 0.f }; j.inertiaDiag = { 60.f, 2.f, 60.f };
        ps.joints.push_back(j);
    }
    ps.loop.enabled = true; ps.loop.tipLink = 2;
    ps.loop.tipLocal    = { float(bar.x()), float(bar.y()), float(bar.z()) };
    ps.loop.anchorWorld = { float(topPivot.x()), float(topPivot.y()), float(topPivot.z()) };

    SerialChain oc = makeOracle(p);
    auto closedConfig = [&](double crank) {
        Eigen::VectorXd q = Eigen::VectorXd::Zero(3); q[0]=crank; q[1]=-crank*1.2; q[2]=crank*0.4;
        LoopConstraint lc; lc.bodyA=2; lc.pA=bar; lc.bodyB=-1; lc.pB=topPivot;
        oc.closeLoops({lc}, {1,2}, q, 100, 1e-12); return q;
    };
    const double crankA = 0.40, crankB = 1.00;
    const Eigen::Vector3d zeroG(0,0,0);
    const double Kp = 1.2e5, Kd = 1.2e4, KdDep = 300.0;   // crank PD + light dependent damping
    const Eigen::VectorXd qcmdA = closedConfig(crankA);   // loop-consistent seed
    auto smooth = [](double a, double b, double t){ double s=t*t*(3.0-2.0*t); return a+(b-a)*s; };

    // One demo run: cycles x (A->B + B->A). PD on the CRANK (joint 0, the independent DOF);
    // the dependent bars follow the D6 with light damping (the 1-DOF mechanism is not fought,
    // and we track the independent coord, not the oracle's non-unique dependent solution).
    // Returns max loop residual + max crank tracking error + NaN flag + trajectory checksum.
    // outputs: maxRes (loop residual, every step), maxLag (peak crank tracking error during
    // motion), maxSettled (crank error at the END of each dwell = reach accuracy), NaN, checksum.
    struct DemoResult { double maxRes=0, maxLag=0, maxSettled=0, crankLo=1e30, crankHi=-1e30, checksum=0; bool nan=false, ran=false; };
    auto runDemo = [&](int cycles, int mTrans, int mDwell) -> DemoResult {
        DemoResult R;
        Scene scene; SimulationController sim(&scene);
        sim.setRobotArticulationSpec(ps);
        sim.play();
        if (sim.articDofCount() != 3) { sim.stop(); return R; }
        sim.setSceneGravity(0,0,0);
        { std::vector<float> q0(3); for (int i=0;i<3;++i) q0[i]=float(qcmdA[i]); sim.setArticJointPositions(q0); }
        auto stepCtl = [&](double crankTgt) -> double {       // returns |crank - target| this step
            auto qm = sim.articJointPositions(); auto qdm = sim.articJointVelocities();
            if (qm.size()!=3 || qdm.size()!=3) { R.nan = true; return 0.0; }
            Eigen::VectorXd q(3), qd(3);
            for (int i=0;i<3;++i){ q[i]=qm[i]; qd[i]=qdm[i];
                if (!std::isfinite(qm[i]) || !std::isfinite(qdm[i])) R.nan = true; }
            // Pure PD on the crank + light damping on the dependent bars. No oracle bias
            // feedforward: the open-chain Coriolis is INVALID under the D6 loop (q1,q2 are
            // constrained, not free); at zero gravity / slow motion the PD + damping suffice.
            Eigen::VectorXd tau(3);
            tau[0] = Kp*(crankTgt - q[0]) - Kd*qd[0];
            tau[1] = -KdDep*qd[1];
            tau[2] = -KdDep*qd[2];
            std::vector<float> tf(3); for (int i=0;i<3;++i) tf[i]=float(tau[i]);
            sim.commandJointTorques(tf);
            sim.singleStep();
            R.crankLo = std::min(R.crankLo, q[0]); R.crankHi = std::max(R.crankHi, q[0]);  // prove motion
            auto poses = sim.articLinkPoses();
            if (poses.size()>=3) {
                Eigen::Vector3d tp(poses[2][0],poses[2][1],poses[2][2]);
                Eigen::Quaterniond tq(poses[2][6],poses[2][3],poses[2][4],poses[2][5]);
                R.maxRes = std::max(R.maxRes, (tp + tq.toRotationMatrix()*bar - topPivot).norm());
            }
            for (int i=0;i<3;++i) R.checksum += std::abs(q[i]) + std::abs(qd[i]);  // FULL-STATE determinism
            return std::abs(q[0] - crankTgt);
        };
        for (int s=0;s<240 && !R.nan;++s) stepCtl(crankA);    // settle (exclude seed transient)
        for (int cyc=0; cyc<cycles && !R.nan; ++cyc) {
            const double seq[2][2] = { {crankA,crankB}, {crankB,crankA} };
            for (int leg=0; leg<2; ++leg) {
                for (int m=0; m<mTrans; ++m) R.maxLag = std::max(R.maxLag, stepCtl(smooth(seq[leg][0], seq[leg][1], double(m)/(mTrans-1))));
                double e = 0; for (int m=0; m<mDwell; ++m) { e = stepCtl(seq[leg][1]); R.maxLag = std::max(R.maxLag, e); }
                R.maxSettled = std::max(R.maxSettled, e);      // reach accuracy at the end of the dwell
            }
        }
        sim.stop();
        R.ran = true;
        return R;
    };

    // --- D1: stability over 1000 cycles + PROOF OF MOTION (the crank really sweeps A<->B,
    // so a stationary/broken robot fails — the residual is measured under ACTUAL motion) ---
    DemoResult r1 = runDemo(1000, 20, 8);
    const bool swept1 = (r1.crankHi - r1.crankLo) >= 0.30;   // proves real motion (stationary => 0)
    bool d1 = r1.ran && !r1.nan && r1.maxRes < 1e-4 && swept1;
    printf("[demo gate]  D1 stability (1000 cyc, no NaN, loop res throughout, crank swept): maxRes=%.3e m crank=[%.3f,%.3f] %s\n",
           r1.maxRes, r1.crankLo, r1.crankHi, d1?"PASS":"FAIL");

    // --- D2: reach accuracy at each commanded config (long dwell) + reported dynamic lag ---
    DemoResult r2 = runDemo(8, 80, 160);
    const double TRACK_TOL = 0.02;
    bool d2 = r2.ran && !r2.nan && r2.maxSettled < TRACK_TOL && (r2.crankHi - r2.crankLo) >= 0.30;
    printf("[demo gate]  D2 tracking: reach-err=%.3e rad (bound %.2f), peak dynamic lag=%.3e rad  %s\n",
           r2.maxSettled, TRACK_TOL, r2.maxLag, d2?"PASS":"FAIL");

    // --- D3: no resource growth across 100 build/run/teardown cycles (the leak class) ---
    runDemo(1, 5, 2);   // warm allocators so the baseline is steady
    const size_t memBefore = krs::processWorkingSetBytes();
    for (int it=0; it<100; ++it) runDemo(1, 5, 2);
    const size_t memAfter = krs::processWorkingSetBytes();
    const double growthMB = (double(memAfter) - double(memBefore)) / (1024.0*1024.0);
    bool d3 = growthMB < 4.0;   // 100 builds; 4 MB catches a ~40 KB/build leak
    printf("[demo gate]  D3 no resource growth (100 build/run/teardown): dWorkingSet=%.2f MB bound=4 %s\n", growthMB, d3?"PASS":"FAIL");

    // --- D4: identical commanded motion reproduces an identical FULL-STATE trajectory ---
    DemoResult d4a = runDemo(50, 30, 12), d4b = runDemo(50, 30, 12);
    bool d4 = std::abs(d4a.checksum - d4b.checksum) < 1e-9 * std::max(1.0, std::abs(d4a.checksum));
    printf("[demo gate]  D4 determinism (two runs, full-state checksum): cs1=%.10g cs2=%.10g %s\n", d4a.checksum, d4b.checksum, d4?"PASS":"FAIL");

    const bool pass = d1 && d2 && d3 && d4;
    printf("[demo gate] %s\n", pass ? "ALL PASS (D1-D4)" : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::dyn
#endif // KR_WITH_PHYSX
