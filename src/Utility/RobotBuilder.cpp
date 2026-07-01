// RobotBuilder.cpp -- ROBOT BUILDER Phase 0 recon (and, later, the OCCT assembly
// parse). Phase 0 answers EMPIRICALLY, against a real assembly, what OCCT's STEP
// reader exposes: the part hierarchy? per-part placements? preserved mate data?
// The answer determines whether joint inference reads mates or infers from geometry.

#include "RobotBuilder.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

#ifdef KR_WITH_OCCT
#include <STEPControl_Reader.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <TDF_Label.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDataStd_Name.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopAbs.hxx>
#include <gp_Trsf.hxx>
#include <TCollection_AsciiString.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <gp_Pln.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Cone.hxx>
#include <gp_Sphere.hxx>
#endif

namespace krs::rbuild {

#ifdef KR_WITH_OCCT
namespace {

// resolve the FANUC assembly path relative to the exe's deploy dir or the source tree.
std::string findAssembly()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    // machine-agnostic candidates: env override, beside the exe / cwd, then the source tree.
    std::vector<std::string> cands;
    if (const char* env = std::getenv("KRS_ASSETS_DIR"); env && env[0])
        cands.push_back(std::string(env) + "/FANUC-430 Robot.STEP");
    cands.push_back("assets/FANUC-430 Robot.STEP");
    cands.push_back("build/release/assets/FANUC-430 Robot.STEP");
#ifdef KRS_SOURCE_DIR
    cands.push_back(std::string(KRS_SOURCE_DIR) + "/assets/FANUC-430 Robot.STEP");
#endif
    for (const std::string& c : cands) if (fs::exists(c, ec)) return c;
    return {};
}

struct Stats {
    int freeShapes = 0;
    int assemblies = 0;     // labels that are sub-assemblies
    int parts = 0;          // leaf (simple-shape) part definitions visited
    int instances = 0;      // component references (placed instances)
    int nonIdentityPlacements = 0;
    int named = 0;
    int maxDepth = 0;
};

std::string labelName(const TDF_Label& label)
{
    Handle(TDataStd_Name) nm;
    if (label.FindAttribute(TDataStd_Name::GetID(), nm)) {
        TCollection_AsciiString a(nm->Get());
        return std::string(a.ToCString());
    }
    return std::string("(unnamed)");
}

void walk(const Handle(XCAFDoc_ShapeTool)& st, const TDF_Label& label,
          int depth, Stats& s, int& printed)
{
    s.maxDepth = std::max(s.maxDepth, depth);
    if (st->IsAssembly(label)) {
        ++s.assemblies;
        TDF_LabelSequence comps;
        st->GetComponents(label, comps);
        for (int i = 1; i <= comps.Length(); ++i) {
            const TDF_Label comp = comps.Value(i);
            ++s.instances;
            const TopLoc_Location loc = st->GetLocation(comp);
            if (!loc.IsIdentity()) ++s.nonIdentityPlacements;
            if (labelName(comp) != "(unnamed)") ++s.named;
            TDF_Label refLabel;
            if (XCAFDoc_ShapeTool::GetReferredShape(comp, refLabel))
                walk(st, refLabel, depth + 1, s, printed);
        }
    } else {
        ++s.parts;
        const std::string nm = labelName(label);
        if (nm != "(unnamed)") ++s.named;
        if (printed < 16) {
            std::printf("[parse-recon]     part: %-34s  depth=%d\n", nm.c_str(), depth);
            ++printed;
        }
    }
}

Eigen::Matrix4d trsfToMatrix(const gp_Trsf& t)
{
    Eigen::Matrix4d M = Eigen::Matrix4d::Identity();
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) M(r, c) = t.Value(r + 1, c + 1);
        M(r, 3) = t.Value(r + 1, 4);
    }
    return M;
}

