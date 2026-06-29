#pragma once
// ===========================================================================
// OMPL sprint, Phase 4 — robot entity + kinematic-chain DATA MODEL (krs::robot).
//
// A Robot OWNS its links + joints + a rigid base placement + an end-effector
// mount port. Key distinctions the directive requires:
//   * BODY MEMBERSHIP vs PLACEMENT: the base-to-floor transform is a rigid
//     PLACEMENT (Matrix4), NOT a chain DOF (rule 6) -- toChain() never adds it as
//     a joint, so nq() counts only member joints.
//   * a non-member joint is EXCLUDED from the chain the planner sees.
//   * ENGINEERING fields (limits / effort / velocity / control-mode / node id)
//     are USER-SUPPLIED and PROVENANCE-TAGGED (geometry-derived vs user-supplied),
//     never fabricated -- the geometry derives the FRAME, the user supplies limits.
//   * the MOUNT PORT is a one-sided TYPED frame; a tool attaches iff its type
//     matches, and swaps without redefining any joint.
//
// The model builds a krs::dyn::SerialChain for planning/execution (Phases 1-2)
// and serializes losslessly (CHAIN-EXPORT-ROUNDTRIP). Pure CPU/Eigen, no GL/OCCT.
// ===========================================================================
#include <Eigen/Dense>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <memory>
#include <utility>
#include <entt/entt.hpp>
#include "RobotDynamics.hpp"

class Scene;
namespace krs::rbuild { struct RobotGraph; }   // authoring schema (RobotBuilder.hpp)

namespace krs::robot {

enum class Provenance { GeometryDerived = 0, UserSupplied = 1 };

struct Joint {
    krs::dyn::JType type = krs::dyn::JType::Revolute;
    bool member = true;                                   // a chain DOF? (false = excluded)
    Eigen::Matrix3d Rtree = Eigen::Matrix3d::Identity();  // parent -> joint frame (q=0)
    Eigen::Vector3d ptree = Eigen::Vector3d::Zero();
    Eigen::Vector3d axis  = Eigen::Vector3d::UnitZ();
    Provenance frameProv = Provenance::GeometryDerived;   // FRAME comes from CAD features
    // engineering fields -- USER-SUPPLIED + provenance-tagged:
    double qLower = -3.14159265, qUpper = 3.14159265, vMax = 2.0, effortMax = 100.0;
    Provenance engProv = Provenance::UserSupplied;
    std::string controlMode = "position";
    int nodeId = 0;
};

struct MountPort {                                        // one-sided typed end-effector frame
    std::string type = "flange";
    int link = -1;                                        // link index carrying the port
    Eigen::Vector3d framePos = Eigen::Vector3d::Zero();   // port frame in the carrying link
    Eigen::Vector3d frameDir = Eigen::Vector3d::UnitZ();
};

struct Robot {
    std::string name = "robot";
    int nLinks = 0;
    std::vector<Joint> joints;                            // serial order (parent = previous added body)
    Eigen::Matrix4d basePlacement = Eigen::Matrix4d::Identity();   // rigid floor placement (NOT a DOF)
    MountPort mount;

