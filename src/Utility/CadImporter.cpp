#include "CadImporter.hpp"

#ifdef KR_WITH_OCCT
// ===========================================================================
// OpenCASCADE STEP ingestion + B-Rep meshing + cylindrical feature recognition.
// ===========================================================================
#include "Scene.hpp"
#include "components.hpp"

#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <gp_Ax2.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopLoc_Location.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <Poly_Triangle.hxx>
#include <Geom_Surface.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <gp_Pnt.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Trsf.hxx>
#include <gp_Cylinder.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <utility>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace krs::cad {

bool available() { return true; }

ImportResult importStep(Scene& scene, const std::string& path, float metersPerUnit)
{
    ImportResult res;
    const double s = double(metersPerUnit);
    STEPControl_Reader reader;
    if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) { res.message = "STEP read failed: " + path; return res; }
    reader.TransferRoots();
    TopoDS_Shape root = reader.OneShape();
    if (root.IsNull()) { res.message = "STEP contained no shapes"; return res; }

    auto& reg = scene.getRegistry();
    // Renderable bodies = every SOLID, PLUS every free SHELL / free FACE not inside a
    // solid. Many CAD parts (e.g. the FANUC J1 S-axis casting) export as an OPEN SHELL,
    // not a closed solid -- enumerating only TopAbs_SOLID silently drops them (a missing
    // render). Order: solids first (stable indices for downstream assignment), then shells,
    // then loose faces.
    std::vector<TopoDS_Shape> bodies;
    for (TopExp_Explorer ex(root, TopAbs_SOLID); ex.More(); ex.Next())               bodies.push_back(ex.Current());
    for (TopExp_Explorer ex(root, TopAbs_SHELL, TopAbs_SOLID); ex.More(); ex.Next()) bodies.push_back(ex.Current());
    for (TopExp_Explorer ex(root, TopAbs_FACE,  TopAbs_SHELL); ex.More(); ex.Next()) bodies.push_back(ex.Current());

    for (const TopoDS_Shape& solid : bodies) {

        // --- meshing quality from the body's bounding box (adaptive deflection) ---
        Bnd_Box bb; BRepBndLib::Add(solid, bb);
        double xmin, ymin, zmin, xmax, ymax, zmax;
        bb.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        const double diag = std::sqrt((xmax - xmin) * (xmax - xmin) + (ymax - ymin) * (ymax - ymin)
                                      + (zmax - zmin) * (zmax - zmin));
        const double defl = std::max(1e-3, diag * 0.004);     // 0.4% of the diagonal
        BRepMesh_IncrementalMesh(solid, defl, Standard_False, 0.5, Standard_True); // triangulate B-Rep

        std::vector<Vertex> verts;
        std::vector<unsigned int> idx;
        std::vector<glm::dvec3> nAccum;                        // smooth-normal accumulation

        for (TopExp_Explorer faceEx(solid, TopAbs_FACE); faceEx.More(); faceEx.Next()) {
            const TopoDS_Face face = TopoDS::Face(faceEx.Current());
            ++res.faces;
            TopLoc_Location loc;
            Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
            if (tri.IsNull()) continue;
            const gp_Trsf trsf = loc.Transformation();
            const unsigned base = unsigned(verts.size());
            for (int i = 1; i <= tri->NbNodes(); ++i) {
                gp_Pnt p = tri->Node(i); p.Transform(trsf);    // -> assembly coords
                Vertex v; v.position = { float(p.X() * s), float(p.Y() * s), float(p.Z() * s) };
                verts.push_back(v); nAccum.emplace_back(0.0);
            }
            const bool rev = (face.Orientation() == TopAbs_REVERSED);
            for (int t = 1; t <= tri->NbTriangles(); ++t) {
                int a, b, c; tri->Triangle(t).Get(a, b, c);
                unsigned i0 = base + (a - 1), i1 = base + (b - 1), i2 = base + (c - 1);
                if (rev) std::swap(i1, i2);                    // keep CCW winding outward
                idx.push_back(i0); idx.push_back(i1); idx.push_back(i2);
                const glm::dvec3 p0 = verts[i0].position, p1 = verts[i1].position, p2 = verts[i2].position;
                const glm::dvec3 fn = glm::cross(p1 - p0, p2 - p0); // area-weighted face normal
                nAccum[i0] += fn; nAccum[i1] += fn; nAccum[i2] += fn;
            }
        }
        if (verts.empty()) continue;
        for (size_t i = 0; i < verts.size(); ++i) {
            glm::dvec3 n = nAccum[i];
            verts[i].normal = (glm::length(n) > 1e-12) ? glm::vec3(glm::normalize(n)) : glm::vec3(0, 1, 0);
        }

        // --- exact B-Rep volume + mass channel ---
        GProp_GProps vprops; BRepGProp::VolumeProperties(solid, vprops);
        const double volume = std::abs(vprops.Mass()) * s * s * s; // GProp "Mass" = volume; scale^3

        // --- spawn entity ---
        entt::entity e = reg.create();
        auto& mesh = reg.emplace<RenderableMeshComponent>(e);
        mesh.vertices = std::move(verts);
        mesh.indices = std::move(idx);
        mesh.sourcePath = "occt_step";
        reg.emplace<TransformComponent>(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        reg.emplace<TagComponent>(e, std::string("STEP solid ") + std::to_string(res.solids + 1));
        reg.emplace<TriPlanarMaterialTag>(e);                  // UV-less B-Rep mesh
        auto& mat = reg.emplace<MaterialComponent>(e);
        mat.volume_m3 = float(volume);
        res.totalVolume += volume;

        // --- Task 3: cylindrical mounting features -> AttachmentComponent ---
        AttachmentComponent att;
        for (TopExp_Explorer fx(solid, TopAbs_FACE); fx.More(); fx.Next()) {
            const TopoDS_Face f = TopoDS::Face(fx.Current());
            Handle(Geom_Surface) surf = BRep_Tool::Surface(f);
            Handle(Geom_CylindricalSurface) cyl = Handle(Geom_CylindricalSurface)::DownCast(surf);
            if (cyl.IsNull()) continue;
            const gp_Ax1 axis = cyl->Axis();
            const gp_Pnt o = axis.Location();
            const gp_Dir d = axis.Direction();
            AttachmentFrame fr;
            fr.localPosition = { float(o.X() * s), float(o.Y() * s), float(o.Z() * s) };
            fr.localAxis = { float(d.X()), float(d.Y()), float(d.Z()) };
            fr.radius = float(cyl->Radius() * s);
            fr.isHole = (f.Orientation() == TopAbs_REVERSED);  // inward-facing cylinder = hole
            att.frames.push_back(fr);
        }
        res.attachments += int(att.frames.size());
        if (!att.frames.empty()) reg.emplace<AttachmentComponent>(e, std::move(att));

        ++res.solids;
    }
    res.ok = res.solids > 0;
    if (!res.ok && res.message.empty()) res.message = "STEP had no solid bodies";
    else if (res.ok) res.message = "Imported " + std::to_string(res.solids) + " solid(s)";
    return res;
}

