#include "FanucArticulation.hpp"
#include "RobotBuilder.hpp"      // buildNamedSerialChain / articSpecFromGraph (GATE FANUC-6DOF)
#include "GateOutcome.hpp"       // krs::gate::skip -- tri-state bench outcome
#include "CadImporter.hpp"       // krs::cad::importStepAssembly (boot-identical METER-scale parse)
#include "Scene.hpp"             // headless import target for the gate

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

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

// ===========================================================================
// GATE FANUC-6DOF (E2.1) -- the true 6-DoF re-import, MEASURED on the real STEP.
// The default boot pipeline is parseAssembly -> buildNamedSerialChain ->
// instantiateFromGraph (FK viz). This gate pins that pipeline's OUTPUT QUALITY
// (dof==6, real interface axes, FK==CAD) and closes the remaining E2.1 gap:
// articSpecFromGraph converts the authored chain into a spec PhysX builds with
// 6 INDEPENDENT dofs -- retiring the 4-joint hand-cap as the only physics spec.
// ===========================================================================
namespace krs::fanuc {

namespace {
#ifdef KR_WITH_OCCT
Eigen::Matrix4d f6PoseMat(const krs::dyn::Pose& P)
{
    Eigen::Matrix4d M = Eigen::Matrix4d::Identity();
    M.block<3, 3>(0, 0) = P.R; M.block<3, 1>(0, 3) = P.p;
    return M;
}
#endif
} // namespace

bool runFanuc6Gate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[fanuc6] GATE FANUC-6DOF -- REAL STEP -> named chain: dof==6, quality axes, FK==placements, spec->PhysX dof==6\n");
#ifndef KR_WITH_OCCT
    printf("[fanuc6] SKIP: OpenCASCADE compiled out (KR_WITH_OCCT) -- cannot parse the STEP assembly.\n");
    krs::gate::skip();
    return true;
#else
    const std::string path = findStepAsset();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        printf("[fanuc6] SKIP: FANUC STEP asset not found on this machine (tried '%s').\n", path.c_str());
        krs::gate::skip();
        return true;
    }
    // BOOT-IDENTICAL import: the default boot feeds buildNamedSerialChain from
    // importStepAssembly (METERS, metersPerUnit=0.001) -- NOT raw parseAssembly (STEP mm).
    // The unit regime matters: the axis-inference tolerances (coax 5e-3 / relaxed 1.5e-2)
    // are meter-scale, so a raw-mm parse runs them 1000x too tight and misgrades the tiers.
    Scene importScene;
    const std::vector<krs::rbuild::ParsedPart> parts = krs::cad::importStepAssembly(importScene, path);
    if (parts.empty()) {
        printf("[fanuc6] FAIL: importStepAssembly produced 0 parts from a PRESENT file (%s)\n", path.c_str());
        return false;
    }
    krs::rbuild::RobotGraph g = krs::rbuild::buildNamedSerialChain(parts);

    // ---- (1) STRUCTURE: base + j1..j6 collapsed bodies, dof == 6, nothing ambiguous ----
    int nAmb = 0; for (const auto& j : g.joints) if (j.ambiguous) ++nAmb;
    const bool structOk = int(g.bodies.size()) == 7 && g.dof() == 6 && nAmb == 0;
    printf("[fanuc6]   (1) parts=%d -> bodies=%d (want 7: base+j1..j6), dof=%d (want 6), ambiguous=%d (want 0)  %s\n",
           int(parts.size()), int(g.bodies.size()), g.dof(), nAmb, structOk ? "PASS" : "FAIL");

    // ---- (2) AXIS QUALITY: J1 vertical (Y-up world; the arm STANDS on its turntable);
    //          every other axis from a REAL coaxial interface (residual <= the relaxed
    //          shared-hinge tier), never the residual-1.0 last-resort single-bore guess. ----
    bool axesOk = !g.joints.empty();
    for (int k = 0; k < int(g.joints.size()); ++k) {
        const auto& j = g.joints[k];
        bool ok;
        if (k == 0) {
            const float vert = std::abs(glm::dot(glm::normalize(j.axisDir), glm::vec3(0, 1, 0)));
            ok = vert > 0.999f && !j.ambiguous;
            printf("[fanuc6]   (2) J1 axis (%+.3f,%+.3f,%+.3f) |dot(world-up)|=%.4f (want >0.999)  %s\n",
                   j.axisDir.x, j.axisDir.y, j.axisDir.z, vert, ok ? "ok" : "BAD");
        } else {
            ok = j.residual <= 1.5e-2 && !j.ambiguous;
            printf("[fanuc6]   (2) J%d axis (%+.3f,%+.3f,%+.3f) pos (%+.3f,%+.3f,%+.3f) residual=%.2e (want <=1.5e-2, not a guess)  %s\n",
                   k + 1, j.axisDir.x, j.axisDir.y, j.axisDir.z,
                   j.axisPos.x, j.axisPos.y, j.axisPos.z, j.residual, ok ? "ok" : "BAD");
        }
        axesOk = axesOk && ok;
    }

    // ---- (3) FK at q=0 reproduces the parsed CAD placements (chainFK == CAD) ----
    double fkErr = 1e9; bool fkOk = false;
    if (g.dof() > 0 && int(g.bodies.size()) == g.dof() + 1) {
        krs::robot::Robot R = g.toRobot();
        krs::dyn::SerialChain chain = R.toChain();
        Eigen::VectorXd q = Eigen::VectorXd::Zero(chain.nq());
        std::vector<krs::dyn::Pose> poses; chain.fk(q, poses);
        if (int(poses.size()) == g.dof()) {
            fkErr = 0.0;
            for (int k = 0; k < int(poses.size()); ++k) {
                const Eigen::Matrix4d w = R.basePlacement * f6PoseMat(poses[k]);
                fkErr = std::max(fkErr, (w - g.bodies[size_t(k) + 1].placement).cwiseAbs().maxCoeff());
            }
            fkOk = fkErr < 1e-6;
        }
    }
    printf("[fanuc6]   (3) FK(q=0) vs parsed CAD placements: max err=%.2e m (want <1e-6)  %s\n",
           fkErr, fkOk ? "PASS" : "FAIL");

    // ---- (4) CONVERTER: articSpecFromGraph reproduces the chain EXACTLY ----
    std::vector<std::string> rep;
    const krs::dyn::RobotArticSpec spec = krs::rbuild::articSpecFromGraph(g, &rep);
    bool convOk = int(spec.joints.size()) == 6 && rep.empty();
    {
        glm::vec3 acc(0.0f);
        double axErr = 0.0, posErr = 0.0;
        for (int k = 0; k < int(spec.joints.size()) && k < int(g.joints.size()); ++k) {
            const auto& s = spec.joints[k]; const auto& j = g.joints[k];
            convOk = convOk && s.parent == k - 1 && s.revolute && !s.frozen
                            && s.qLower < s.qUpper && std::isfinite(s.qLower) && std::isfinite(s.qUpper);
            const glm::vec3 a(s.axis[0], s.axis[1], s.axis[2]);
            axErr = std::max(axErr, double(glm::length(a - glm::normalize(j.axisDir))));
            acc += glm::vec3(s.ptree[0], s.ptree[1], s.ptree[2]);   // wp0 composition (identity Rtree chain)
            posErr = std::max(posErr, double(glm::length(acc - j.axisPos)));
        }
        convOk = convOk && axErr < 1e-6 && posErr < 1e-6;
        printf("[fanuc6]   (4) articSpecFromGraph: joints=%d (want 6 serial driven, finite limits), axis err=%.2e, wp0-composition err=%.2e (<1e-6), report=%d (want 0)  %s\n",
               int(spec.joints.size()), axErr, posErr, int(rep.size()), convOk ? "PASS" : "FAIL");
        for (const auto& r : rep) printf("[fanuc6]       report: %s\n", r.c_str());
    }

    // ---- (5) PHYSX: the converted spec builds an articulation with 6 INDEPENDENT dofs.
    //          Set ONLY J4 wrist roll (index 3) -- the exact dof the legacy 4-joint cap
    //          FROZE -- and verify it moves alone, then hold through 30 zero-g steps. ----
    bool physOk = false;