    // Build the planning/execution chain from the OWNED (member) joints. The base
    // placement is a rigid world transform, NOT a movable DOF, so it is never
    // added -> nq() == number of member joints (includeNonMembers is the buggy
    // negative control that wrongly treats an excluded joint as a DOF).
    krs::dyn::SerialChain toChain(bool includeNonMembers = false) const {
        krs::dyn::SerialChain c;
        int prevBody = -1;
        for (const auto& j : joints) {
            if (!j.member && !includeNonMembers) continue;
            krs::dyn::DynJoint dj;
            dj.type = j.type; dj.parent = prevBody;
            dj.Rtree = j.Rtree; dj.ptree = j.ptree; dj.axis = j.axis;
            dj.qLower = j.qLower; dj.qUpper = j.qUpper;
            krs::dyn::DynBody b;
            b.mass = 1.0; b.com = Eigen::Vector3d(0.2, 0, 0); b.inertiaCom = 0.05 * Eigen::Matrix3d::Identity();
            prevBody = c.addBody(dj, b);
        }
        return c;
    }
};

// ===========================================================================
// THE LIVE ROBOT -- the first-class runtime owner of joint STATE. It wraps the
// static schema (Robot) + its cached kinematic chain and owns the canonical q
// (commanded) -- the SINGLE SOURCE OF TRUTH for joint angles. qActual is PhysX
// feedback (INFLUENCE only; it never overwrites q). The node graph WRITES q
// (applyCommand), PhysX FOLLOWS q as kinematic targets and reports qActual back,
// and the RobotGraph is the SCHEMA this is instantiated from. fkLinks() = FK(q).
// ===========================================================================
struct LiveRobot {
    Robot                 model;                  // schema (links/joints/base/limits)
    krs::dyn::SerialChain chain;                  // built from model member joints
    Eigen::VectorXd       q;                      // commanded (CLAMPED) -- SOURCE OF TRUTH
    Eigen::VectorXd       qActual;                // PhysX feedback (influence)
    // GHOST VALIDITY (Phase 7): qCommandRaw is the commanded value BEFORE limit clamping -- it equals
    // q on in-limit DOFs and exceeds a limit where the command was out of range. jointValid[i] is 0
    // exactly where clampDof() changed the value (the command violated the limit). The translucent
    // "ghost" robot is FK(qCommandRaw) (where the robot WOULD be unclamped) tinted by validity, drawn
    // against the real clamped robot FK(q) -- so an operator sees commanded-vs-reachable + which joint
    // is at its stop.
    Eigen::VectorXd       qCommandRaw;            // commanded PRE-CLAMP (ghost target)
    std::vector<char>     jointValid;             // per DOF: 1 = in limits, 0 = clamped (violated)
    std::vector<int>      memberJoint;            // DOF index -> model.joints index
    std::vector<std::vector<entt::entity>> linkEntities;  // chain body idx -> entities it drives
    std::vector<Eigen::Matrix4d> restLinkWorld;   // per chain-body rest world pose (q=0) for delta viz
    std::vector<std::vector<Eigen::Matrix4d>> linkEntityRestWorld;  // per chain-body, per entity rest world xf
    entt::entity          root = entt::null;       // the RobotRootComponent entity
    int                   robotId = -1;
    bool                  useRobotFkViz = false;   // ON => Robot FK drives viz (steps 3/6)
    bool                  ownsDrive = false;       // ON => this Robot's q drives the PhysX
                                                   // articulation; OFF => legacy drive path
                                                   // (lets a robot be first-class for
                                                   // hierarchy/selection without hijacking
                                                   // the working FANUC sweep -- step 6a).
    std::string           name = "robot";

    static constexpr double kLimitEps = 1e-9;     // a DOF is "valid" iff clamp moved it <= this

    int ndof() const { return int(q.size()); }

    // (Re)build the chain from the schema; resize q/qActual but PRESERVE values when the
    // DOF count is unchanged, so editing limits/frames never resets the live pose.
    void rebuild() {
        chain = model.toChain();
        memberJoint.clear();
        // DOF index -> model joint index: only NON-FIXED member joints carry a DOF, so
        // memberJoint.size() == chain.nq() even when Fixed joints rigidly weld sub-links.
        for (int i = 0; i < int(model.joints.size()); ++i)
            if (model.joints[i].member && model.joints[i].type != krs::dyn::JType::Fixed)
                memberJoint.push_back(i);
        const int n = chain.nq();
        if (int(q.size())       != n) q       = Eigen::VectorXd::Zero(n);
        if (int(qActual.size()) != n) qActual = Eigen::VectorXd::Zero(n);
        ensureGhostSized();
    }

    // Keep the ghost-validity arrays sized to q (default: ghost == q, all valid).
    void ensureGhostSized() {
        if (int(qCommandRaw.size()) != int(q.size())) qCommandRaw = q;
        if (int(jointValid.size())  != int(q.size())) jointValid.assign(size_t(q.size()), char(1));
    }

    // Clamp one DOF to its member joint's limits.
    double clampDof(int dof, double v) const {
        if (dof < 0 || dof >= int(memberJoint.size())) return v;
        const Joint& j = model.joints[memberJoint[dof]];
        return std::min(j.qUpper, std::max(j.qLower, v));
    }

    void setCommandedQ(const Eigen::VectorXd& qc) {
        ensureGhostSized();
        const int n = std::min(int(qc.size()), int(q.size()));
        for (int i = 0; i < n; ++i) {
            qCommandRaw[i] = qc[i];
            q[i]           = clampDof(i, qc[i]);
            jointValid[i]  = (std::abs(qCommandRaw[i] - q[i]) <= kLimitEps) ? char(1) : char(0);
        }
    }