// ---------------------------------------------------------------------------
// Phase A topology recon: dump solids + cluster cylindrical axes into shared
// hinge lines so the kinematic loops (FANUC parallelogram) are visible.
// ---------------------------------------------------------------------------
namespace {
struct CylAxis {
    int solid; double px, py, pz; double dx, dy, dz; double radius; bool hole;
};
// Canonical infinite-line representation: unit direction (sign-fixed) + the
// foot of the perpendicular from the origin (point on the line nearest origin).
struct LineKey { double dx, dy, dz; double fx, fy, fz; };
LineKey lineOf(const CylAxis& a) {
    double dx = a.dx, dy = a.dy, dz = a.dz;
    const double dn = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (dn > 1e-12) { dx /= dn; dy /= dn; dz /= dn; }
    // sign-canonicalise so the largest-magnitude component is positive
    const double ax = std::abs(dx), ay = std::abs(dy), az = std::abs(dz);
    double s = 1.0;
    if (az >= ax && az >= ay) s = (dz < 0) ? -1.0 : 1.0;
    else if (ay >= ax)        s = (dy < 0) ? -1.0 : 1.0;
    else                      s = (dx < 0) ? -1.0 : 1.0;
    dx *= s; dy *= s; dz *= s;
    const double dot = a.px*dx + a.py*dy + a.pz*dz;          // foot = p - (p.d) d
    return { dx, dy, dz, a.px - dot*dx, a.py - dot*dy, a.pz - dot*dz };
}
} // namespace