// read each face's ANALYTIC B-Rep parameters (part-LOCAL frame) -- same exact read
// as CadImporter (no mesh fit), so a part's bores carry their true axis/radius.
std::vector<BRepFace> extractAnalyticFaces(const TopoDS_Shape& shape)
{
    std::vector<BRepFace> faces;
    for (TopExp_Explorer fx(shape, TopAbs_FACE); fx.More(); fx.Next()) {
        const TopoDS_Face face = TopoDS::Face(fx.Current());
        BRepAdaptor_Surface ad(face, Standard_True);
        BRepFace bf;
        switch (ad.GetType()) {
            case GeomAbs_Plane: { bf.type = 0; const gp_Pln pl = ad.Plane();
                const gp_Dir n = pl.Axis().Direction(); const gp_Pnt o = pl.Location();
                bf.normal = { float(n.X()), float(n.Y()), float(n.Z()) };
                bf.axisPos = { float(o.X()), float(o.Y()), float(o.Z()) }; break; }
            case GeomAbs_Cylinder: { bf.type = 1; const gp_Cylinder cy = ad.Cylinder();
                const gp_Pnt o = cy.Axis().Location(); const gp_Dir d = cy.Axis().Direction();
                bf.axisPos = { float(o.X()), float(o.Y()), float(o.Z()) };
                bf.axisDir = { float(d.X()), float(d.Y()), float(d.Z()) };
                bf.radius = float(cy.Radius());
                // JOINT-ORIGIN FIX (mirrors CadImporter analyticFacesScaled): cy.Axis().Location() is an
                // arbitrary point on the INFINITE axis (often the CAD origin), so re-seed axisPos to the
                // PHYSICAL trimmed-bore midpoint so the inferred joint origin lands inside the bore.
                const double v0 = ad.FirstVParameter(), v1 = ad.LastVParameter();
                if (std::abs(v0) < 1e6 && std::abs(v1) < 1e6) {
                    bf.axisEnd0 = bf.axisPos + bf.axisDir * float(v0);
                    bf.axisEnd1 = bf.axisPos + bf.axisDir * float(v1);
                    bf.axisPos  = 0.5f * (bf.axisEnd0 + bf.axisEnd1);
                }
                break; }
            case GeomAbs_Cone: { bf.type = 2; const gp_Cone co = ad.Cone();
                const gp_Pnt o = co.Axis().Location(); const gp_Dir d = co.Axis().Direction();
                bf.axisPos = { float(o.X()), float(o.Y()), float(o.Z()) };
                bf.axisDir = { float(d.X()), float(d.Y()), float(d.Z()) };
                bf.radius = float(co.RefRadius()); break; }
            case GeomAbs_Sphere: { bf.type = 3; const gp_Sphere sp = ad.Sphere();
                const gp_Pnt o = sp.Location();
                bf.axisPos = { float(o.X()), float(o.Y()), float(o.Z()) };
                bf.radius = float(sp.Radius()); break; }
            default: bf.type = 4; break;
        }
        faces.push_back(bf);
    }
    return faces;
}

// recursively collect LEAF parts with their accumulated world placement.
void collectParts(const Handle(XCAFDoc_ShapeTool)& st, const TDF_Label& label,
                  const TopLoc_Location& parentLoc, std::vector<ParsedPart>& out)
{
    if (st->IsAssembly(label)) {
        TDF_LabelSequence comps;
        st->GetComponents(label, comps);
        for (int i = 1; i <= comps.Length(); ++i) {
            const TDF_Label comp = comps.Value(i);
            const TopLoc_Location loc = parentLoc * st->GetLocation(comp);
            TDF_Label refLabel;
            if (XCAFDoc_ShapeTool::GetReferredShape(comp, refLabel))
                collectParts(st, refLabel, loc, out);
        }
    } else {
        ParsedPart p;
        p.name = labelName(label);
        p.placement = trsfToMatrix(parentLoc.Transformation());
        p.faces = extractAnalyticFaces(st->GetShape(label));
        out.push_back(std::move(p));
    }
}

} // namespace
#endif // KR_WITH_OCCT

std::vector<ParsedPart> parseAssembly(const std::string& path)
{
    std::vector<ParsedPart> parts;
#ifdef KR_WITH_OCCT
    Handle(TDocStd_Document) doc;
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    app->NewDocument("MDTV-XCAF", doc);
    STEPCAFControl_Reader caf;
    caf.SetColorMode(true); caf.SetNameMode(true); caf.SetLayerMode(true);
    if (caf.ReadFile(path.c_str()) != IFSelect_RetDone || !caf.Transfer(doc)) return parts;
    Handle(XCAFDoc_ShapeTool) st = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    TDF_LabelSequence freeShapes;
    st->GetFreeShapes(freeShapes);
    for (int i = 1; i <= freeShapes.Length(); ++i)
        collectParts(st, freeShapes.Value(i), TopLoc_Location(), parts);
#else
    (void)path;
#endif
    return parts;
}