    // Node-graph command bus -> q. Only DOFs flagged driven are written (undriven rest). The ghost
    // tracks the RAW command on driven DOFs (so an out-of-limit command shows as a divergent ghost);
    // undriven DOFs rest at q and are always valid.
    void applyCommand(const std::vector<float>& target, const std::vector<char>& driven) {
        ensureGhostSized();
        const int n = int(q.size());
        for (int i = 0; i < n; ++i) {
            const bool drv = (i < int(target.size())) && (i < int(driven.size())) && driven[i];
            if (drv) {
                qCommandRaw[i] = double(target[i]);
                q[i]           = clampDof(i, qCommandRaw[i]);
            } else {
                qCommandRaw[i] = q[i];               // rests at q -> always valid
            }
            jointValid[i] = (std::abs(qCommandRaw[i] - q[i]) <= kLimitEps) ? char(1) : char(0);
        }
    }

    // True iff any DOF's command was clamped (the ghost diverges from the real robot somewhere).
    bool anyJointInvalid() const {
        for (char c : jointValid) if (!c) return true;
        return false;
    }

    // Per chain-body world poses for q (chain base frame). World = basePlacement * pose.
    std::vector<krs::dyn::Pose> fkLinks() const {
        std::vector<krs::dyn::Pose> poses; chain.fk(q, poses); return poses;
    }

    // Per chain-body poses for the RAW (pre-clamp) command -- the GHOST pose (where the robot would be
    // if no limit clamped it). Equals fkLinks() when every command is in range. (Phase 7 ghost robot.)
    std::vector<krs::dyn::Pose> fkGhostLinks() const {
        std::vector<krs::dyn::Pose> poses;
        const Eigen::VectorXd qg = (qCommandRaw.size() == q.size()) ? qCommandRaw : q;
        chain.fk(qg, poses);
        return poses;
    }

    // World-frame (axisPos, axisDir) of each member joint at HOME (q=0), for the Robot
    // View joint-axis overlay. Joint origin = parent link frame * ptree; axis = parent
    // rotation * Rtree * axis. Parent of member joint k is chain body k-1 (base for k=0).
    std::vector<std::pair<Eigen::Vector3d, Eigen::Vector3d>> jointAxesWorld() const {
        return jointAxesWorld(Eigen::VectorXd::Zero(std::max(0, chain.nq())));
    }
    // World-frame (axisPos, axisDir) of each member joint at an ARBITRARY config q (so the
    // axis overlay can FOLLOW the robot when it moves, not stay stuck at home).
    std::vector<std::pair<Eigen::Vector3d, Eigen::Vector3d>> jointAxesWorld(const Eigen::VectorXd& q) const {
        std::vector<std::pair<Eigen::Vector3d, Eigen::Vector3d>> out;
        if (chain.nq() <= 0) return out;
        Eigen::VectorXd qq = (q.size() == chain.nq()) ? q : Eigen::VectorXd::Zero(chain.nq());
        std::vector<krs::dyn::Pose> poses; chain.fk(qq, poses);
        const Eigen::Matrix3d Rb = model.basePlacement.block<3, 3>(0, 0);
        const Eigen::Vector3d pb = model.basePlacement.block<3, 1>(0, 3);
        for (int k = 0; k < int(memberJoint.size()); ++k) {
            const Joint& j = model.joints[memberJoint[k]];
            Eigen::Matrix3d Rp = Rb; Eigen::Vector3d pp = pb;
            if (k > 0 && (k - 1) < int(poses.size())) { Rp = Rb * poses[k - 1].R; pp = Rb * poses[k - 1].p + pb; }
            out.emplace_back(pp + Rp * j.ptree, (Rp * (j.Rtree * j.axis)).normalized());
        }
        return out;
    }