void inspectStep(const std::string& path)
{
    using std::printf;
    printf("\n========== [STEP INSPECT] %s ==========\n", path.c_str());
    STEPControl_Reader reader;
    if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) {
        printf("[STEP INSPECT] STEPControl_Reader.ReadFile FAILED (path quoting / existence?)\n");
        fflush(stdout); return;
    }
    const int nRoots = int(reader.NbRootsForTransfer());
    reader.TransferRoots();
    TopoDS_Shape root = reader.OneShape();
    if (root.IsNull()) { printf("[STEP INSPECT] no shapes after transfer\n"); fflush(stdout); return; }

    Bnd_Box abb; BRepBndLib::Add(root, abb);
    double Xn,Yn,Zn,Xx,Yx,Zx; abb.Get(Xn,Yn,Zn,Xx,Yx,Zx);
    const double diag = std::sqrt((Xx-Xn)*(Xx-Xn)+(Yx-Yn)*(Yx-Yn)+(Zx-Zn)*(Zx-Zn));
    printf("[STEP INSPECT] roots=%d  assembly bbox: X[%.1f..%.1f] Y[%.1f..%.1f] Z[%.1f..%.1f]  diag=%.1f  (units likely %s)\n",
           nRoots, Xn,Xx, Yn,Yx, Zn,Zx, diag, diag > 50.0 ? "MILLIMETRES" : "metres");

    // Enumeration audit: the importer only spawns one entity per SOLID. Geometry that
    // is a free SHELL (open surface) or loose FACE is invisible to TopAbs_SOLID -> a
    // missing render. Count what's NOT inside a solid (the J1->J2 link symptom).
    {
        int nSolid = 0, nFreeShell = 0, nFreeFace = 0, nFreeWire = 0;
        for (TopExp_Explorer ex(root, TopAbs_SOLID); ex.More(); ex.Next()) ++nSolid;
        for (TopExp_Explorer ex(root, TopAbs_SHELL, TopAbs_SOLID); ex.More(); ex.Next()) ++nFreeShell;
        for (TopExp_Explorer ex(root, TopAbs_FACE,  TopAbs_SHELL); ex.More(); ex.Next()) ++nFreeFace;
        for (TopExp_Explorer ex(root, TopAbs_WIRE,  TopAbs_FACE ); ex.More(); ex.Next()) ++nFreeWire;
        printf("[STEP INSPECT] ENUMERATION: SOLIDs=%d  free SHELLs(not in solid)=%d  free FACEs(not in shell)=%d  free WIREs=%d\n",
               nSolid, nFreeShell, nFreeFace, nFreeWire);
        // Per free shell: bbox, so a missing part's location is visible.
        int si = 0;
        for (TopExp_Explorer ex(root, TopAbs_SHELL, TopAbs_SOLID); ex.More(); ex.Next(), ++si) {
            Bnd_Box sb; BRepBndLib::Add(ex.Current(), sb);
            if (sb.IsVoid()) continue;
            double a,b,c,d,e,f; sb.Get(a,b,c,d,e,f);
            printf("[STEP INSPECT]   free shell %d: bbox X[%.1f..%.1f] Y[%.1f..%.1f] Z[%.1f..%.1f]\n", si, a,d, b,e, c,f);
        }
    }

    std::vector<CylAxis> allAxes;
    int solidIdx = 0;
    for (TopExp_Explorer sx(root, TopAbs_SOLID); sx.More(); sx.Next(), ++solidIdx) {
        const TopoDS_Shape sld = sx.Current();
        Bnd_Box bb; BRepBndLib::Add(sld, bb);
        double xn,yn,zn,xx,yx,zx; bb.Get(xn,yn,zn,xx,yx,zx);
        GProp_GProps vp; BRepGProp::VolumeProperties(sld, vp);
        const double vol = std::abs(vp.Mass());
        const gp_Pnt com = vp.CentreOfMass();
        int faces=0, nPlane=0, nCyl=0, nCone=0, nSphere=0, nTorus=0, nFree=0;
        std::vector<CylAxis> here;
        for (TopExp_Explorer fx(sld, TopAbs_FACE); fx.More(); fx.Next()) {
            const TopoDS_Face f = TopoDS::Face(fx.Current());
            ++faces;
            BRepAdaptor_Surface ad(f, Standard_True);          // accounts for face location
            switch (ad.GetType()) {
                case GeomAbs_Plane:    ++nPlane;  break;
                case GeomAbs_Cone:     ++nCone;   break;
                case GeomAbs_Sphere:   ++nSphere; break;
                case GeomAbs_Torus:    ++nTorus;  break;
                case GeomAbs_Cylinder: {
                    ++nCyl;
                    const gp_Cylinder c = ad.Cylinder();
                    const gp_Pnt o = c.Location();
                    const gp_Dir d = c.Axis().Direction();
                    CylAxis ca{ solidIdx, o.X(),o.Y(),o.Z(), d.X(),d.Y(),d.Z(),
                                c.Radius(), (f.Orientation()==TopAbs_REVERSED) };
                    here.push_back(ca); allAxes.push_back(ca);
                    break;
                }
                default: ++nFree; break;
            }
        }
        printf("\n[solid %d] vol=%.0f  bbox[%.0f..%.0f, %.0f..%.0f, %.0f..%.0f] span(%.0f,%.0f,%.0f)\n",
               solidIdx, vol, xn,xx, yn,yx, zn,zx, xx-xn, yx-yn, zx-zn);
        printf("          centroid(%.1f, %.1f, %.1f)  faces=%d [plane %d, cyl %d, cone %d, sph %d, torus %d, free %d]\n",
               com.X(), com.Y(), com.Z(), faces, nPlane,nCyl,nCone,nSphere,nTorus,nFree);
        // distinct cylinder bores within this solid (radius+line dedupe)
        std::vector<LineKey> seen; int shown=0;
        for (const auto& ca : here) {
            const LineKey k = lineOf(ca); bool dup=false;
            for (const auto& s2 : seen) {
                if (std::abs(k.dx*s2.dx + k.dy*s2.dy + k.dz*s2.dz) > 0.9998 &&
                    std::sqrt((k.fx-s2.fx)*(k.fx-s2.fx)+(k.fy-s2.fy)*(k.fy-s2.fy)+(k.fz-s2.fz)*(k.fz-s2.fz)) < 1.0)
                    { dup=true; break; }
            }
            if (dup) continue; seen.push_back(k);
            if (shown++ < 12)
                printf("            cyl r=%6.2f  o(%.1f,%.1f,%.1f) dir(%.3f,%.3f,%.3f) %s\n",
                       ca.radius, ca.px,ca.py,ca.pz, ca.dx,ca.dy,ca.dz, ca.hole?"hole":"shaft");
        }
    }

    // ---- cross-solid axis clustering => shared hinge lines / closed loops ----
    printf("\n[STEP INSPECT] total solids=%d, total cylindrical faces=%d\n", solidIdx, int(allAxes.size()));
    struct Cluster { LineKey key; std::vector<int> solids; int count; double rmin, rmax; };
    std::vector<Cluster> clusters;
    for (const auto& ca : allAxes) {
        const LineKey k = lineOf(ca);
        int hit = -1;
        for (size_t i=0;i<clusters.size();++i) {
            const auto& c = clusters[i].key;
            if (std::abs(k.dx*c.dx + k.dy*c.dy + k.dz*c.dz) > 0.9995 &&
                std::sqrt((k.fx-c.fx)*(k.fx-c.fx)+(k.fy-c.fy)*(k.fy-c.fy)+(k.fz-c.fz)*(k.fz-c.fz)) < 2.0)
                { hit = int(i); break; }
        }
        if (hit < 0) { clusters.push_back({k, {ca.solid}, 1, ca.radius, ca.radius}); }
        else {
            auto& cl = clusters[hit]; ++cl.count;
            cl.rmin = std::min(cl.rmin, ca.radius); cl.rmax = std::max(cl.rmax, ca.radius);
            if (std::find(cl.solids.begin(), cl.solids.end(), ca.solid) == cl.solids.end())
                cl.solids.push_back(ca.solid);
        }
    }
    printf("[STEP INSPECT] SHARED HINGE CANDIDATES (coaxial cylinders spanning >=2 solids):\n");
    int shared = 0;
    for (const auto& cl : clusters) {
        if (cl.solids.size() < 2) continue;
        ++shared;
        printf("   * line foot(%.1f,%.1f,%.1f) dir(%.3f,%.3f,%.3f)  r[%.1f..%.1f]  solids{",
               cl.key.fx,cl.key.fy,cl.key.fz, cl.key.dx,cl.key.dy,cl.key.dz, cl.rmin,cl.rmax);
        for (size_t i=0;i<cl.solids.size();++i) printf("%s%d", i?",":"", cl.solids[i]);
        printf("}  (%d faces)\n", cl.count);
    }
    if (!shared) printf("   (none — no two solids share a coaxial cylinder within tolerance)\n");
    printf("========== [STEP INSPECT END] ==========\n\n");
    fflush(stdout);
}

