// SnapGate.cpp -- the KRS_SNAP_SELFTEST gate for krs::snap (see Snap.hpp).
// Headless, pure CPU, synthetic entities in a Scene registry; analytic assertions
// with measured numbers; NEG-CTRLs. Mirrors the GroupOps/Measure gate style.
#include "Snap.hpp"
#include "Scene.hpp"
#include "components.hpp"    // TransformComponent, RenderableMeshComponent, BRepFace, computeFaceKey
#include "BRepEdge.hpp"
#include "BRepVertex.hpp"

#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <cstdio>

namespace krs::snap {

namespace {

struct GSuite {
    int pass = 0, total = 0;
    void check(const char* name, bool ok) {
        ++total; if (ok) ++pass;
        std::printf("[snap]   %-4s %s\n", ok ? "PASS" : "FAIL", name);
        std::fflush(stdout);
    }
};

bool near3(const glm::vec3& a, const glm::vec3& b, float tol = 1e-5f) {
    return glm::length(a - b) < tol;
}

int countKind(const std::vector<SnapCandidate>& v, SnapKind k) {
    int n = 0;
    for (const auto& c : v) if (c.kind == k) ++n;
    return n;
}

const SnapCandidate* findAt(const std::vector<SnapCandidate>& v, SnapKind k,
                            const glm::vec3& pos, float tol = 1e-5f) {
    for (const auto& c : v) if (c.kind == k && near3(c.pos, pos, tol)) return &c;
    return nullptr;
}

// SYNTHETIC BODY: face 0 = unit square plane at z=0, corners (0,0,0)..(0,1,0), OUTWARD normal
// +Z (the importer's guaranteed convention after the P3 orientation fix); face 1 = a trimmed
// cylinder (axis +Z, rims at z=0 and z=2, r=0.5) that shares NO boundary topology with face 0.
// Tessellated quad + triFace; 4 line edges (faceA=0); 4 true vertices with full adjacency.
entt::entity mkBody(entt::registry& reg, const TransformComponent& xf, const char* name) {
    entt::entity e = reg.create();
    reg.emplace<TransformComponent>(e, xf);
    reg.emplace<TagComponent>(e, std::string(name));

    const glm::vec3 P[4] = { {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0} };

    auto& mesh = reg.emplace<RenderableMeshComponent>(e);
    for (const auto& p : P) { Vertex v; v.position = p; v.normal = { 0,0,1 }; mesh.vertices.push_back(v); }
    mesh.indices = { 0, 1, 2, 0, 2, 3 };
    mesh.triFace = { 0, 0 };

    auto& fc = reg.emplace<BRepFaceComponent>(e);
    BRepFace plane; plane.type = 0; plane.normal = { 0,0,1 }; plane.axisPos = { 0.5f, 0.5f, 0 };
    plane.faceKey = computeFaceKey(plane);
    fc.faces.push_back(plane);
    BRepFace cyl; cyl.type = 1; cyl.axisDir = { 0,0,1 };
    cyl.axisEnd0 = { 0,0,0 }; cyl.axisEnd1 = { 0,0,2 }; cyl.axisPos = { 0,0,1 }; cyl.radius = 0.5f;
    cyl.faceKey = computeFaceKey(cyl);
    fc.faces.push_back(cyl);

    auto& ec = reg.emplace<BRepEdgeComponent>(e);
    for (int i = 0; i < 4; ++i) {
        BRepEdge be; be.kind = BRepEdge::Line;
        be.p0 = P[i]; be.p1 = P[(i + 1) % 4];
        be.axisDir = glm::normalize(be.p1 - be.p0);
        be.faceA = 0;
        be.polyline = { be.p0, be.p1 };
        be.edgeKey = computeEdgeKey(be);
        ec.edges.push_back(be);
    }

    auto& vc = reg.emplace<BRepVertexComponent>(e);
    for (int i = 0; i < 4; ++i) {
        BRepVertex bv; bv.pos = P[i];
        bv.faces = { 0 };
        bv.edges = { (i + 3) % 4, i };            // the two square edges meeting at this corner
        bv.vertexKey = computeVertexKey(bv);
        vc.verts.push_back(bv);
    }
    return e;
}

} // namespace

bool runSnapGate() {
    std::printf("[snap] ================ SNAP GATE (krs::snap) ================\n");
    GSuite S;
    Scene scene;
    auto& reg = scene.getRegistry();

    const glm::vec3 corners[4] = { {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0} };
    const glm::vec3 mids[4]    = { {0.5f,0,0}, {1,0.5f,0}, {0.5f,1,0}, {0,0.5f,0} };

    // ================= (1) SQUARE PLANE FACE (identity transform) =================
    entt::entity A = mkBody(reg, TransformComponent{}, "A");
    const auto fA = candidatesForFace(reg, A, 0);
    std::printf("[snap] square face -> %d candidates: centroid=%d vertex=%d midpoint=%d endpoint=%d "
                "(want 13 = 1+4+4+4)\n", int(fA.size()),
                countKind(fA, SnapKind::FaceCentroid), countKind(fA, SnapKind::BoundaryVertex),
                countKind(fA, SnapKind::EdgeMidpoint), countKind(fA, SnapKind::EdgeEndpoint));
    S.check("FACE-COUNTS       square: 1 centroid + 4 vertices + 4 midpoints + 4 deduped endpoints",
            fA.size() == 13
            && countKind(fA, SnapKind::FaceCentroid) == 1
            && countKind(fA, SnapKind::BoundaryVertex) == 4
            && countKind(fA, SnapKind::EdgeMidpoint) == 4
            && countKind(fA, SnapKind::EdgeEndpoint) == 4);

    const SnapCandidate* cen = findAt(fA, SnapKind::FaceCentroid, { 0.5f, 0.5f, 0 });
    if (cen) std::printf("[snap] centroid at (%.6f, %.6f, %.6f) z=(%.3f,%.3f,%.3f) x=(%.3f,%.3f,%.3f) glyph=%d\n",
                         cen->pos.x, cen->pos.y, cen->pos.z, cen->z.x, cen->z.y, cen->z.z,
                         cen->x.x, cen->x.y, cen->x.z, cen->glyph);
    S.check("FACE-CENTROID     area-weighted centroid EXACTLY at (0.5,0.5,0), glyph=square(0)",
            cen && cen->glyph == 0);
    S.check("FACE-CENTROID-Z   centroid +Z == the OUTWARD stored normal (0,0,1) (importer convention)",
            cen && near3(cen->z, { 0,0,1 }, 1e-6f));
    S.check("FACE-CENTROID-X   centroid X == deterministic (1,0,0)",
            cen && near3(cen->x, { 1,0,0 }, 1e-6f));
    S.check("FACE-CENTROID-KEY centroid carries the owning faceKey (nonzero, durable re-anchor)",
            cen && cen->key != 0 && cen->key == reg.get<BRepFaceComponent>(A).faces[0].faceKey);

    {
        bool cornersOk = true, endsOk = true, midsOk = true, zOut = true, keysOk = true;
        const auto& vcomp = reg.get<BRepVertexComponent>(A).verts;
        for (int i = 0; i < 4; ++i) {
            const SnapCandidate* bv = findAt(fA, SnapKind::BoundaryVertex, corners[i]);
            const SnapCandidate* ep = findAt(fA, SnapKind::EdgeEndpoint, corners[i]);
            const SnapCandidate* mp = findAt(fA, SnapKind::EdgeMidpoint, mids[i]);
            cornersOk = cornersOk && bv && bv->glyph == 3 && bv->vertexId == i;
            endsOk    = endsOk && ep && ep->glyph == 3;
            midsOk    = midsOk && mp && mp->glyph == 2;
            keysOk    = keysOk && bv && bv->key == vcomp[std::size_t(i)].vertexKey && bv->key != 0;
        }
        for (const auto& c : fA) zOut = zOut && near3(c.z, { 0,0,1 }, 1e-6f);
        S.check("FACE-VERTICES     4 BoundaryVertex EXACTLY at the corners, glyph=circle(3), vertexId hints",
                cornersOk);
        S.check("FACE-VERTEX-KEYS  each BoundaryVertex carries ITS vertexKey (nonzero)", keysOk);
        S.check("FACE-MIDPOINTS    4 EdgeMidpoints EXACTLY at the edge midpoints, glyph=triangle(2)",
                midsOk);
        S.check("FACE-ENDPOINTS    8 raw endpoints dedupe to 4 unique corner positions, glyph=circle(3)",
                endsOk);
        S.check("FACE-Z-OUT        EVERY face-derived candidate's +Z == the outward normal (13/13)",
                zOut);
    }

    // ================= (2) CYLINDER FACE (rims known) =================
    const auto fC = candidatesForFace(reg, A, 1);
    std::printf("[snap] cylinder face -> %d candidates (want exactly 3 AxisPoints)\n", int(fC.size()));
    {
        const SnapCandidate* a0 = findAt(fC, SnapKind::AxisPoint, { 0,0,0 });
        const SnapCandidate* a1 = findAt(fC, SnapKind::AxisPoint, { 0,0,1 });
        const SnapCandidate* a2 = findAt(fC, SnapKind::AxisPoint, { 0,0,2 });
        S.check("CYL-AXIS-3        exactly 3 AxisPoints at end0/mid/end1 (0,0,0)/(0,0,1)/(0,0,2)",
                fC.size() == 3 && a0 && a1 && a2);
        S.check("CYL-AXIS-INDEX    axis points disambiguated index 0/1/2",
                a0 && a1 && a2 && a0->index == 0 && a1->index == 1 && a2->index == 2);
        S.check("CYL-AXIS-FRAME    +Z ALONG the axis (0,0,1); glyph=diamond(4)",
                a0 && near3(a0->z, { 0,0,1 }, 1e-6f) && a0->glyph == 4 && a1->glyph == 4 && a2->glyph == 4);
    }

    // ================= (2b) CONE APEX / SPHERE CENTRE / INNER-WIRE CENTROID =================
    {
        entt::entity B = reg.create();
        reg.emplace<TransformComponent>(B, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        auto& fc = reg.emplace<BRepFaceComponent>(B);
        BRepFace cone; cone.type = 2; cone.axisPos = { 0,0,4 }; cone.axisDir = { 0,0,1 };
        cone.radius = 0.5f; cone.faceKey = computeFaceKey(cone);
        BRepFace sph; sph.type = 3; sph.axisPos = { 7,8,9 }; sph.radius = 1.0f;
        sph.faceKey = computeFaceKey(sph);
        BRepFace disk; disk.type = 0; disk.normal = { 0,0,1 }; disk.axisPos = { 0,0,0 };
        disk.faceKey = computeFaceKey(disk);
        fc.faces.push_back(cone); fc.faces.push_back(sph); fc.faces.push_back(disk);
        auto& ec = reg.emplace<BRepEdgeComponent>(B);
        BRepEdge hole; hole.kind = BRepEdge::Circle; hole.center = { 0.25f, 0.25f, 0 };
        hole.axisDir = { 0,0,1 }; hole.radius = 0.1f; hole.closed = true;
        hole.p0 = hole.p1 = { 0.35f, 0.25f, 0 }; hole.polyline = { hole.p0, hole.p1 };
        hole.faceA = 2;                                        // bounds the planar disk face
        hole.edgeKey = computeEdgeKey(hole);
        ec.edges.push_back(hole);

        const auto fCone = candidatesForFace(reg, B, 0);
        S.check("CONE-APEX         cone -> ConeApex at the DOCUMENTED fallback axisPos (no half-angle channel), glyph=diamond(4)",
                fCone.size() == 1 && fCone[0].kind == SnapKind::ConeApex
                && near3(fCone[0].pos, { 0,0,4 }, 1e-6f) && near3(fCone[0].z, { 0,0,1 }, 1e-6f)
                && fCone[0].glyph == 4);
        const auto fSph = candidatesForFace(reg, B, 1);
        S.check("SPHERE-CENTRE     sphere -> its centre as AxisPoint index 1",
                fSph.size() == 1 && fSph[0].kind == SnapKind::AxisPoint
                && near3(fSph[0].pos, { 7,8,9 }, 1e-6f) && fSph[0].index == 1);
        const auto fDisk = candidatesForFace(reg, B, 2);
        const SnapCandidate* iw = findAt(fDisk, SnapKind::InnerWireCentroid, { 0.25f, 0.25f, 0 });
        std::printf("[snap] planar face w/ closed circular boundary -> %d candidate(s), inner-wire centroid %s\n",
                    int(fDisk.size()), iw ? "FOUND" : "MISSING");
        S.check("INNER-WIRE        closed circle on a PLANAR face -> InnerWireCentroid at its centre (documented approx), +Z = plane normal",
                iw && near3(iw->z, { 0,0,1 }, 1e-6f) && iw->glyph == 1 && iw->key != 0);
    }

    // ================= (3) HOVERED EDGES =================
    entt::entity C = reg.create();
    reg.emplace<TransformComponent>(C, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    {
        auto& ec = reg.emplace<BRepEdgeComponent>(C);
        BRepEdge circ; circ.kind = BRepEdge::Circle; circ.center = { 1,2,3 };
        circ.axisDir = { 0,1,0 }; circ.radius = 0.25f; circ.closed = true;
        circ.p0 = circ.p1 = { 1.25f, 2, 3 };
        circ.polyline = { circ.p0, circ.p1 };
        circ.edgeKey = computeEdgeKey(circ);
        ec.edges.push_back(circ);
        BRepEdge line; line.kind = BRepEdge::Line; line.p0 = { 0,0,0 }; line.p1 = { 2,0,0 };
        line.axisDir = { 1,0,0 };
        line.polyline = { line.p0, line.p1 };
        line.edgeKey = computeEdgeKey(line);
        ec.edges.push_back(line);
        BRepEdge arc; arc.kind = BRepEdge::Circle; arc.center = { 0,0,0 };
        arc.axisDir = { 0,0,1 }; arc.radius = 1.0f; arc.closed = false;
        arc.p0 = { 1,0,0 }; arc.p1 = { 0,1,0 };
        arc.polyline = { arc.p0, arc.p1 };
        arc.edgeKey = computeEdgeKey(arc);
        ec.edges.push_back(arc);
    }
    const auto eCirc = candidatesForEdge(reg, C, 0);
    std::printf("[snap] circle edge -> %d candidate(s); line edge -> %d; arc edge -> %d\n",
                int(eCirc.size()), int(candidatesForEdge(reg, C, 1).size()),
                int(candidatesForEdge(reg, C, 2).size()));
    S.check("EDGE-CIRCLE       closed circle -> ONE CircleCenter at (1,2,3), +Z along the circle axis (0,1,0), glyph=plus(1)",
            eCirc.size() == 1 && eCirc[0].kind == SnapKind::CircleCenter
            && near3(eCirc[0].pos, { 1,2,3 }, 1e-6f) && near3(eCirc[0].z, { 0,1,0 }, 1e-6f)
            && eCirc[0].glyph == 1 && eCirc[0].key == reg.get<BRepEdgeComponent>(C).edges[0].edgeKey);
    {
        const auto eLine = candidatesForEdge(reg, C, 1);
        const SnapCandidate* mp = findAt(eLine, SnapKind::EdgeMidpoint, { 1,0,0 });
        const SnapCandidate* e0 = findAt(eLine, SnapKind::EdgeEndpoint, { 0,0,0 });
        const SnapCandidate* e1 = findAt(eLine, SnapKind::EdgeEndpoint, { 2,0,0 });
        S.check("EDGE-LINE         line -> midpoint (1,0,0) glyph=triangle(2) + endpoints index 0/1 glyph=circle(3)",
                eLine.size() == 3 && mp && mp->glyph == 2
                && e0 && e1 && e0->index == 0 && e1->index == 1 && e0->glyph == 3);
        S.check("EDGE-LINE-FRAME   +Z along the line (1,0,0); X = the Y-projection fallback (0,1,0)",
                mp && near3(mp->z, { 1,0,0 }, 1e-6f) && near3(mp->x, { 0,1,0 }, 1e-6f));
        const auto eArc = candidatesForEdge(reg, C, 2);
        const SnapCandidate* ac = findAt(eArc, SnapKind::CircleCenter, { 0,0,0 });
        S.check("EDGE-ARC          open arc -> CircleCenter (0,0,0) + BOTH arc endpoints",
                eArc.size() == 3 && ac
                && findAt(eArc, SnapKind::EdgeEndpoint, { 1,0,0 })
                && findAt(eArc, SnapKind::EdgeEndpoint, { 0,1,0 }));
    }

    // ================= (4) HOVERED TRUE VERTEX =================
    entt::entity D = reg.create();
    reg.emplace<TransformComponent>(D, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    {
        auto& fc = reg.emplace<BRepFaceComponent>(D);
        BRepFace f0; f0.type = 0; f0.normal = { 0,0,1 }; f0.faceKey = computeFaceKey(f0);
        BRepFace f1; f1.type = 0; f1.normal = { 1,0,0 }; f1.faceKey = computeFaceKey(f1);
        fc.faces.push_back(f0); fc.faces.push_back(f1);
        auto& vc = reg.emplace<BRepVertexComponent>(D);
        BRepVertex v0; v0.pos = { 0.25f, 0.25f, 0 }; v0.faces = { 0, 1 }; v0.vertexKey = computeVertexKey(v0);
        BRepVertex v1; v1.pos = { 5, 5, 5 };         /* NO adjacent faces */ v1.vertexKey = computeVertexKey(v1);
        vc.verts.push_back(v0); vc.verts.push_back(v1);
    }
    {
        const SnapCandidate va = candidateForVertex(reg, D, 0, -1);
        const SnapCandidate vb = candidateForVertex(reg, D, 0, 1);
        const SnapCandidate vc_ = candidateForVertex(reg, D, 1, -1);
        std::printf("[snap] vertex frames: default-face z=(%.2f,%.2f,%.2f)  through-face-1 z=(%.2f,%.2f,%.2f)  "
                    "orphan z=(%.2f,%.2f,%.2f)\n", va.z.x, va.z.y, va.z.z, vb.z.x, vb.z.y, vb.z.z,
                    vc_.z.x, vc_.z.y, vc_.z.z);
        S.check("VERTEX-DEFAULT    throughFaceId=-1 -> frame from the FIRST adjacent face (+Z=(0,0,1))",
                va.entity == D && near3(va.pos, { 0.25f, 0.25f, 0 }, 1e-6f)
                && near3(va.z, { 0,0,1 }, 1e-6f) && va.faceId == 0 && va.vertexId == 0 && va.glyph == 3);
        S.check("VERTEX-THROUGH    throughFaceId=1 -> frame follows THAT face (+Z=(1,0,0))",
                vb.entity == D && near3(vb.z, { 1,0,0 }, 1e-6f) && vb.faceId == 1);
        S.check("VERTEX-ORPHAN     no adjacent face -> +Z == +global Z (Onshape curve-point rule)",
                vc_.entity == D && near3(vc_.z, { 0,0,1 }, 1e-6f) && vc_.faceId == -1);
    }

    // ================= (5) deterministicX =================
    {
        const glm::vec3 x1 = deterministicX({ 0,0,1 });
        const glm::vec3 x2 = deterministicX({ 0,0,1 });
        std::printf("[snap] deterministicX(+Z) = (%.6f, %.6f, %.6f)  |x|=%.6f\n",
                    x1.x, x1.y, x1.z, glm::length(x1));
        S.check("DETX-BASIC        x(+Z) == (1,0,0): unit, |dot(x,z)| < 1e-6",
                near3(x1, { 1,0,0 }, 1e-6f) && std::abs(glm::length(x1) - 1.0f) < 1e-6f
                && std::abs(glm::dot(x1, glm::vec3(0,0,1))) < 1e-6f);
        S.check("DETX-STABLE       two calls return the IDENTICAL vector (session-stable)",
                x1.x == x2.x && x1.y == x2.y && x1.z == x2.z);
        const glm::vec3 zNearX = glm::normalize(glm::vec3(0.999f, 0.02f, 0.04f));
        const glm::vec3 xf = deterministicX(zNearX);
        std::printf("[snap] deterministicX(near +X): |dot(z,gX)|=%.4f -> x=(%.4f, %.4f, %.4f), dot=%.2e\n",
                    std::abs(zNearX.x), xf.x, xf.y, xf.z, glm::dot(xf, zNearX));
        S.check("DETX-FALLBACK     z near globalX (|dot|>0.9) -> Y-projection: x.y>0.99, perpendicular, unit",
                xf.y > 0.99f && std::abs(glm::dot(xf, zNearX)) < 1e-6f
                && std::abs(glm::length(xf) - 1.0f) < 1e-6f);
    }

    // ================= (6) flipZ + rotateX90 =================
    {
        const SnapCandidate& c0 = *cen;                        // z=(0,0,1), x=(1,0,0)
        const SnapCandidate cf = flipZ(c0);
        S.check("FLIPZ             Z -> -Z with X kept; result orthonormal",
                near3(cf.z, { 0,0,-1 }, 1e-6f) && near3(cf.x, { 1,0,0 }, 1e-6f)
                && std::abs(glm::dot(cf.x, cf.z)) < 1e-6f && std::abs(glm::length(cf.x) - 1.0f) < 1e-6f);
        SnapCandidate cr = c0;
        bool orthoEach = true;
        cr = rotateX90(cr);
        const bool quarter = near3(cr.x, { 0,1,0 }, 1e-6f);    // +90 deg: (1,0,0) -> (0,1,0)
        for (int i = 0; i < 3; ++i) {
            cr = rotateX90(cr);
            orthoEach = orthoEach && std::abs(glm::dot(cr.x, cr.z)) < 1e-6f
                        && std::abs(glm::length(cr.x) - 1.0f) < 1e-6f;
        }
        std::printf("[snap] rotateX90 x4 -> x=(%.7f, %.7f, %.7f) (want (1,0,0) back)\n", cr.x.x, cr.x.y, cr.x.z);
        S.check("ROTX90-QUARTER    one click rotates X +90 deg about Z ((1,0,0) -> (0,1,0))", quarter);
        S.check("ROTX90-CYCLE      four clicks return the ORIGINAL X within 1e-6; orthonormal after each",
                orthoEach && near3(cr.x, c0.x, 1e-6f) && near3(cr.z, c0.z, 1e-6f));
    }

    // ================= (7) ROTATED + TRANSLATED entity: exact world transforms =================
    // rotX90 about +X maps (x,y,z) -> (x,-z,y); then translate (1,2,3). Hand-computed targets.
    TransformComponent xfE;
    xfE.translation = { 1, 2, 3 };
    xfE.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1, 0, 0));
    entt::entity E = mkBody(reg, xfE, "E");
    {
        const auto fE = candidatesForFace(reg, E, 0);
        const SnapCandidate* cw = findAt(fE, SnapKind::FaceCentroid, { 1.5f, 2, 3.5f });
        const SnapCandidate* vw = findAt(fE, SnapKind::BoundaryVertex, { 2, 2, 4 });   // (1,1,0)
        const SnapCandidate* mw = findAt(fE, SnapKind::EdgeMidpoint, { 1.5f, 2, 3 });  // (0.5,0,0)
        if (cw) std::printf("[snap] xf centroid at (%.5f, %.5f, %.5f) z=(%.5f, %.5f, %.5f) "
                            "(want (1.5,2,3.5) / (0,-1,0))\n",
                            cw->pos.x, cw->pos.y, cw->pos.z, cw->z.x, cw->z.y, cw->z.z);
        S.check("XF-CENTROID       centroid transforms EXACTLY: (0.5,0.5,0) -> (1.5,2,3.5)", cw != nullptr);
        S.check("XF-FRAME          +Z rotates with the body: (0,0,1) -> (0,-1,0); X re-derived = (1,0,0)",
                cw && near3(cw->z, { 0,-1,0 }, 1e-5f) && near3(cw->x, { 1,0,0 }, 1e-5f));
        S.check("XF-VERTEX+MID     corner (1,1,0) -> (2,2,4); midpoint (0.5,0,0) -> (1.5,2,3)",
                vw != nullptr && mw != nullptr);
        const auto fCyl = candidatesForFace(reg, E, 1);
        const SnapCandidate* a2 = findAt(fCyl, SnapKind::AxisPoint, { 1, 0, 3 });      // end1 (0,0,2)
        S.check("XF-CYL-AXIS       cylinder rim end1 (0,0,2) -> (1,0,3); axis (0,0,1) -> (0,-1,0)",
                fCyl.size() == 3 && a2 && a2->index == 2 && near3(a2->z, { 0,-1,0 }, 1e-5f));
    }

    // ================= (8) NEG-CTRLs =================
    S.check("NEG-CTRL          bad faceId (99 / -1) -> EMPTY, no crash",
            candidatesForFace(reg, A, 99).empty() && candidatesForFace(reg, A, -1).empty());
    S.check("NEG-CTRL          bad edgeId (99) -> EMPTY, no crash",
            candidatesForEdge(reg, C, 99).empty());
    S.check("NEG-CTRL          bad vertexId (99) -> INVALID candidate (entity == null), no crash",
            candidateForVertex(reg, D, 99, -1).entity == entt::null);
    {
        entt::entity bare = reg.create();
        reg.emplace<TransformComponent>(bare, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        S.check("NEG-CTRL          entity WITHOUT B-Rep components -> empty/invalid across all three",
                candidatesForFace(reg, bare, 0).empty()
                && candidatesForEdge(reg, bare, 0).empty()
                && candidateForVertex(reg, bare, 0, -1).entity == entt::null);
    }

    const bool pass = (S.pass == S.total);
    std::printf("[snap] %d/%d checks\n", S.pass, S.total);
    std::printf("[snap] %s\n", pass
        ? "ALL PASS (Onshape face/edge/vertex inference exact; +Z out of material; cylinder axis "
          "triplet; deterministic X + flip/rotate corrections; world transforms exact; NEG-CTRLs hold)"
        : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::snap
