#pragma once

#include <glm/glm.hpp>
#include <vector>

/**
 * @brief Reverse-mode (adjoint) differentiable MLS-MPM — the gradient/training
 * twin of the realtime GPU MpmSystem. A compact CPU double-precision forward
 * solver that reproduces the GPU shader math exactly (quadratic B-spline
 * weights, D^-1 = 4/dx^2 APIC, fixed-corotated Neo-Hookean, Drucker-Prager),
 * plus a tape-based reverse pass that backpropagates a scalar loss to the
 * control inputs. This mirrors the engine's existing "realtime GPU + CPU
 * reference" split (GPU PBF vs CPU DFSPH): the GPU solver renders, this core
 * differentiates.
 *
 * Double precision + deterministic CPU execution is a deliberate choice: it is
 * what lets the analytical gradients match central finite differences to the
 * <1e-5 bar required by KRS_MPM_SELFTEST's ADJOINT_GRADIENT_CHECK (GPU
 * fixed-point atomics are order-nondeterministic and would never reach it).
 */
namespace krs::mpmad {

using vec3 = glm::dvec3;
using mat3 = glm::dmat3;

enum class Mat : int { Fluid = 0, Elastic = 1, Sand = 2 };

struct Particle {
    vec3 x{ 0.0 };       // position (m)
    vec3 v{ 0.0 };       // velocity (m/s)
    mat3 C{ 0.0 };       // APIC affine velocity matrix
    mat3 F{ 1.0 };       // deformation gradient
    double mass = 1.0;   // kg
    double vol = 1.0;    // rest volume V0 (m^3)
    double mu = 0.0;     // Lame mu (or bulk K for fluid)
    double lambda = 0.0; // Lame lambda (or Tait gamma for fluid)
    double alpha = 0.0;  // Drucker-Prager friction coefficient
    double Jp = 1.0;     // plastic/volume scalar (fluid: J)
    Mat material = Mat::Elastic;
};

struct Config {
    int N = 20;                  // grid cells per axis
    double dx = 0.1;             // cell size (m)
    vec3 origin{ -1.0, -0.2, -1.0 };
    vec3 gravity{ 0.0, -9.81, 0.0 };
    double dt = 1.0e-3;          // substep (s)
    int steps = 24;              // substeps per rollout
    int bound = 2;               // wall BC band (cells)
};

/// SVD F = U diag(sigma) V^T with U,V proper rotations and the reflection sign
/// pushed onto the smallest singular value (so U*diag(sigma)*V^T == F exactly).
void svd3(const mat3& F, mat3& U, vec3& sigma, mat3& V);

/// Reverse of svd3: given dL/dU, dL/dsigma, dL/dV, return dL/dF.
/// Uses the analytic SVD differential; repeated singular values are guarded by
/// clamping the 1/(sj^2 - si^2) coupling to zero (documented trade-off).
mat3 svd3Adjoint(const mat3& U, const vec3& sigma, const mat3& V,
                 const mat3& gU, const vec3& gSigma, const mat3& gV);

/// Result of a gradient check: analytical vs central finite difference.
struct GradCheck {
    bool pass = false;
    double maxRelErr = 0.0;
    std::vector<double> analytic;
    std::vector<double> numeric;
};

/// Peak-stress evaluation of an elastic sample under a body acceleration — the
/// "heavy exact" pass behind the trajectory verifier. Drops a clamped elastic
/// block, loads it with the given acceleration, runs the double-precision
/// forward MLS-MPM and reports the peak von Mises Cauchy stress vs the yield.
struct StressEval {
    double maxVonMises = 0.0; // Pa, peak over particles and the rollout
    double yield = 0.0;       // Pa
    bool exceeded = false;    // maxVonMises > yield
    int steps = 0;
};
StressEval evaluatePeakStress(double accel, double youngsE, double nu,
                              double density, double yieldStress);

/// Headless verification suite hooks (Task 3 modules).
bool runSelfTests();              // runs all adjoint checks, logs PASS/FAIL
GradCheck checkSvdAdjoint();      // standalone SVD-adjoint FD check
GradCheck checkElasticGradient(); // dL/dv0 of a deforming elastic block, <1e-5
GradCheck checkSandGradient();    // Drucker-Prager return-map adjoint check

} // namespace krs::mpmad
