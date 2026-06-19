#pragma once
// ===========================================================================
// SELF-COLLISION MATRIX GENERATION (krs::plan) -- MoveIt's one genuinely-computed
// setup step. Sample the robot's configuration space (respecting joint limits);
// for each non-adjacent link-capsule pair, observe how often it self-collides and
// CLASSIFY it:
//   ADJACENT   (shares a joint)              -> DISABLE (legitimate contact at the joint)
//   NEVER      (0 colliding samples)         -> DISABLE (geometrically cannot reach)
//   ALWAYS     (every sample collides)       -> DISABLE (permanently in contact)
//   SOMETIMES  (some collide, some don't)    -> KEEP    (the REAL self-collision risks)
// A pair colliding at the DEFAULT pose but separable elsewhere is SOMETIMES -> KEPT:
// the SAFETY rule is we NEVER disable a pair that genuinely collides in reachable
// configs (the dangerous error). Disabled pairs populate CollisionWorld::disabled
// SelfPairs so the planner skips them yet still checks the kept real-risk pairs.
//
// DENSITY-MONOTONE by construction: density-N samples are the first N of a fixed
// seeded sequence, so a higher density's sample set is a SUPERSET of a lower one ->
// any pair KEPT (both states observed) at low density stays kept at high density:
// more sampling never disables a real collision.
// ===========================================================================
#include <Eigen/Dense>
#include <vector>
#include <set>
#include <utility>
#include <random>
#include <cstdint>
#include "RobotDynamics.hpp"
#include "PlanningWorld.hpp"

namespace krs::plan {

enum class PairClass { Adjacent = 0, Never = 1, Always = 2, Sometimes = 3, DefaultSeparable = 4 };

inline const char* pairClassName(PairClass c) {
    switch (c) {
        case PairClass::Adjacent:         return "ADJACENT";
        case PairClass::Never:            return "NEVER";
        case PairClass::Always:           return "ALWAYS";
        case PairClass::Sometimes:        return "SOMETIMES";
        case PairClass::DefaultSeparable: return "DEFAULT(separable)";
    }
    return "?";
}

struct SelfCollisionMatrix {
    int nCaps = 0;
    int density = 0;
    std::vector<std::vector<char>>      disabled;   // nCaps x nCaps, symmetric (1 = skip in collision checking)
    std::vector<std::vector<PairClass>> cls;        // classification per pair
    std::vector<std::vector<int>>       collideCount; // # colliding samples per pair (diagnostic)

    bool isDisabled(int i, int j) const { return disabled[i][j] != 0; }
    PairClass classOf(int i, int j) const { return cls[i][j]; }

    // capsule-index pairs to skip -> feeds CollisionWorld::disabledSelfPairs.
    std::set<std::pair<int, int>> disabledPairSet() const {
        std::set<std::pair<int, int>> s;
        for (int i = 0; i < nCaps; ++i)
            for (int j = i + 1; j < nCaps; ++j)
                if (disabled[i][j]) s.insert({ i, j });
        return s;
    }

    // the KEPT (checked) non-adjacent real-risk pairs.
    std::set<std::pair<int, int>> keptPairSet() const {
        std::set<std::pair<int, int>> s;
        for (int i = 0; i < nCaps; ++i)
            for (int j = i + 1; j < nCaps; ++j)
                if (!disabled[i][j]) s.insert({ i, j });
        return s;
    }

    // MANUAL OVERRIDE: force a pair on (disable=true) or off (disable=false) after generation.
    void override_(int i, int j, bool disable) {
        disabled[i][j] = disabled[j][i] = disable ? 1 : 0;
    }
};

// Generate the self-collision disable matrix. density-N uses the first N states of a
// fixed seeded sequence (monotone). Self-collision is link-capsule vs link-capsule
// only (obstacles ignored -- this is about the robot alone), respecting joint limits.
inline SelfCollisionMatrix generateSelfCollisionMatrix(
    const krs::dyn::SerialChain& chain, const CollisionWorld& world,
    const JointLimits& lim, int density, std::uint32_t seed = 1u)
{
    const int nc = int(world.capsules.size());
    SelfCollisionMatrix m;
    m.nCaps = nc; m.density = density;
    m.disabled.assign(nc, std::vector<char>(nc, 0));
    m.cls.assign(nc, std::vector<PairClass>(nc, PairClass::Never));
    m.collideCount.assign(nc, std::vector<int>(nc, 0));

    auto collidePair = [&](const std::vector<krs::dyn::Pose>& poses, int i, int j) -> bool {
        Eigen::Vector3d Ai, Bi, Aj, Bj;
        world.worldCapsule(poses, i, Ai, Bi);
        world.worldCapsule(poses, j, Aj, Bj);
        const double clear = segSegDist(Ai, Bi, Aj, Bj)
                             - world.capsules[i].radius - world.capsules[j].radius;
        return clear < 0.0;
    };

    // default-pose (q=0) collision, per pair.
    std::vector<std::vector<char>> defCollide(nc, std::vector<char>(nc, 0));
    {
        std::vector<krs::dyn::Pose> poses;
        chain.fk(Eigen::VectorXd::Zero(chain.nq()), poses);
        for (int i = 0; i < nc; ++i)
            for (int j = i + 1; j < nc; ++j)
                defCollide[i][j] = collidePair(poses, i, j) ? 1 : 0;
    }

    // N samples from the fixed seeded sequence (first N -> superset for larger N).
    std::mt19937 rng(seed);
    std::vector<std::uniform_real_distribution<double>> dist;
    dist.reserve(chain.nq());
    for (int d = 0; d < chain.nq(); ++d) dist.emplace_back(lim.qLower[d], lim.qUpper[d]);

    for (int s = 0; s < density; ++s) {
        Eigen::VectorXd q(chain.nq());
        for (int d = 0; d < chain.nq(); ++d) q[d] = dist[d](rng);   // consumes RNG in a fixed order
        std::vector<krs::dyn::Pose> poses; chain.fk(q, poses);
        for (int i = 0; i < nc; ++i)
            for (int j = i + 1; j < nc; ++j)
                if (collidePair(poses, i, j)) ++m.collideCount[i][j];
    }

    for (int i = 0; i < nc; ++i)
        for (int j = i + 1; j < nc; ++j) {
            const bool adj = CollisionWorld::adjacent(chain, world.capsules[i].body, world.capsules[j].body);
            const int c = m.collideCount[i][j];
            PairClass pc; bool dis;
            if (adj)               { pc = PairClass::Adjacent; dis = true;  }   // legitimate joint contact
            else if (c == 0)       { pc = PairClass::Never;    dis = true;  }   // cannot reach
            else if (c == density) { pc = PairClass::Always;   dis = true;  }   // permanently in contact
            else                   { pc = defCollide[i][j] ? PairClass::DefaultSeparable
                                                           : PairClass::Sometimes;
                                     dis = false; }                            // SOMETIMES -> KEEP (real risk)
            m.cls[i][j] = m.cls[j][i] = pc;
            m.disabled[i][j] = m.disabled[j][i] = dis ? 1 : 0;
        }
    return m;
}

// SELFCOLLISION gates (env KRS_SELFCOL_SELFTEST; folded into KRS_OVERNIGHT_BENCH).
bool runSelfCollisionMatrixGate();   // correct classification vs brute-force GT; no dangerous disables; density-monotone
bool runSelfCollisionFeedsPlannerGate();   // validity skips disabled but catches a kept pair's collision

} // namespace krs::plan
