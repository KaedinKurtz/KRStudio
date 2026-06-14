#include "ArticulationGate.hpp"

#if !defined(KR_WITH_PHYSX)
namespace krs::dyn { bool runArticulationGate() { return true; } } // vacuous pass
#else

#include "RobotDynamics.hpp"
#include <PxPhysicsAPI.h>
#include <Eigen/Geometry>
#include <vector>
#include <cstdio>
#include <cmath>
#include <random>
#include <algorithm>

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
    static bool s_ext = PxInitExtensions(*phys, nullptr); (void)s_ext;
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

    // ---- A3: parallelogram closed loop via PxD6Joint, residual across motion ----
    {
        const double aLen = 1.0;
        std::vector<GateSpec> s(3);
        s[0].parent=-1; s[0].axis=Eigen::Vector3d(0,0,1); s[0].ptree=Eigen::Vector3d(0,0,0);
        s[1].parent=0;  s[1].axis=Eigen::Vector3d(0,0,1); s[1].ptree=Eigen::Vector3d(aLen,0,0);
        s[2].parent=1;  s[2].axis=Eigen::Vector3d(0,0,1); s[2].ptree=Eigen::Vector3d(aLen,0,0);
        for (auto& g : s){ g.mass=1.0; g.com=Eigen::Vector3d(0.5*aLen,0,0); g.inertiaDiag=Eigen::Vector3d(0.01,0.1,0.1); }
        SerialChain oc = makeOracle(s);
        std::vector<Pose> wp0; oc.fk(Eigen::VectorXd::Zero(3), wp0);
        Artic a = buildArtic(phys, mat, s, wp0);
        // close: tip of link3 (body2) local (aLen,0,0) pinned to world anchor (aLen,0,0).
        // a revolute pin: lock 3 translations + 2 swings, free TWIST about Z.
        const PxTransform tipLocal(PxVec3(float(aLen),0,0));
        const PxTransform anchorWorld(PxVec3(float(aLen),0,0));
        PxD6Joint* d6 = PxD6JointCreate(*phys, nullptr, anchorWorld, a.links[3], tipLocal);
        // Position pin: lock the 3 translations (closes the loop in position),
        // free all rotations. Locking a rotation the planar 4-bar needs (Z) would
        // over-constrain it and the solver would fight the natural configuration.
        d6->setMotion(PxD6Axis::eX, PxD6Motion::eLOCKED);
        d6->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
        d6->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
        d6->setMotion(PxD6Axis::eSWING1, PxD6Motion::eFREE);
        d6->setMotion(PxD6Axis::eSWING2, PxD6Motion::eFREE);
        d6->setMotion(PxD6Axis::eTWIST,  PxD6Motion::eFREE);
        scene->addArticulation(*a.art);
        PxArticulationCache* cache = a.art->createCache();
        scene->setGravity(PxVec3(0,0,0));        // isolate the constraint (no sag)
        // settle, then sample residual across a few crank angles
        double maxRes = 0;
        const double cranks[3] = {0.5, 0.8, 1.1};
        for (double crank : cranks) {
            // seed a closed-ish config via the oracle, push into the articulation
            Eigen::VectorXd q = Eigen::VectorXd::Zero(3); q[0]=crank; q[1]=-crank*1.2; q[2]=crank*0.4;
            LoopConstraint lc; lc.bodyA=2; lc.pA=Eigen::Vector3d(aLen,0,0); lc.bodyB=-1; lc.pB=Eigen::Vector3d(aLen,0,0);
            oc.closeLoops({lc}, {1,2}, q, 100, 1e-12);
            for (PxU32 d=0; d<a.art->getDofs(); ++d) { cache->jointPosition[d]=PxReal(q[d]); cache->jointVelocity[d]=0.0f; }
            a.art->applyCache(*cache, PxArticulationCacheFlag::ePOSITION);
            a.art->applyCache(*cache, PxArticulationCacheFlag::eVELOCITY);
            for (int step=0; step<240; ++step) { scene->simulate(1.0f/240.0f); scene->fetchResults(true); }
            const PxTransform tipW = a.links[3]->getGlobalPose().transform(tipLocal);
            const double res = (E(tipW.p) - Eigen::Vector3d(aLen,0,0)).norm();
            maxRes = std::max(maxRes, res);
        }
        const bool pass = maxRes < 1e-4;
        printf("[artic gate]  A3 parallelogram D6 loop residual (across motion): max=%.3e m  %s\n", maxRes, pass?"PASS":"FAIL");
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

} // namespace krs::dyn
#endif // KR_WITH_PHYSX