#if defined(KR_WITH_PHYSX)
    {
        Scene scene; SimulationController sim(&scene);
        sim.setRobotArticulationSpec(spec);
        sim.play(); sim.setSceneGravity(0, 0, 0);
        const int dof = sim.articDofCount();
        std::vector<float> tgt(6, 0.0f);
        if (dof == 6) { tgt[3] = 0.4f; sim.setArticJointPositions(tgt); }
        const std::vector<float> q1 = sim.articJointPositions();
        double setErr = 1e9, bleed = 1e9;
        if (int(q1.size()) == 6) {
            setErr = std::abs(double(q1[3]) - 0.4);
            bleed = 0.0; for (int i = 0; i < 6; ++i) if (i != 3) bleed = std::max(bleed, double(std::abs(q1[i])));
        }
        bool finiteOk = dof == 6;
        for (int s = 0; s < 30 && finiteOk; ++s) sim.singleStep();
        const std::vector<float> q2 = sim.articJointPositions();
        double drift = 1e9;
        if (int(q2.size()) == 6) {
            drift = 0.0;
            for (int i = 0; i < 6; ++i) {
                if (!std::isfinite(q2[i])) finiteOk = false;
                drift = std::max(drift, double(std::abs(q2[i] - tgt[i])));
            }
        }
        sim.stop();
        physOk = dof == 6 && setErr < 1e-3 && bleed < 1e-3 && finiteOk && drift < 5e-2;
        printf("[fanuc6]   (5) PhysX articulation: dof=%d (want 6; legacy cap was 4), set J4=0.4 -> read %.4f (err %.2e), other-dof bleed %.2e (<1e-3), 30-step zero-g drift %.2e (<5e-2)  %s\n",
               dof, (int(q1.size()) == 6 ? double(q1[3]) : -1.0), setErr, bleed, drift, physOk ? "PASS" : "FAIL");
    }