    Eigen::VectorXd deviation() const {
        return (q.size() == qActual.size()) ? Eigen::VectorXd(q - qActual) : Eigen::VectorXd();
    }
};

// ctx-singleton registry of live robots (multi-robot). Mirrors the existing ctx
// patterns (RobotGraph, ArticulationCommandComponent). robotId is the stable key.
struct RobotRegistry {
    // shared_ptr (not unique_ptr) so the registry is copyable -- entt's registry
    // context (entt::any) stores values copy/assignably. shared_ptr also keeps each
    // LiveRobot at a STABLE address, so callers may hold LiveRobot* across frames.
    std::vector<std::shared_ptr<LiveRobot>> robots;
    LiveRobot* get(int id) {
        for (auto& r : robots) if (r && r->robotId == id) return r.get();
        return nullptr;
    }
    LiveRobot& create(int id) {
        if (LiveRobot* e = get(id)) return *e;
        robots.push_back(std::make_shared<LiveRobot>());
        robots.back()->robotId = id;
        return *robots.back();
    }
};

// --- mount port: attach a typed tool -------------------------------------
struct Tool {
    std::string type = "flange";
    Eigen::Vector3d localTip = Eigen::Vector3d::Zero();   // tool tip in the port frame
};

// The tool attaches iff its type matches the port type. On success, tipWorld is
// the tool tip placed at the mount-port frame (FK of the chain at config q, times
// the base placement, times the port-in-link offset, times the tool tip). The
// chain is NOT modified -- swapping tools never redefines a joint.
inline bool attachTool(const Robot& r, const krs::dyn::SerialChain& chain, const Eigen::VectorXd& q,
                       const Tool& tool, Eigen::Vector3d& tipWorld) {
    if (tool.type != r.mount.type) return false;          // typed: mismatch -> no attach
    std::vector<krs::dyn::Pose> poses;
    chain.fk(q, poses);
    // the carrying link's body pose (mount.link maps to the last member body for a serial arm).
    const int body = std::min(int(poses.size()) - 1, std::max(0, r.mount.link));
    const krs::dyn::Pose& P = poses[body];
    // world frame of the body, with the rigid base placement applied.
    const Eigen::Matrix3d Rb = r.basePlacement.block<3, 3>(0, 0);
    const Eigen::Vector3d pb = r.basePlacement.block<3, 1>(0, 3);
    const Eigen::Matrix3d Rworld = Rb * P.R;
    const Eigen::Vector3d pworld = Rb * P.p + pb;
    // tool tip = mount-port origin + tool tip (expressed in the port/link frame),
    // mapped to world by the carrying link's pose and the rigid base placement.
    tipWorld = Rworld * (r.mount.framePos + tool.localTip) + pworld;
    return true;
}

// --- lossless serialization ----------------------------------------------
inline std::string serialize(const Robot& r) {
    std::ostringstream o;
    o << std::setprecision(17);
    o << "ROBOT " << r.name << ' ' << r.nLinks << ' ' << r.joints.size() << '\n';
    o << "BASE";
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) o << ' ' << r.basePlacement(i, j);
    o << '\n';
    o << "MOUNT " << r.mount.type << ' ' << r.mount.link;
    for (int i = 0; i < 3; ++i) o << ' ' << r.mount.framePos[i];
    for (int i = 0; i < 3; ++i) o << ' ' << r.mount.frameDir[i];
    o << '\n';
    for (const auto& j : r.joints) {
        o << "JOINT " << int(j.type) << ' ' << int(j.member);
        for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) o << ' ' << j.Rtree(a, b);
        for (int a = 0; a < 3; ++a) o << ' ' << j.ptree[a];
        for (int a = 0; a < 3; ++a) o << ' ' << j.axis[a];
        o << ' ' << int(j.frameProv) << ' ' << j.qLower << ' ' << j.qUpper << ' ' << j.vMax
          << ' ' << j.effortMax << ' ' << int(j.engProv) << ' ' << j.controlMode << ' ' << j.nodeId << '\n';
    }
    o << "END\n";
    return o.str();
}

