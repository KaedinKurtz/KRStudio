#include "FanucArticulation.hpp"

#include <array>
#include <filesystem>
#include <system_error>

namespace krs::fanuc {

// --- THE assignment (single source of truth) -------------------------------
// Body index = import enumeration order: SOLIDs 0..16, then free SHELLs 17..26
// (CadImporter now spawns open shells too -- the FANUC J1 S-axis casting is an
// open shell, body 26). Derived from bore / shared-hinge connectivity + the
// shell bboxes (ROADMAP R.7), NOT the offset-fit.
//   link 0 base : solid {16} (the floor-bolted pedestal ONLY)
//   link 1 J1   : the S-axis casting shell {26} + carousel solid {13} + the brackets
//                 {11,14} bolted to the casting + its lower fittings shells {24,25} --
//                 the previously-MISSING J1->J2 link. (V-assign caught {11,14}: they
//                 share an OFF-AXIS bore with the rotating casting {26}, so they rotate
//                 with J1 -- they are NOT part of the fixed base.)
//   link 2 J2   : upper arm {12}
//   link 3 J3   : everything else (forearm + strut + bolts + wrist solids, and the
//                 wrist/tool-flange shells {17..23}; J4 frozen so they ride link 3)
int solidLink(int inspectIndex)
{
    const int k = inspectIndex;
    if (k == 16) return 0;                                                        // fixed pedestal only
    if (k == 13 || k == 11 || k == 14 || k == 24 || k == 25 || k == 26) return 1; // J1 rotating assembly
    if (k == 12) return 2;                                                        // upper arm
    return 3;                                                                     // forearm + strut + bolts + wrist
}

std::string assignmentFingerprint()
{
    std::string fp = "fanuc-v2:";                  // v2: free shells enumerated (27 bodies)
    for (int k = 0; k < kSolidCount; ++k) fp += char('0' + solidLink(k));  // links are 0..3 -> single digit
    return fp;
}

krs::dyn::RobotArticSpec canonicalSpec()
{
    using krs::dyn::ArticJointSpec;
    krs::dyn::RobotArticSpec ps; ps.fixBase = true;
    auto addJ = [&](int parent, float ax, float ay, float az, float px, float py, float pz,
                    float lo, float hi, bool frozen = false) {
        ArticJointSpec j; j.parent = parent; j.revolute = true;
        j.axis = { ax, ay, az };
        j.Rtree = { 1,0,0, 0,1,0, 0,0,1 };
        j.ptree = { px, py, pz };
        j.mass = 1.0f; j.com = { 0,0,0 }; j.inertiaDiag = { 0.1f, 0.1f, 0.1f };
        // Approximate FANUC-430-class per-axis travel (rad). User-editable; Phase 4 will
        // refine via collision sweep. These are real FINITE bounds (not +/-pi) so clampDof
        // demonstrably stops a command at the limit.
        j.qLower = lo; j.qUpper = hi; j.vMax = 2.0f; j.effortMax = 100.0f; j.frozen = frozen;
        ps.joints.push_back(j);
    };
    addJ(-1, 0,1,0,  0.f, 0.f,    0.f,   -2.967f, 2.967f);          // J1 base yaw  (Y @ origin)  +/-170deg
    addJ( 0, 1,0,0,  0.f, 0.74f,  0.305f,-1.571f, 2.618f);          // J2 shoulder  (X)           -90..+150deg
    addJ( 1, 1,0,0,  0.f, 1.075f, 0.f,   -2.792f, 2.792f);          // J3 elbow     (X)           +/-160deg
    addJ( 2, 0,0,1,  0.f, 0.25f,  0.f,   -3.316f, 3.316f, true);    // J4 wrist roll(Z) FROZEN     +/-190deg
    return ps;
}

std::string findStepAsset()
{
    const char* cands[] = {
        "assets/FANUC-430 Robot.STEP",
        "build/release/assets/FANUC-430 Robot.STEP",
        "../assets/FANUC-430 Robot.STEP",
        "KRStudio/assets/FANUC-430 Robot.STEP",
    };
    std::error_code ec;
    for (const char* c : cands) if (std::filesystem::exists(c, ec)) return c;
    return cands[0];
}

} // namespace krs::fanuc

// --- scene setup (needs both OpenCASCADE import + PhysX articulation) -------
#if defined(KR_WITH_OCCT) && defined(KR_WITH_PHYSX)

#include "CadImporter.hpp"
#include "SimulationController.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include <cstdlib>

namespace krs::fanuc {

Setup setupFanucScene(Scene& scene, SimulationController& sim, const std::string& stepPath)
{
    Setup out;
    out.solidEntity.assign(kSolidCount, entt::null);
    out.movingLinkEntities.assign(4, {});

    krs::cad::ImportResult ir = krs::cad::importStep(scene, stepPath, 0.001f);
    out.solids = ir.solids;
    if (!ir.ok) { out.message = "STEP import failed: " + ir.message; return out; }
    if (ir.solids != kSolidCount) { out.message = "expected 17 solids, got " + std::to_string(ir.solids); return out; }

    auto& reg = scene.getRegistry();
    for (auto e : reg.view<TagComponent>()) {
        const std::string& tag = reg.get<TagComponent>(e).tag;
        const std::string pfx = "STEP solid ";
        if (tag.rfind(pfx, 0) != 0) continue;
        const int k = std::atoi(tag.c_str() + pfx.size()) - 1;   // 1-indexed tag -> 0-based inspect idx
        if (k < 0 || k >= kSolidCount) continue;
        out.solidEntity[k] = e;
        const int L = solidLink(k);
        if (L >= 1 && L <= 4) out.movingLinkEntities[L - 1].push_back(e);   // base (L0) unmapped -> static
    }

    sim.setRobotArticulationSpec(canonicalSpec());
    sim.play();                                  // buildPhysicsWorld -> buildArticulation
    if (sim.articDofCount() != 4) { out.message = "articulation dof != 4"; return out; }
    sim.setSceneGravity(0, 0, 0);                // kinematic demo drive -> gravity irrelevant
    { std::vector<float> q0(4, 0.f); sim.setArticJointPositions(q0); }   // rest = STEP assembly pose
    sim.setArticulationVizMapping(out.movingLinkEntities);              // captures rest link poses

    out.fingerprint = assignmentFingerprint();
    out.ok = true;
    out.message = "ok";
    return out;
}

} // namespace krs::fanuc

#else

namespace krs::fanuc {
Setup setupFanucScene(Scene&, SimulationController&, const std::string&)
{ Setup out; out.message = "built without OpenCASCADE/PhysX"; return out; }
}

#endif