#else
    physOk = true;
    printf("[fanuc6]   (5) PhysX compiled out -- articulation build not exercised (spec-level checks only)\n");
#endif

    // ---- (6) NEG-CTRLs ----
    // (a) the LEGACY spec still hand-caps at 4 joints with J4 frozen -- the exact cap
    //     this pipeline retires. If someone silently un-caps canonicalSpec instead of
    //     using the graph pipeline, this control catches the drift.
    const krs::dyn::RobotArticSpec legacy = canonicalSpec();
    const bool legacyCapped = int(legacy.joints.size()) == 4 && legacy.joints[3].frozen;
    printf("[fanuc6]   (6) NEG-CTRL legacy canonicalSpec: joints=%d, frozen[3]=%s (must stay 4-capped/frozen)  %s\n",
           int(legacy.joints.size()),
           (legacy.joints.size() > 3 && legacy.joints[3].frozen) ? "yes" : "no",
           legacyCapped ? "REJECTS(non-vacuous)" : "VACUOUS!");
    // (b) an AMBIGUOUS mid-chain joint TRUNCATES the converted spec honestly (fewer
    //     joints + a named report), never a fabricated axis.
    bool honestDrop = false;
    if (int(g.joints.size()) >= 4) {
        krs::rbuild::RobotGraph gBad = g;
        gBad.joints[3].ambiguous = true;
        std::vector<std::string> rep2;
        const krs::dyn::RobotArticSpec spec2 = krs::rbuild::articSpecFromGraph(gBad, &rep2);
        honestDrop = int(spec2.joints.size()) == 3 && !rep2.empty();
        printf("[fanuc6]   (6) NEG-CTRL ambiguous mid-chain joint: spec joints=%d (want 3), report=%d (want >0)  %s\n",
               int(spec2.joints.size()), int(rep2.size()), honestDrop ? "REJECTS(non-vacuous)" : "VACUOUS!");
    } else {
        printf("[fanuc6]   (6) NEG-CTRL ambiguous joint: chain too short to test  VACUOUS!\n");
    }

    const bool pass = structOk && axesOk && fkOk && convOk && physOk && legacyCapped && honestDrop;
    printf("[fanuc6] %s\n", pass
        ? "ALL PASS (real STEP -> true 6-DoF chain, quality axes, FK==CAD; graph->spec->PhysX articulation has 6 independent dofs)"
        : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
#endif
}

} // namespace krs::fanuc