inline Robot deserialize(const std::string& s, bool& ok) {
    ok = false;
    Robot r;
    std::istringstream in(s);
    std::string tok;
    auto getd = [&](double& d) { return bool(in >> d); };
    if (!(in >> tok) || tok != "ROBOT") return r;
    size_t nJoints = 0;
    if (!(in >> r.name >> r.nLinks >> nJoints)) return r;
    if (!(in >> tok) || tok != "BASE") return r;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) if (!getd(r.basePlacement(i, j))) return r;
    if (!(in >> tok) || tok != "MOUNT") return r;
    if (!(in >> r.mount.type >> r.mount.link)) return r;
    for (int i = 0; i < 3; ++i) if (!getd(r.mount.framePos[i])) return r;
    for (int i = 0; i < 3; ++i) if (!getd(r.mount.frameDir[i])) return r;
    for (size_t k = 0; k < nJoints; ++k) {
        if (!(in >> tok) || tok != "JOINT") return r;     // wrong count / corruption -> fail
        Joint j; int ti = 0, mem = 0, fp = 0, ep = 0;
        if (!(in >> ti >> mem)) return r;
        j.type = static_cast<krs::dyn::JType>(ti); j.member = (mem != 0);
        for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) if (!getd(j.Rtree(a, b))) return r;
        for (int a = 0; a < 3; ++a) if (!getd(j.ptree[a])) return r;
        for (int a = 0; a < 3; ++a) if (!getd(j.axis[a])) return r;
        if (!(in >> fp >> j.qLower >> j.qUpper >> j.vMax >> j.effortMax >> ep >> j.controlMode >> j.nodeId))
            return r;
        j.frameProv = static_cast<Provenance>(fp); j.engProv = static_cast<Provenance>(ep);
        r.joints.push_back(j);
    }
    if (!(in >> tok) || tok != "END") return r;            // missing sentinel -> corruption
    ok = true;
    return r;
}

// Field-wise equality (doubles to 1e-12) for the round-trip assertion.
inline bool nearlyEqual(const Robot& a, const Robot& b, double tol = 1e-12) {
    if (a.name != b.name || a.nLinks != b.nLinks || a.joints.size() != b.joints.size()) return false;
    if ((a.basePlacement - b.basePlacement).cwiseAbs().maxCoeff() > tol) return false;
    if (a.mount.type != b.mount.type || a.mount.link != b.mount.link) return false;
    if ((a.mount.framePos - b.mount.framePos).cwiseAbs().maxCoeff() > tol) return false;
    if ((a.mount.frameDir - b.mount.frameDir).cwiseAbs().maxCoeff() > tol) return false;
    for (size_t k = 0; k < a.joints.size(); ++k) {
        const Joint& x = a.joints[k]; const Joint& y = b.joints[k];
        if (x.type != y.type || x.member != y.member || x.frameProv != y.frameProv
            || x.engProv != y.engProv || x.controlMode != y.controlMode || x.nodeId != y.nodeId) return false;
        if ((x.Rtree - y.Rtree).cwiseAbs().maxCoeff() > tol) return false;
        if ((x.ptree - y.ptree).cwiseAbs().maxCoeff() > tol) return false;
        if ((x.axis - y.axis).cwiseAbs().maxCoeff() > tol) return false;
        if (std::abs(x.qLower - y.qLower) > tol || std::abs(x.qUpper - y.qUpper) > tol
            || std::abs(x.vMax - y.vMax) > tol || std::abs(x.effortMax - y.effortMax) > tol) return false;
    }
    return true;
}

// GATE ROBOT-CHAIN (env KRS_ROBOTCHAIN_SELFTEST; in the bench): ROBOT-CHAIN /
// JOINT-FROM-FEATURE / MOUNT-PORT / CHAIN-EXPORT-ROUNDTRIP, each a measured number
// with a non-vacuous negative control. Pure CPU.
bool runRobotChainGate();

// GATE ROBOT-OWNER (env KRS_ROBOTOWNER_SELFTEST): the first-class Robot owner.
// Step 1 (pure CPU): LiveRobot::fkLinks()==SerialChain::fk(q); applyCommand clamps to
// limits + leaves undriven DOFs at rest; rebuild() sizes q to nq(). Step 2 (ECS):
// instantiateFromGraph creates a named root + parents bodies + real robotId + link map.
bool runRobotOwnerGate();

// GATE GHOST-VALIDITY (env KRS_GHOST_SELFTEST): the translucent ghost validity robot (CPU half).
// An out-of-limit command clamps q while qCommandRaw holds the raw target + jointValid flags the
// violated DOF; the ghost FK(qCommandRaw) diverges from the real FK(q) iff a joint is clamped.
// NEG-CTRL: a ghost FKing the clamped q shows zero divergence (cannot reveal the violation).
bool runGhostValidityGate();