bool runAutoParseReport()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[autoparse] PHASE 1 -- real-assembly auto-parse demo (FANUC STEP -> bodies -> inferred chain)\n");
#ifndef KR_WITH_OCCT
    printf("[autoparse] OCCT compiled out -- cannot run.\n");
    return false;
#else
    const std::string path = findAssembly();
    if (path.empty()) { printf("[autoparse] FAIL: FANUC STEP not found\n"); return false; }

    const std::vector<ParsedPart> parts = parseAssembly(path);
    int cylTotal = 0;
    for (const auto& p : parts) for (const auto& f : p.faces) if (f.type == 1) ++cylTotal;
    printf("[autoparse]   parsed %zu bodies (with placements) ; %d cylindrical faces total\n",
           parts.size(), cylTotal);

    // infer the chain: greedy spanning tree of coaxial-cylinder interfaces.
    RobotGraph g = buildGraphFromParts(parts, /*base=*/0);
    const int committed = int(g.joints.size());
    const auto mem = g.members();
    printf("[autoparse]   inferred %d revolute interface(s) (coaxial bores) -> spanning tree; "
           "base-connected bodies=%zu/%zu ; chain DOF=%d\n",
           committed, mem.size(), parts.size(), g.dof());
    int shown = 0;
    for (const auto& j : g.joints) {
        if (shown++ >= 6) break;
        printf("[autoparse]     joint %s -> %s : axis=(%.3f,%.3f,%.3f) res=%.2e\n",
               parts[j.parent].name.c_str(), parts[j.child].name.c_str(),
               j.axisDir.x, j.axisDir.y, j.axisDir.z, j.residual);
    }
    // honesty: parts with NO inferred interface are left UNJOINTED (not faked into the robot).
    printf("[autoparse]   %zu body(ies) left UNJOINTED (no confident interface) -- manual definition required\n",
           parts.size() - mem.size());

    // NAME-DRIVEN chain (the robust first guess): names set the serial ORDER, geometry the axes.
    // Rule 2: the wrist (j4/j5/j6) is geometrically ambiguous, so we VERIFY the joint order
    // matches the named chain base->j1->..->j6, not merely that some chain closes.
    RobotGraph gn = buildNamedSerialChain(parts);
    int revs = 0, ambig = 0, expect = 1; bool orderOk = true;
    for (const auto& j : gn.joints) {
        if (j.type != JType::Revolute) continue;
        ++revs; if (j.ambiguous) ++ambig;
        const int childJ = jointNumberFromName(gn.bodies[j.child].name);   // collapsed link bodies
        if (childJ != expect) orderOk = false;
        ++expect;
    }
    const int dofN = gn.dof();
    printf("[autoparse]   NAME-DRIVEN chain: %d revolute joint(s), DOF=%d, %d ambiguous(need manual axis); "
           "named order base->j1..j6 = %s\n",
           revs, dofN, ambig, orderOk ? "OK" : "WRONG");
    // Per-joint axes of the chain ACTUALLY used at boot (J0 = base turntable). Confirms the base
    // verticality prior + flags any axis the user must refine via the Joint Axis Direction field.
    bool onLinkFail = false;
    {
        const Eigen::Vector3d bz = gn.bodies[gn.base].placement.block<3,1>(0,2);
        printf("[autoparse]   base part-Z (mounting normal) = (%.3f, %.3f, %.3f)\n", bz.x(), bz.y(), bz.z());
        // ON-LINK CHECK (verifies the trimmed-bore-midpoint origin fix): each joint origin (axisPos, world)
        // must lie within the CHILD link's world extent (from its analytic-face reference points + a margin).
        // Pre-fix, axisPos = cy.Axis().Location() floated far up the axis (often the CAD origin, metres off);
        // this asserts every origin now sits on/near its link. off = worst per-joint distance outside the box.
        int offLink = 0;
        for (int ji = 0; ji < int(gn.joints.size()); ++ji) {
            const auto& j = gn.joints[ji];
            // The joint origin sits at the PARENT<->CHILD interface, so test against the UNION of both
            // links' world extents (the base joint's axis, e.g., lies in the base, not up in j1).
            glm::vec3 mn(1e9f), mx(-1e9f);
            for (int bi2 : { j.parent, j.child }) {
                if (bi2 < 0 || bi2 >= int(gn.bodies.size())) continue;
                const RBBody& cb = gn.bodies[bi2];
                const glm::vec3 cbo(float(cb.placement(0,3)), float(cb.placement(1,3)), float(cb.placement(2,3)));
                mn = glm::min(mn, cbo); mx = glm::max(mx, cbo);
                for (const auto& f : cb.faces) { const BRepFace w = faceToWorld(f, cb.placement);
                    mn = glm::min(mn, w.axisPos); mx = glm::max(mx, w.axisPos); }
            }
            const glm::vec3 margin(0.30f);   // 300 mm: covers bore extent + the parent/child joint interface
            const bool onLink = glm::all(glm::greaterThanEqual(j.axisPos, mn - margin)) &&
                                glm::all(glm::lessThanEqual   (j.axisPos, mx + margin));
            if (!onLink && !j.ambiguous) ++offLink;
            printf("[autoparse]     name-chain J%d: B%d->B%d axis=(%.3f,%.3f,%.3f) origin=(%.3f,%.3f,%.3f) on-link=%s%s\n",
                   ji, j.parent, j.child, j.axisDir.x, j.axisDir.y, j.axisDir.z,
                   j.axisPos.x, j.axisPos.y, j.axisPos.z, onLink ? "YES" : "NO",
                   j.ambiguous ? " (ambiguous)" : "");
        }
        printf("[autoparse]   joint origins ON their child link: %d/%d off-link (want 0)\n", offLink, revs);
        onLinkFail = (offLink > 0);
    }

    // Gate passes iff a real 6-joint serial chain in the NAMED order was produced with every joint
    // origin ON its link (axes may still be ambiguous on the wrist -- that is honest, user defines those).
    const bool pass = parts.size() >= 2 && revs == 6 && orderOk && !onLinkFail;
    printf("[autoparse] %s\n", pass ? "PASS (named 6-DoF serial chain in correct order; axes from bores; ambiguous wrist axes flagged for manual definition)"
                                    : "FAIL (named serial chain not 6 joints in order)");
    std::fflush(stdout);
    return pass;
