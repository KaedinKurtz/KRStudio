// ClearanceLimits.cpp -- see ClearanceLimits.hpp. Clearance-based joint-limit discovery via a
// per-DOF 1-D self-clearance scan (others held at a seed config).
#include "ClearanceLimits.hpp"

#include <cstdio>
#include <algorithm>
#include <cmath>
#include <random>

namespace krs::climits {

using krs::dyn::SerialChain;
using krs::plan::CollisionWorld;
using krs::plan::JointLimits;

namespace {
// Signed surface clearance at config q: min over all checked pairs (self + obstacle). >= target
// means safe. (CollisionReport.minClearance is exactly this signed distance.)
double clearanceAt(const SerialChain& chain, const CollisionWorld& world, const Eigen::VectorXd& q) {
    return world.query(chain, q).minClearance;
}

// Scan DOF d outward from seed in direction dir (+1/-1), others held at seed, until clearance drops
// below target or we reach `mechEnd`. Returns the furthest SAFE value of q[d] in that direction,
// bisection-refined at the crossing. `q` is a scratch copy already equal to seed.
double scanDir(const SerialChain& chain, const CollisionWorld& world, double clearance,
               Eigen::VectorXd q, int d, double seedVal, double mechEnd, int dir, int samples) {
    const double span = std::abs(mechEnd - seedVal);
    if (span < 1e-9) return seedVal;
    const double step = span / double(std::max(4, samples));
    double lastSafe = seedVal;
    double v = seedVal;
    for (int s = 1; s <= samples; ++s) {
        v = seedVal + dir * step * s;
        // clamp exactly to the mechanical end on the last step
        if ((dir > 0 && v > mechEnd) || (dir < 0 && v < mechEnd)) v = mechEnd;
        q[d] = v;
        if (clearanceAt(chain, world, q) >= clearance) {
            lastSafe = v;
            if (v == mechEnd) break;      // reached mechanical travel, all safe
        } else {
            // crossing between lastSafe and v -> bisect for the precise boundary
            double lo = lastSafe, hi = v;
            for (int it = 0; it < 24; ++it) {
                const double mid = 0.5 * (lo + hi);
                q[d] = mid;
                if (clearanceAt(chain, world, q) >= clearance) lo = mid; else hi = mid;
            }
            return lo;
        }
    }
    return lastSafe;
}
} // namespace

ClearanceLimits computeClearanceInterval(const SerialChain& chain, const CollisionWorld& world,
                                         double clearance, const Eigen::VectorXd& seedIn,
                                         const JointLimits& mech, int samplesPerDof) {
    ClearanceLimits out;
    const int n = chain.nq();
    out.dof = n; out.clearance = clearance;
    if (n <= 0 || int(mech.qLower.size()) < n || int(mech.qUpper.size()) < n) return out;
    Eigen::VectorXd seed = (seedIn.size() == n) ? seedIn : Eigen::VectorXd::Zero(n);
    // clamp the seed into the mechanical box (a seed outside travel is meaningless).
    for (int d = 0; d < n; ++d) seed[d] = std::clamp(seed[d], mech.qLower[d], mech.qUpper[d]);
    out.seed = seed;
    out.qLower = seed; out.qUpper = seed;

    // If the seed itself violates clearance, the safe interval is empty -> collapse to the seed and
    // flag it (the caller shouldn't widen limits into a colliding pose).
    if (clearanceAt(chain, world, seed) < clearance) { out.seedSafe = false; out.ok = true; return out; }

    for (int d = 0; d < n; ++d) {
        out.qUpper[d] = scanDir(chain, world, clearance, seed, d, seed[d], mech.qUpper[d], +1, samplesPerDof);
        out.qLower[d] = scanDir(chain, world, clearance, seed, d, seed[d], mech.qLower[d], -1, samplesPerDof);
    }
    out.ok = true;
    return out;
}

ClearanceLimits computeStaticLimits(const SerialChain& chain, const CollisionWorld& world,
                                    double clearance, const JointLimits& mech, int samplesPerDof) {
    const int n = chain.nq();
    const Eigen::VectorXd home = Eigen::VectorXd::Zero(n);
    ClearanceLimits box = computeClearanceInterval(chain, world, clearance, home, mech, samplesPerDof);
    if (!box.ok || !box.seedSafe || n <= 0) return box;

    // CONSERVATIVE SHRINK: the per-DOF scan holds the OTHER joints at home, so a CORNER of the box
    // (several joints folded together) can still collide. For a static box that is safe for ANY
    // combination within it, verify by sampling and symmetrically shrink the half-ranges toward home
    // until the worst sampled config clears the target. (The dynamic/live mode avoids this cost by
    // re-scanning from the CURRENT pose each tick -- so it keeps the full single-joint range.)
    Eigen::VectorXd lo = box.qLower, hi = box.qUpper;
    std::mt19937 rng(0xC1EA2C0u);
    auto worstClearance = [&](const Eigen::VectorXd& L, const Eigen::VectorXd& H) {
        double worst = 1e30;
        const int probes = 64 + 24 * n;
        // deterministic corner sweep first (the extremes are the usual offenders)...
        const int corners = std::min(1 << n, 256);
        for (int c = 0; c < corners; ++c) {
            Eigen::VectorXd q(n);
            for (int d = 0; d < n; ++d) q[d] = (c & (1 << d)) ? H[d] : L[d];
            worst = std::min(worst, world.query(chain, q).minClearance);
        }
        // ...then random interior samples.
        std::vector<std::uniform_real_distribution<double>> dist;
        for (int d = 0; d < n; ++d) dist.emplace_back(L[d], H[d]);
        for (int s = 0; s < probes; ++s) {
            Eigen::VectorXd q(n);
            for (int d = 0; d < n; ++d) q[d] = dist[d](rng);
            worst = std::min(worst, world.query(chain, q).minClearance);
        }
        return worst;
    };
    for (int it = 0; it < 40; ++it) {
        if (worstClearance(lo, hi) >= clearance) break;
        for (int d = 0; d < n; ++d) {              // shrink each half-range 12% toward home
            hi[d] = home[d] + (hi[d] - home[d]) * 0.88;
            lo[d] = home[d] + (lo[d] - home[d]) * 0.88;
        }
    }
    box.qLower = lo; box.qUpper = hi;
    return box;
}

ClearanceLimits computeDynamicLimits(const SerialChain& chain, const CollisionWorld& world,
                                     double clearance, const Eigen::VectorXd& qCurrent,
                                     const JointLimits& mech, int samplesPerDof) {
    return computeClearanceInterval(chain, world, clearance, qCurrent, mech, samplesPerDof);
}

// ================================================================================================
// GATE CLEARANCE
// ================================================================================================
bool runClearanceGate() {
    using std::printf;
    using namespace krs::dyn;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[clearance] GATE CLEARANCE -- self-intersection -> mechanical joint limits for a surface clearance\n");

    auto eye3 = [] { return Eigen::Matrix3d::Identity(); };
    DynBody body; body.mass = 1.0; body.com.setZero(); body.inertiaCom = eye3();

    // A 3R PLANAR arm (all pitch about Y, links along +x) that folds a NON-ADJACENT link (link2)
    // back over the base link (link0) -- b0 and b2 are non-adjacent, so their capsules ARE checked.
    // At home (q=0) the arm is straight (extended, no self-collision); folding j1/j2 brings link2
    // back toward link0.
    SerialChain chain;
    DynJoint j0; j0.type = JType::Revolute; j0.parent = -1; j0.axis = Eigen::Vector3d(0,1,0);
    const int b0 = chain.addBody(j0, body);
    DynJoint j1; j1.type = JType::Revolute; j1.parent = b0; j1.ptree = Eigen::Vector3d(0.5,0,0);
    j1.axis = Eigen::Vector3d(0,1,0); const int b1 = chain.addBody(j1, body);
    DynJoint j2; j2.type = JType::Revolute; j2.parent = b1; j2.ptree = Eigen::Vector3d(0.5,0,0);
    j2.axis = Eigen::Vector3d(0,1,0); const int b2 = chain.addBody(j2, body);

    krs::plan::CollisionWorld world;
    world.capsules.push_back({ b0, Eigen::Vector3d(0,0,0), Eigen::Vector3d(0.5,0,0), 0.05 }); // link0 (base)
    world.capsules.push_back({ b1, Eigen::Vector3d(0,0,0), Eigen::Vector3d(0.5,0,0), 0.05 }); // link1
    world.capsules.push_back({ b2, Eigen::Vector3d(0,0,0), Eigen::Vector3d(0.5,0,0), 0.05 }); // link2 (folds back)

    krs::plan::JointLimits mech;
    mech.qLower.resize(3); mech.qUpper.resize(3); mech.vMax.resize(3);
    mech.qLower << -3.14159, -3.14159, -3.14159;
    mech.qUpper <<  3.14159,  3.14159,  3.14159;
    mech.vMax   << 2.0, 2.0, 2.0;

    const double clr = 0.01;   // 1 cm

    // STATIC from home: the box must be (a) clearance-safe at 1 cm across its extent, (b) strictly
    // narrower than mechanical range on the folding joints (j1, j2) -- you can't fold all the way.
    const ClearanceLimits stat = computeStaticLimits(chain, world, clr, mech, 300);
    bool boxSafe = stat.ok && stat.seedSafe;
    if (boxSafe) {
        for (int a = 0; a <= 6 && boxSafe; ++a)
            for (int b = 0; b <= 6 && boxSafe; ++b) {
                Eigen::VectorXd q(3);
                q[0] = 0.0;
                q[1] = stat.qLower[1] + (stat.qUpper[1]-stat.qLower[1]) * a/6.0;
                q[2] = stat.qLower[2] + (stat.qUpper[2]-stat.qLower[2]) * b/6.0;
                if (world.query(chain, q).minClearance < clr - 1e-3) boxSafe = false;
            }
    }
    const bool narrows = stat.ok
        && (stat.qUpper[1] - stat.qLower[1]) < (mech.qUpper[1] - mech.qLower[1]) - 1e-3
        && (stat.qUpper[2] - stat.qLower[2]) < (mech.qUpper[2] - mech.qLower[2]) - 1e-3;
    printf("[clearance]   static box: j2 range [%.3f, %.3f] (mech +-pi) narrower=%s ; box clearance-safe=%s\n",
           stat.qLower[2], stat.qUpper[2], narrows?"yes":"NO", boxSafe?"yes":"NO");

    // MONOTONE: a LARGER clearance target shrinks (never grows) the safe box.
    const ClearanceLimits statBig = computeStaticLimits(chain, world, 0.05, mech, 300);
    const bool monotone = statBig.ok
        && (statBig.qUpper[2] - statBig.qLower[2]) <= (stat.qUpper[2] - stat.qLower[2]) + 1e-6;
    printf("[clearance]   monotone: clearance 1cm j2 range=%.3f >= 5cm range=%.3f  %s\n",
           stat.qUpper[2]-stat.qLower[2], statBig.qUpper[2]-statBig.qLower[2], monotone?"yes":"NO");

    // DYNAMIC state-aware: with j1 EXTENDED (link1 straight, link2 far from base) j2 has a wide safe
    // range; with j1 already FOLDED (link2 region carried back near the base) j2's safe range is
    // narrower.
    Eigen::VectorXd qExt(3);  qExt  << 0.0, 0.0, 0.0;   // straight arm
    Eigen::VectorXd qFold(3); qFold << 0.0, 2.4, 0.0;   // shoulder folded back
    const ClearanceLimits dynExt  = computeDynamicLimits(chain, world, clr, qExt,  mech, 300);
    const ClearanceLimits dynFold = computeDynamicLimits(chain, world, clr, qFold, mech, 300);
    const double wExt  = dynExt.ok  ? dynExt.qUpper[2]  - dynExt.qLower[2]  : -1;
    const double wFold = dynFold.ok ? dynFold.qUpper[2] - dynFold.qLower[2] : -1;
    const bool stateAware = dynExt.ok && dynExt.seedSafe && wExt > wFold + 1e-3;
    printf("[clearance]   dynamic: j2 safe width arm-extended=%.3f > shoulder-folded=%.3f  %s\n",
           wExt, wFold, stateAware?"yes":"NO");

    // NEG-CTRL: a seed already IN collision collapses the interval + flags seedSafe=false.
    Eigen::VectorXd qHit(3); qHit << 0.0, 2.9, 2.9;    // fully folded onto the base link
    const ClearanceLimits neg = computeDynamicLimits(chain, world, clr, qHit, mech, 300);
    const bool negCollapse = neg.ok && !neg.seedSafe
        && std::abs(neg.qUpper[2] - neg.qLower[2]) < 1e-9;
    printf("[clearance]   NEG-CTRL: colliding seed collapses (seedSafe=%s width=%.3e)  %s\n",
           neg.seedSafe?"yes":"no", std::abs(neg.qUpper[2]-neg.qLower[2]), negCollapse?"REJECTS":"VACUOUS!");

    const bool pass = narrows && boxSafe && monotone && stateAware && negCollapse;
    printf("[clearance] %s\n", pass ? "ALL PASS (self-intersection -> clearance-safe joint box; monotone in clearance; state-aware dynamic; colliding-seed neg-ctrl)"
                                    : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::climits