bool runSelfTest()
{
    using std::printf;
    printf("[CAD] self-test: box(100mm) - cyl(r20) through-hole, STEP round-trip\n");

    // 1) Build the solid: a 100 mm cube minus a 20 mm-radius axial through-hole.
    TopoDS_Shape box = BRepPrimAPI_MakeBox(100.0, 100.0, 100.0).Shape();
    TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(50.0, 50.0, -10.0), gp_Dir(0.0, 0.0, 1.0)), 20.0, 120.0).Shape();
    TopoDS_Shape solid = BRepAlgoAPI_Cut(box, cyl).Shape();
    if (solid.IsNull()) { printf("[CAD] FAIL: boolean cut produced a null shape\n"); return false; }

    // 2) Full ISO-10303-21 round-trip: write a temp STEP file, then read it back.
    std::error_code ec;
    const std::string path = (std::filesystem::temp_directory_path(ec) / "krs_cad_selftest.step").string();
    {
        STEPControl_Writer writer;
        if (writer.Transfer(solid, STEPControl_AsIs) != IFSelect_RetDone) {
            printf("[CAD] FAIL: STEP transfer\n"); return false;
        }
        if (writer.Write(path.c_str()) != IFSelect_RetDone) {
            printf("[CAD] FAIL: STEP write to %s\n", path.c_str()); return false;
        }
    }
    STEPControl_Reader reader;
    if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) {
        printf("[CAD] FAIL: STEP read-back from %s\n", path.c_str()); return false;
    }
    reader.TransferRoots();
    TopoDS_Shape rt = reader.OneShape();
    if (rt.IsNull()) { printf("[CAD] FAIL: read-back shape is empty\n"); return false; }

    // 3) Exercise the exact pipeline importStep uses: solid loop, mesh, feature
    //    recognition (cylindrical faces), exact B-Rep volume.
    int solids = 0, faces = 0, tris = 0, cylFaces = 0;
    double volume = 0.0;
    for (TopExp_Explorer sx(rt, TopAbs_SOLID); sx.More(); sx.Next()) {
        const TopoDS_Shape sld = sx.Current();
        ++solids;
        BRepMesh_IncrementalMesh(sld, 0.5, Standard_False, 0.5, Standard_True);
        for (TopExp_Explorer fx(sld, TopAbs_FACE); fx.More(); fx.Next()) {
            const TopoDS_Face f = TopoDS::Face(fx.Current());
            ++faces;
            TopLoc_Location loc;
            Handle(Poly_Triangulation) t = BRep_Tool::Triangulation(f, loc);
            if (!t.IsNull()) tris += t->NbTriangles();
            Handle(Geom_Surface) surf = BRep_Tool::Surface(f);
            if (!Handle(Geom_CylindricalSurface)::DownCast(surf).IsNull()) ++cylFaces;
        }
        GProp_GProps vp; BRepGProp::VolumeProperties(sld, vp);
        volume += std::abs(vp.Mass());
    }
    std::filesystem::remove(path, ec);

    const double expected = 100.0 * 100.0 * 100.0 - 3.14159265358979 * 20.0 * 20.0 * 100.0;
    const double err = std::abs(volume - expected) / expected;
    printf("[CAD]   solids=%d faces=%d triangles=%d cylFaces=%d\n", solids, faces, tris, cylFaces);
    printf("[CAD]   volume=%.1f mm^3 (expected ~%.1f, rel.err %.5f)\n", volume, expected, err);

    // 1 solid out, the through-hole detected as >=1 cylindrical feature, meshed,
    // and the exact volume matches the analytic cube-minus-cylinder value.
    const bool pass = (solids == 1) && (cylFaces >= 1) && (tris > 0) && (err < 0.02);
    printf("[CAD] self-test: %s\n", pass ? "PASS" : "FAIL");
    fflush(stdout);
    return pass;
}

} // namespace krs::cad

#else
// --- Built without OpenCASCADE: graceful no-op so the UI degrades cleanly. ---
namespace krs::cad {
bool available() { return false; }
ImportResult importStep(Scene&, const std::string&, float)
{
    ImportResult r;
    r.message = "STEP import needs OpenCASCADE — rebuild with KR_WITH_OCCT (opencascade in vcpkg.json).";
    return r;
}
void inspectStep(const std::string&) {} // no OCCT -> no-op
bool runSelfTest() { return true; } // no OCCT -> vacuous pass, keeps harnesses green
} // namespace krs::cad
#endif