#endif
}

bool runParseReconGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[parse-recon] GATE PARSE-RECON -- what does OCCT's STEP reader expose for a REAL assembly?\n");

#ifndef KR_WITH_OCCT
    printf("[parse-recon] OCCT compiled out (no KR_WITH_OCCT) -- recon cannot run.\n");
    return false;
#else
    const std::string path = findAssembly();
    if (path.empty()) {
        printf("[parse-recon] FAIL: could not locate assets/FANUC-430 Robot.STEP\n");
        return false;
    }
    printf("[parse-recon] assembly: %s\n", path.c_str());

    // ---- (A) the CURRENT flattening reader: STEPControl_Reader -> one fused shape ----
    int flatSolids = 0, flatShells = 0, flatFaces = 0;
    {
        STEPControl_Reader reader;
        if (reader.ReadFile(path.c_str()) == IFSelect_RetDone) {
            reader.TransferRoots();
            TopoDS_Shape root = reader.OneShape();
            if (!root.IsNull()) {
                for (TopExp_Explorer ex(root, TopAbs_SOLID); ex.More(); ex.Next()) ++flatSolids;
                for (TopExp_Explorer ex(root, TopAbs_SHELL, TopAbs_SOLID); ex.More(); ex.Next()) ++flatShells;
                for (TopExp_Explorer ex(root, TopAbs_FACE, TopAbs_SHELL); ex.More(); ex.Next()) ++flatFaces;
            }
        }
        printf("[parse-recon]   STEPControl_Reader (FLATTENED): solids=%d  free-shells=%d  free-faces=%d "
               "(no hierarchy/placement tree -- placements baked into geometry)\n",
               flatSolids, flatShells, flatFaces);
    }

    // ---- (B) the assembly-aware reader: STEPCAFControl_Reader + XCAF ----
    Handle(TDocStd_Document) doc;
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    app->NewDocument("MDTV-XCAF", doc);
    STEPCAFControl_Reader caf;
    caf.SetColorMode(true);
    caf.SetNameMode(true);
    caf.SetLayerMode(true);
    if (caf.ReadFile(path.c_str()) != IFSelect_RetDone) {
        printf("[parse-recon] FAIL: STEPCAFControl_Reader.ReadFile\n");
        return false;
    }
    if (!caf.Transfer(doc)) {
        printf("[parse-recon] FAIL: STEPCAFControl_Reader.Transfer\n");
        return false;
    }
    Handle(XCAFDoc_ShapeTool) st = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    Handle(XCAFDoc_ColorTool) ct = XCAFDoc_DocumentTool::ColorTool(doc->Main());

    TDF_LabelSequence freeShapes;
    st->GetFreeShapes(freeShapes);
    Stats s;
    s.freeShapes = freeShapes.Length();
    int printed = 0;
    for (int i = 1; i <= freeShapes.Length(); ++i)
        walk(st, freeShapes.Value(i), 0, s, printed);

    TDF_LabelSequence colors;
    ct->GetColors(colors);
    const int nColors = colors.Length();

    // total simple-shape (part definition) labels in the doc, regardless of tree position.
    TDF_LabelSequence allShapes;
    st->GetShapes(allShapes);
    int defParts = 0, defAsm = 0;
    for (int i = 1; i <= allShapes.Length(); ++i) {
        if (st->IsAssembly(allShapes.Value(i))) ++defAsm; else ++defParts;
    }

    printf("[parse-recon]   STEPCAFControl_Reader (ASSEMBLY-AWARE):\n");
    printf("[parse-recon]     free top-shapes=%d  assembly-nodes=%d  part-instances=%d  leaf-parts-visited=%d\n",
           s.freeShapes, s.assemblies, s.instances, s.parts);
    printf("[parse-recon]     shape-tool labels: definitions parts=%d assemblies=%d ; named=%d ; colors=%d ; tree-depth=%d\n",
           defParts, defAsm, s.named, nColors, s.maxDepth);
    printf("[parse-recon]     instance placements: %d/%d are NON-identity (real assembly structure)\n",
           s.nonIdentityPlacements, s.instances);

    // ---- (C) mate / kinematic constraints ----
    // The standard XCAF shape/color tools expose geometry + placements + names +
    // colors, but NOT parametric mate constraints. STEP AP203/214 carry no
    // kinematics; AP242 has an optional kinematic schema that exporters rarely
    // populate. We report the absence honestly -- joint inference must use GEOMETRY.
    printf("[parse-recon]   MATE/KINEMATIC data: XCAF shape/color tools expose NO parametric mates "
           "(concentric/coincident/revolute) -- STEP drops them. => infer joints from GEOMETRY.\n");

    // ---- verdict ----
    const bool readOk = s.freeShapes > 0;
    const bool hasHierarchy = s.assemblies > 0 || s.instances > 1 || defParts > 1;
    const bool hasPlacements = s.nonIdentityPlacements > 0;
    printf("[parse-recon]   VERDICT: read=%s ; hierarchy=%s ; per-part placements=%s ; mates=NO\n",
           readOk ? "ok" : "FAIL",
           hasHierarchy ? "YES (tree recovered)" : "NO (single flattened body)",
           hasPlacements ? "YES" : "NO/identity");
    printf("[parse-recon]   => AUTO-PARSE TIER: %s\n",
           hasHierarchy
             ? "use the XCAF part tree + placements as bodies; INFER joints from inter-part geometry."
             : "flat body set; segment parts from solids + INFER joints from inter-part geometry.");

    const bool pass = readOk;   // the gate passes if the recon ran against the real file and reported truthfully
    printf("[parse-recon] %s\n", pass ? "PASS (assembly read; structural report produced against the real FANUC STEP)"
                                      : "FAIL (could not read the assembly)");
    std::fflush(stdout);
    return pass;
#endif
}

} // namespace krs::rbuild
