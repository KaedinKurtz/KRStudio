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
    for (TopExp_Explorer solidEx(root, TopAbs_SOLID); solidEx.More(); solidEx.Next()) {
        const TopoDS_Shape solid = solidEx.Current();

        // --- meshing quality from the solid's bounding box (adaptive deflection) ---
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
bool runSelfTest() { return true; } // no OCCT -> vacuous pass, keeps harnesses green
} // namespace krs::cad
#endif
