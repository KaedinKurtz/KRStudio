#pragma once
// ===========================================================================
// krs::constraint -- the CORE of a Fusion-360-style ASSEMBLY-CONSTRAINT suite.
//
// A Constraint relates two ANCHORS. An Anchor is a durable, body-LOCAL oriented
// frame (origin + primary axis Z + reference axis X), authored from a viewport
// pick (krs::sel::Selection) and re-anchorable by the geometry-invariant
// faceKey/edgeKey -- the SAME body-local durable-anchor architecture as
// MateConnector (components.hpp): NEVER a stored world frame, NEVER a live
// index that re-import/re-tessellation can silently rebind. The world frame is
// RE-DERIVED through the body's CURRENT TransformComponent on every use.
//
// applyConstraintSnap() is the interactive "assemble" action: body B is moved
// RIGIDLY (translation + rotation only, never scale) so the constraint's
// geometric relation holds with body A fixed -- the CAD mate convention (the
// first-picked side stays put). Snap math mirrors the proven robot-builder
// mate path: krs::rbuild::RobotGraph::mateTransformConcentric (RobotBuilder.hpp)
// / krs::robot::snapMateSubtree (RobotInstance.cpp) -- shortest-arc axis
// alignment + mate-point coincidence.
//
// REFUSALS (return false, ZERO motion): suppressed constraint; either anchor
// unresolvable (dead entity, stale key not re-findable, out-of-range hint);
// b.body is a robot member (RobotSubcomponentComponent / RobotRootComponent --
// robots are kinematically owned, the chain is the single writer of their
// motion); a==b same body; b.body has no TransformComponent to move.
//
// DOF metadata (lockedDof): which RELATIVE degrees of freedom each type
// REMOVES, expressed in the A-anchor frame with Z = primary axis. The physics
// layer maps these onto PxD6 axes later; this table is the contract.
//
//   type          locked                        open (for physics)
//   ------------  ----------------------------  -----------------------------
//   Coincident    tx ty tz                      rx ry rz   (point form)
//   Concentric    tx ty rx ry                   tz rz
//   Parallel      rx ry                         tx ty tz rz
//   Perpendicular ry                            (one rotation, by convention)
//   Tangent       tz                            (one translation along the
//                                                contact normal; exact when A
//                                                is the plane side)
//   Flush         tz rx ry                      tx ty rz
//   Distance      tz rx ry                      tx ty rz   (Flush + offset)
//   Angle         ry                            (one rotation, by convention)
//   Rigid         tx ty tz rx ry rz             --
//   Revolute      tx ty tz rx ry                rz
//   Slider        tx ty rx ry rz                tz
//   Cylindrical   tx ty rx ry                   tz rz
//   Ball          tx ty tz                      rx ry rz
//   PinSlot       tx ty rx ry                   tz rz  [SLOT APPROXIMATION:
//                 the true pair frees a slide along the SLOT direction (the
//                 A-frame X), not along the pin axis; a single axis-aligned
//                 D6 table cannot express both, so the slot slide is folded
//                 onto tz. Document + revisit when the physics layer grows a
//                 per-type frame remap.]
//
//   Honest-partial notes: Coincident reports the POINT form (tx,ty,tz); the
//   plane point-on-plane variant really only removes tz. Perpendicular/Angle
//   each remove exactly ONE rotational DOF whose axis depends on the current
//   configuration; it is reported on ry by convention. Tangent's removed
//   translation is along the contact normal (tz of a plane-A anchor); for
//   cylinder-cylinder tangency it is the radial direction -- still one
//   translational DOF.
//
// SERIALIZATION: toJson/fromJson round-trip every field. Entities are written
// as their raw entt uint32 -- LOAD-TIME ENTITY REMAP IS THE ksave LAYER'S JOB
// (this module has no knowledge of the save file's entity table). 64-bit
// face/edge keys are written as 16-digit hex STRINGS (QJson numbers are
// doubles; a 64-bit hash would silently lose bits).
//
// Gate: runConstraintGate() (env hook KRS_CONSTRAINT_SELFTEST, wired by the
// main session) -- headless, synthetic entities in a Scene registry, every
// CType snapped + analytically verified, NEG-CTRLs, JSON round-trip, chained
// revolute linkage. Mirrors src/Physics/Measure.cpp's gate style.
// ===========================================================================
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>
#include <QJsonObject>
#include <cstdint>
#include <string>
#include <vector>

