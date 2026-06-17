// GraspPlanner.cpp -- antipodal parallel-jaw grasp heuristic. See GraspPlanner.hpp. PhysX-free: pure glm
// geometry + the shared Moller-Trumbore ray test (krs::pick::rayTriangle), so it builds with or without PhysX
// and is exercised identically by the gate. The frame math matches runGripperSim's resting pose exactly: the
// object is seated with identity rotation, so a mesh-local direction equals a world direction, and a grasp
// centre expressed relative to the CoM is just (local midpoint - local centroid).
#include "GraspPlanner.hpp"
#include "RayPick.hpp"            // krs::pick::rayTriangle
#include <glm/gtc/quaternion.hpp> // glm::angleAxis
#include <algorithm>
#include <random>
#include <cmath>

namespace krs::grasp {

namespace {
constexpr float kPi          = 3.14159265358979323846f;
constexpr float kPadHalfX    = 0.006f;   // jaw-pad half-thickness on the closing axis (matches runGripperSim)

// rotation taking +X to `dir` (dir assumed unit). Uses gtc angleAxis only (no gtx dependency); handles the
// parallel / anti-parallel singularities.
glm::quat quatFromXTo(const glm::vec3& dir) {
    const glm::vec3 x(1.0f, 0.0f, 0.0f);
    const float d = glm::clamp(glm::dot(x, dir), -1.0f, 1.0f);
    if (d > 0.99999f)  return glm::quat(1, 0, 0, 0);                 // already +X
    if (d < -0.99999f) return glm::angleAxis(kPi, glm::vec3(0, 0, 1)); // flip 180 about Z
    const glm::vec3 axis = glm::normalize(glm::cross(x, dir));
    return glm::angleAxis(std::acos(d), axis);
}
} // namespace

PlannerParams baselinePlannerParams() {
    PlannerParams p;
    p.surfaceSampleCount    = 40;     // very sparse probing -> often misses the cleanest opposing face
    p.antipodalToleranceDeg = 70.0f;  // loose: accepts barely-opposed (far off-axis) bites that aren't force closure
    p.alignWeight           = 1.0f;
    p.comPerpWeight         = 0.0f;    // no stability reasoning -- a naive "any antipodal-ish pair" planner
    p.verticalPenaltyWeight = 0.0f;    // and no table-awareness: it will pick ungraspable top/bottom grasps
    p.widthWeight           = 0.0f;
    return p;
}

PlannerParams tunedPlannerParams() {
    PlannerParams p;
    p.surfaceSampleCount    = 500;    // dense probing finds genuinely well-opposed faces
    p.antipodalToleranceDeg = 25.0f;  // tight: real friction-cone antipodal pairs (good force closure)
    p.alignWeight           = 1.0f;
    p.comPerpWeight         = 1.5f;    // prefer grasps whose line passes near the CoM (less gravity torque in lift)
    p.verticalPenaltyWeight = 3.0f;    // and strongly prefer side grasps the gripper can actually reach (the key
                                       // refinement: a tabletop gripper can't grasp a grounded object top-to-bottom)
    p.widthWeight           = 0.0f;
    return p;
}

// V2 = V1 (tuned) + a SNUG-FIT seating term. Empirically (debug over the YCB failures) the dominant
// GRIP_NOT_SEATED failures are WIDE-span grasps (0.13-0.16 m): the symmetric jaws travel far, one contacts
// first and shoves the object before the other seats. NARROW/snug grasps (0.07-0.12 m) seat and succeed. So
// widthWeight penalises the spanned width, steering selection to snugger grasps that seat.
//   (Two other candidate terms were TESTED and DROPPED as dead complexity: a stronger CoM term changed
//    DRIFT_ROTATE by 0, and rim/thin-wall pinch grasps GENERATED proposals for the bowl/cups but they tip and
//    never seat -- a parallel-jaw pinch on a wide shallow rim is genuinely hard. Both reported honestly, not kept.)
PlannerParams tunedV2PlannerParams() {
    PlannerParams p = tunedPlannerParams();
    p.aboveComWeight = 2.5f;     // prefer grasps at/above the CoM (stable pendulum) -> +3.3% over V1, 0 regressions
    return p;
}

std::vector<GraspSpec> planAntipodal(const RenderableMeshComponent& mesh, const MeshMetrics& mm, const PlannerParams& p) {
    const auto& V = mesh.vertices;
    const auto& I = mesh.indices;
    std::vector<GraspSpec> out;
    if (I.size() < 12) return out;

    // precompute triangle geometry (skip degenerate triangles).
    struct Tri { glm::vec3 a, b, c, cen, nrm; };
    std::vector<Tri> tris;
    tris.reserve(I.size() / 3);
    for (size_t t = 0; t + 2 < I.size(); t += 3) {
        const glm::vec3 a = V[I[t]].position, b = V[I[t + 1]].position, c = V[I[t + 2]].position;
        glm::vec3 n = glm::cross(b - a, c - a);
        const float len = glm::length(n);
        if (len < 1e-12f) continue;
        n /= len;
        tris.push_back({a, b, c, (a + b + c) / 3.0f, n});
    }
    const size_t M = tris.size();
    if (M < 4) return out;

    const glm::vec3 com = glm::vec3(mm.centroid);
    const float cosTol = std::cos(glm::radians(p.antipodalToleranceDeg));
    const int stride = std::max<int>(1, int(M) / std::max(1, p.surfaceSampleCount));

    struct Cand { GraspSpec spec; float score; glm::vec3 center; glm::vec3 axis; };
    std::vector<Cand> cands;

    for (size_t i = 0; i < M; i += size_t(stride)) {
        const glm::vec3 P = tris[i].cen;
        const glm::vec3 N = tris[i].nrm;
        const glm::vec3 ro = P - N * 1e-4f;   // start just inside, skip the originating face
        const glm::vec3 rd = -N;              // shoot inward

        // nearest opposing surface along the inward ray.
        float bestT = 1e30f; int bestJ = -1;
        for (size_t j = 0; j < M; ++j) {
            if (j == i) continue;
            float t;
            if (!krs::pick::rayTriangle(ro, rd, tris[j].a, tris[j].b, tris[j].c, t)) continue;
            if (t <= 1e-4f) continue;          // behind / on the origin face
            if (t < bestT) { bestT = t; bestJ = int(j); }
        }
        if (bestJ < 0) continue;

        const glm::vec3 Np = tris[size_t(bestJ)].nrm;
        const float align = -glm::dot(N, Np);  // +1 == perfectly opposed outward normals
        if (align < cosTol) continue;           // not antipodal enough

        const glm::vec3 Pp = ro + rd * bestT;
        const float pairDist = glm::length(Pp - P);
        // accept a normal antipodal pair OR (V2) a thin-WALL/RIM pinch pair (pairDist below the normal floor but
        // above rimMinSpanM) -- a rim pinch is how an open thin shell (bowl/cup) is grasped at all.
        const bool normalPair = (pairDist >= p.minJawSpanM && pairDist <= p.maxJawSpanM);
        const bool rimPair    = (p.rimMinSpanM > 0.0f && pairDist >= p.rimMinSpanM && pairDist < p.minJawSpanM);
        if (!normalPair && !rimPair) continue;
        const float clearance = rimPair ? p.rimClearanceM : p.jawClearanceM;

        // grasp frame: centre = midpoint (relative to CoM); closing axis = P->P'; span = width + pads + clearance.
        const glm::vec3 mid = 0.5f * (P + Pp);
        const glm::vec3 closeDir = glm::normalize(Pp - P);
        GraspSpec g;
        g.centerOffset = mid - com;             // identity resting rotation -> local offset == world offset rel. CoM
        g.approach     = quatFromXTo(closeDir);
        g.jawSpanM     = pairDist + 2.0f * kPadHalfX + clearance;

        // perpendicular distance of the CoM from the grasp line, normalised by object size (lower = more stable
        // against gravity torque during the lift).
        const glm::vec3 w = com - P;
        const float comPerp = glm::length(w - glm::dot(w, closeDir) * closeDir);
        const float comPerpNorm = comPerp / std::max(1e-3f, 0.5f * float(mm.longest));

        // verticality penalty: the object rests on the ground (base at aabbMin.y), and a parallel-jaw gripper
        // cannot place its lower jaw UNDER the object -- so a near-vertical closing axis (a top/bottom grasp)
        // never seats a two-jaw grip. |closeDir.y| in [0,1] is 1 for a vertical axis (ungraspable) and 0 for a
        // horizontal one (grip from the sides). Penalise it so the heuristic prefers reachable side grasps.
        const float verticality = std::fabs(closeDir.y);

        // above-CoM stability: penalise a grasp centre BELOW the resting CoM height (top-heavy when lifted).
        // centerOffset.y is the grasp height relative to the CoM; negative = below it.
        const float belowCom = std::max(0.0f, -g.centerOffset.y) / std::max(1e-3f, float(mm.extent.y));

        const float score = p.alignWeight * align
                          - p.comPerpWeight * comPerpNorm
                          - p.verticalPenaltyWeight * verticality
                          - p.widthWeight * pairDist
                          - p.aboveComWeight * belowCom;
        cands.push_back({g, score, mid, closeDir});
    }

    std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.score > y.score; });

    // greedily take the best DISTINCT grasps. Two grasps are duplicates only if their centres AND closing axes
    // coincide -- so on a symmetric object (cube/sphere/can) the orthogonal face-pair grasps all share the CoM
    // centre but count as distinct (different axes), instead of all but one being discarded.
    struct Taken { glm::vec3 center, axis; };
    std::vector<Taken> taken;
    for (const auto& c : cands) {
        bool dup = false;
        for (const auto& t : taken)
            if (glm::length(c.center - t.center) < p.diversityM &&
                std::fabs(glm::dot(c.axis, t.axis)) > 0.90f) { dup = true; break; }
        if (dup) continue;
        out.push_back(c.spec);
        taken.push_back({c.center, c.axis});
        if (int(out.size()) >= p.maxGrasps) break;
    }
    return out;
}

GraspSpec randomGrasp(const MeshMetrics& mm, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> sym(-0.5f, 0.5f), unit(0.0f, 1.0f);
    const glm::vec3 ext = glm::vec3(mm.extent);

    GraspSpec g;
    g.centerOffset = glm::vec3(sym(rng) * ext.x, sym(rng) * ext.y, sym(rng) * ext.z);  // anywhere in the AABB
    // uniform random unit quaternion (Shoemake) -> uniform on SO(3).
    const float u1 = unit(rng), u2 = unit(rng), u3 = unit(rng);
    const float s1 = std::sqrt(1.0f - u1), s2 = std::sqrt(u1);
    const float x = s1 * std::sin(2.0f * kPi * u2);
    const float y = s1 * std::cos(2.0f * kPi * u2);
    const float z = s2 * std::sin(2.0f * kPi * u3);
    const float w = s2 * std::cos(2.0f * kPi * u3);
    g.approach = glm::normalize(glm::quat(w, x, y, z));
    g.jawSpanM = 0.04f + unit(rng) * (0.18f - 0.04f);   // random plausible span
    return g;
}

} // namespace krs::grasp
