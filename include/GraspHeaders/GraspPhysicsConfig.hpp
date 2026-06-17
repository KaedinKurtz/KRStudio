#pragma once
// GraspPhysicsConfig.hpp -- the LOCKED, physically-justified physics + success criterion for the grasp
// pipeline. THIS IS THE RULER. Tuning improves the PLANNER, never these numbers. Every value is a constexpr
// member of a single struct; lockedConfigHash() fingerprints the whole struct and is PRINTED by every grasp
// gate, so a silent edit to any locked parameter is visible in the logs. assertPhysicsLocked() (in GraspSim)
// re-reads the LIVE PhysX scene/material/force and fails the gate on any mismatch. Softening any of these to
// raise the success rate is a HARD FAILURE, not a path to green.
#include <cstdint>
#include <cstring>

namespace krs::grasp {

struct GraspPhysicsConfig {
    // --- world / contact (justified in ROADMAP §GR) ---
    float gravity      = 9.81f;        // m/s^2, downward (matches the engine's validated bench suite)
    float frictionMu   = 0.70f;        // jaw<->object static == dynamic; rubber gripper pad on rigid plastic (~0.6-0.9)
    float gripForceN   = 40.0f;        // constant squeeze per jaw after contact; finite + bounded (real PJ: 20-140 N)
    float densityKgM3  = 1000.0f;      // x the cooked/decomposed collider volume -> object mass
    // --- the lift/hold trajectory ---
    float liftHeightM  = 0.15f;        // required lift (standard pick clearance, >> object size)
    float liftSpeedMps = 0.25f;        // lift velocity cap (real inertial load, not a stress test)
    float holdSeconds  = 2.0f;         // settled hold at apex (creep/slip surfaces over 2 s)
    // --- the success test ---
    float successDistM = 0.030f;       // object center within this of (start + liftHeight up) at end of hold
    float contactFrac  = 0.90f;        // require continuous jaw<->object contact >= this fraction of lift+hold steps
    float maxGripForceFactor = 3.0f;   // a success must be a BOUNDED friction grip: peak jaw<->object contact force
                                       // <= this * gripForceN. The lift jaw is KINEMATIC and can otherwise impose
                                       // UNBOUNDED reaction (a wedged object reads 1000-17000 N -> an unphysical
                                       // rigid clamp that carries ANY object regardless of friction). Capping at
                                       // 3x (120 N) admits real grips + dynamic transients (good grasp peaks ~41 N)
                                       // and REJECTS the clamp. This is a TIGHTENING of the criterion (Phase 3):
                                       // it removes phantom-clamp 'successes', it never makes a grasp pass.
    // --- determinism ---
    float fixedDt      = 1.0f / 240.0f;// fixed PhysX step (engine standard; bit-identical re-runs)
};

// All-float, no padding -> the byte hash covers every locked value with no indeterminate padding bytes.
static_assert(sizeof(GraspPhysicsConfig) == 11 * sizeof(float), "GraspPhysicsConfig must stay packed floats so lockedConfigHash() is reproducible");

// The single source of truth. constexpr -> baked at compile time; an edit changes lockedConfigHash().
inline constexpr GraspPhysicsConfig kLockedPhysics{};

// FNV-1a over the struct's byte image -> a 64-bit fingerprint printed by every gate. If ANY locked value is
// edited, this number changes and a reviewer sees the drift instantly in the gate logs.
inline std::uint64_t lockedConfigHash() {
    unsigned char bytes[sizeof(GraspPhysicsConfig)];
    std::memcpy(bytes, &kLockedPhysics, sizeof(GraspPhysicsConfig));
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char b : bytes) { h ^= b; h *= 1099511628211ull; }
    return h;
}

} // namespace krs::grasp