#include "components.hpp"        // TransformComponent, BRepFace(Component), Robot* tags, computeFaceKey
#include "BRepEdge.hpp"          // BRepEdge(Component), computeEdgeKey (edge anchors)
#include "SelectionService.hpp"  // krs::sel::Selection -- the pick currency anchors are authored from

namespace krs::constraint {

// The constraint vocabulary. First eight are GEOMETRIC relations; the last six
// are KINEMATIC types that also carry a DOF meaning for the physics layer.
enum class CType {
    Coincident, Concentric, Parallel, Perpendicular, Tangent, Flush,
    Distance, Angle, Rigid, Revolute, Slider, Cylindrical, Ball, PinSlot
};
constexpr int kCTypeCount = 14;

// What geometry the anchor was AUTHORED from (drives per-type snap math:
// plane-vs-cylinder tangency, point-vs-plane coincidence, ...).
enum class AnchorSource {
    PlaneFace = 0, CylFace, CircleEdge, LineEdge, Vertex, SphereFace
};

// A durable, body-LOCAL oriented frame rigidly attached to a body's geometry
// (the MateConnector architecture). key = BRepFace.faceKey for face anchors /
// BRepEdge.edgeKey for edge anchors (0 = pure datum / synthetic anchor);
// faceId/edgeId are FAST-PATH HINTS, validated against key and re-found by
// key when stale (re-import / array reorder). The world frame derives through
// the CURRENT entity transform -- it moves with the body for free.
struct Anchor {
    entt::entity  body   = entt::null;
    std::uint64_t key    = 0;                  // faceKey / edgeKey (durable re-anchor id)
    int           faceId = -1, edgeId = -1;    // hints (never trusted over key)
    glm::vec3     pos{ 0.0f };                 // frame origin, body-LOCAL
    glm::vec3     axisZ{ 0.0f, 0.0f, 1.0f };   // primary axis (joint axis / plane normal), body-LOCAL unit
    glm::vec3     axisX{ 1.0f, 0.0f, 0.0f };   // reference dir (roll about Z), body-LOCAL unit
    float         radius = 0.0f;               // bore/circle radius (tangency + compatibility)
    AnchorSource  from = AnchorSource::PlaneFace;
};

// Build an Anchor from a committed viewport pick: converts the Selection's
// WORLD-space analytic params to body-local through the inverse entity
// transform. Cylinder/cone anchors take the rim NEAREST the click as origin
// (the face the operator pointed at -- mirrors RobotBuilderPanel faceFrame);
// planes anchor at the analytic on-plane point; edges at centre/midpoint;
// vertices at the pick point. Selection.faceKey carries the edgeKey for edge
// picks (SelectionService contract), so `key` is filled either way.
Anchor makeAnchor(entt::registry& reg, const krs::sel::Selection& sel);

// The re-derived WORLD frame of an anchor at the body's CURRENT transform.
// valid==false when the body died, or a keyed anchor's key can no longer be
// found in the body's BRepFaceComponent/BRepEdgeComponent (stale key), or an
// un-keyed hint is out of range. A stale faceId/edgeId with a LIVE key is NOT
// an error -- the key re-find keeps the anchor working (topological-naming
// mitigation).
struct AnchorWorldFrame {
    bool      valid = false;
    glm::vec3 pos{ 0.0f };               // frame origin (world)
    glm::vec3 z{ 0.0f, 0.0f, 1.0f };     // primary axis (world unit)
    glm::vec3 x{ 1.0f, 0.0f, 0.0f };     // reference axis (world unit, perpendicular to z)
    float     radius = 0.0f;
};
AnchorWorldFrame anchorWorldFrame(entt::registry& reg, const Anchor& a);

// One assembly constraint between two anchors. `a` is the FIXED side, `b` the
// side applyConstraintSnap moves (CAD pick-order convention). offset applies
// to Distance (plane gap along a's normal, signed) and is reserved for axial
// offsets; angleDeg is the Angle target. driven=false on kinematic types means
// the open DOF are left to physics (the PxD6 layer); true means a controller
// owns them -- data only, the snap ignores it.
struct Constraint {
    std::uint64_t id = 0;
    CType  type = CType::Coincident;
    Anchor a, b;
    double offset   = 0.0;
    double angleDeg = 0.0;
    bool   suppressed = false;
    bool   driven     = false;
};

// Scene-wide constraint list, ctx singleton (mirror worldState() / MateGraph).
struct ConstraintGraphComponent {
    std::vector<Constraint> constraints;
    std::uint64_t nextId = 1;            // monotonic, never reused (0 = unassigned)
    bool showIcons = true;               // viewport icon layer toggle (UI reads, module ignores)
};
ConstraintGraphComponent& constraintGraph(entt::registry& reg);

// SNAP: rigidly move body B (translation + rotation only, never scale) so the
// relation holds with body A fixed. Returns false with ZERO motion on any
// refusal (see header banner). Per-type semantics:
//   Concentric   axes colinear (minimal rotation -- flips sense only when that
//                is the smaller arc), then translate the PERPENDICULAR offset
//                off the axis; the along-axis offset is preserved (open DOF).
//   Coincident   plane A: B's point moves onto the plane along the normal;
//                point anchors: exact coincide.
//   Parallel     minimal rotation about B's own anchor point; smallest-arc
//                sign (an obtuse pair ends anti-parallel).
//   Perpendicular minimal rotation until dot(axes) == 0.
//   Tangent      plane/cylinder: translate along the plane normal until the
//                axis-to-plane distance (measured at B's anchor point) equals
//                the radius, same side preserved; cylinder/cylinder: translate
//                along the common perpendicular to EXTERNAL tangency
//                (axis distance = rA + rB). Two plane anchors: refused.
//   Flush        plane normals ANTI-parallel + coplanar (the mating-faces
//                relation).
//   Distance     Flush with a signed gap of `offset` along A's normal (plane
//                anchors); point anchors: B's point at pA + offset * zA.
//   Angle        rotate about the common perpendicular until the axis angle
//                equals angleDeg.
//   Rigid        full frame lock: B's anchor frame maps onto A's.
//   Revolute     concentric + anchor planes coincident (mate points coincide;
//                mirrors mateTransformConcentric exactly); roll stays free.
//   Slider       axes colinear + roll locked (full basis align), along-axis
//                offset preserved.
//   Cylindrical  positions like Concentric.
//   Ball         anchor points coincide, orientation untouched.
//   PinSlot      concentric PROJECTED: axes parallel (minimal), B's anchor
//                point moved into A's slot plane (span of A's Z and X through
//                A's origin); the in-slot offset along A's X is preserved.
bool applyConstraintSnap(entt::registry& reg, Constraint& c);

// Which relative DOF the type REMOVES, in the A-anchor frame (Z = primary
// axis). true = locked. See the table + honesty notes in the header banner.
struct DofSpec { bool tx = false, ty = false, tz = false, rx = false, ry = false, rz = false; };
DofSpec lockedDof(CType t);

// Human strings.
const char* cTypeName(CType t);                 // "Concentric", ...
std::string describe(const Constraint& c);      // one-line UI list entry

// JSON (see banner: entities as raw uint32 -- ksave remaps on load; 64-bit
// keys as hex strings). fromJson returns false on malformed/unknown input and
// leaves the output untouched on failure.
QJsonObject toJson(const Constraint& c);
bool fromJson(const QJsonObject& o, Constraint& out);
QJsonObject toJson(const ConstraintGraphComponent& g);
bool fromJson(const QJsonObject& o, ConstraintGraphComponent& out);

// Headless self-test: every CType snapped on synthetic B-Rep bodies and
// verified analytically (axis alignment < 1e-5, distances < 1e-6, angles
// exact), suppressed / robot-member / stale-key / dead-entity refusals with
// zero motion, key re-find after face + edge scrambles, full-graph JSON text
// round-trip equality, DOF table audit, and a chained A-B / B-C revolute
// linkage. Prints PASS/FAIL rows + a summary line; returns all-pass.
bool runConstraintGate();

} // namespace krs::constraint