// --- the runtime side of the foundation (defined in src/Physics/RobotInstance.cpp) ---
// Factory: build a LiveRobot + a named root entity from an authoring RobotGraph. Creates
// the root (RobotRootComponent + Tag + identity Transform), parents the graph's member
// body entities under it (ParentComponent), stamps the REAL robotId onto each
// (RobotSubcomponentComponent), and fills the chain link -> entity map. Returns the
// registered LiveRobot (owned by the ctx RobotRegistry), or nullptr on failure.
LiveRobot* instantiateFromGraph(Scene& scene, const krs::rbuild::RobotGraph& g, int robotId);

// Bridge the boot FANUC into a first-class Robot (step 6a): build the schema from
// krs::fanuc::canonicalSpec(), create the named root, parent all the FANUC body
// entities under it (real robotId), and map the chain links to the moving-link
// entity groups. Registered with ownsDrive=false + useRobotFkViz=false so it is
// first-class for outliner/selection WITHOUT hijacking the legacy PhysX sweep.
LiveRobot* instantiateFanucRobot(Scene& scene,
                                 const std::vector<std::vector<entt::entity>>& movingLinkEntities,
                                 const std::vector<entt::entity>& allBodies,
                                 int robotId, const std::string& name);

// Build an EDITABLE authoring RobotGraph that mirrors a LiveRobot exactly (one RBBody per
// link with its solid-entity group, one RBJoint per member joint with world frame + type +
// limits). toRobot() of the result reproduces the SAME FK, so a robot can round-trip
// schema <-> graph for structural editing (the keystone behind the editable FANUC).
// Defined in RobotInstance.cpp; returns by value (complete RobotGraph at the call site).
krs::rbuild::RobotGraph buildGraphFromLiveRobot(const LiveRobot& lr);

// Re-apply an edited authoring graph to its live robot (schema<->graph round-trip): snaps
// the robot home, then re-instantiates idempotently (root reuse + entity re-map), so an
// edit in the builder takes real effect on the live robot. Defined in RobotInstance.cpp.
LiveRobot* reapplyGraphToRobot(Scene& scene, const krs::rbuild::RobotGraph& g, int robotId);

// --- Outliner grouping (step 7): Robot -> bodies tree, multi-robot ----------------
// One robot group: the named root + the body entities owned by it (robotId match).
struct RobotGroup {
    entt::entity              root = entt::null;
    int                       robotId = -1;
    std::string               name;
    std::vector<entt::entity> bodies;
};
struct SceneGrouping {
    std::vector<RobotGroup>   robots;   // one per RobotRootComponent (any count)
    std::vector<entt::entity> loose;    // named entities not owned by any robot
};
// Partition the scene's named entities into robot groups + loose objects, so the
// outliner can render a Robot->bodies tree and selection can resolve to the owning
// robot. Headless-safe (pure registry walk), so it is unit-gated. (Step 7.)
SceneGrouping groupByRobot(entt::registry& reg);

// Mirror a robot's renderable body entities (by robotId) from the main scene INTO an
// isolated view scene (copies mesh + material + transform + material tags), so the
// Robot View can show the ACTUAL robot (e.g. the FANUC) rather than placeholder cubes.
// Fills outMap with {mainEntity, viewEntity} pairs (for live pose mirroring + cross-
// viewport selection). The view bodies are parent-less (their copied transform is
// already world, the main bodies' root being identity). Returns the count.
int mirrorLiveRobotIntoScene(Scene& viewScene, Scene& mainScene, int robotId,
                             std::vector<std::pair<entt::entity, entt::entity>>& outMap);

// Drain the node-graph command bus (registry-ctx ArticulationCommandComponent) INTO each
// registered LiveRobot::q (clamped to limits, driven DOFs only). LiveRobot::q is THE
// single source of truth -- PhysX is teleported FROM it separately. Returns #robots
// updated. Headless-safe (no PhysX), so it is unit-gated. (Step 4.)
int drainCommandBusIntoRobots(entt::registry& reg);

// Capture each link's rest world pose + each driven entity's rest world transform at
// the current q (call while q==0). Required before writeBackRobotViz. (Steps 3 / 6.)
void captureRobotRest(Scene& scene, LiveRobot& lr);

// Drive the link entity TransformComponents from Robot FK(q) (delta-from-rest), making
// the live Robot the viz source instead of PhysX. Rest poses are captured at q=0 on
// instantiation. No-op unless useRobotFkViz is set. (Steps 3 / 6.)
void writeBackRobotViz(Scene& scene, LiveRobot& lr);

} // namespace krs::robot
