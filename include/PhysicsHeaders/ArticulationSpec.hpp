#pragma once
// POD articulation spec — the PhysX/Eigen-free boundary type between the GATE-H
// harness (which holds the Eigen oracle) and SimulationController (which builds the
// live PxArticulationReducedCoordinate). Mirrors ArticulationGate's GateSpec, but
// plain so it can cross the SimulationController public header without pulling PhysX
// or Eigen into every TU. Geometry is the validated §M.1a FANUC detected-axis spec.
#include <vector>
#include <array>

namespace krs::dyn {

struct ArticJointSpec {
    int parent = -1;                                   // -1 = attached to the fixed root
    bool revolute = true;                              // false = prismatic
    std::array<float, 3> axis{ {0.f, 0.f, 1.f} };       // joint axis in the joint frame
    std::array<float, 9> Rtree{ {1,0,0, 0,1,0, 0,0,1} };// row-major 3x3 parent->joint rotation
    std::array<float, 3> ptree{ {0.f, 0.f, 0.f} };      // parent->joint translation (m)
    float mass = 1.0f;
    std::array<float, 3> com{ {0.f, 0.f, 0.f} };
    std::array<float, 3> inertiaDiag{ {0.1f, 0.1f, 0.1f} };
    // Engineering limits -- carried so they reach krs::robot::Joint and clampDof() is the
    // FINAL authoritative clamp on commanded angles (no command may exceed the bound).
    // Defaults are wide (+/-pi); canonicalSpec() overrides with real per-axis bounds.
    float qLower = -3.14159265f, qUpper = 3.14159265f;  // rad (revolute) / m (prismatic)
    float vMax = 2.0f, effortMax = 100.0f;
    bool  frozen = false;                              // declared but not yet a driven DOF (e.g. wrist pending 6-DoF parse)
};

// Optional closed loop (the FANUC parallelogram): pin link `tipLink`'s +tipLocal
// to a fixed world anchor via a 3-translation-locked / rotations-free D6 joint.
struct ArticLoopSpec {
    bool enabled = false;
    int tipLink = -1;                                  // 0-based joint index
    std::array<float, 3> tipLocal{ {0.f, 0.f, 0.f} };
    std::array<float, 3> anchorWorld{ {0.f, 0.f, 0.f} };
};

struct RobotArticSpec {
    std::vector<ArticJointSpec> joints;
    ArticLoopSpec loop;
    bool fixBase = true;
};

} // namespace krs::dyn
