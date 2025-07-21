#pragma once
#include "components.hpp" // For types like glm, Eigen, StateSpaceModel, etc.
#include "TemporalBuffer.hpp"
#include <vector>

namespace DataAnalysis {

    // --- General & Statistical Analysis ---
    double calculateMean(const TemporalBuffer<double>& buffer);
    double calculateVariance(const TemporalBuffer<double>& buffer);
    double calculateStdDev(const TemporalBuffer<double>& buffer);

    // --- Vector & Spatial Analysis (for glm::vec3) ---
    double calculatePathLength(const TemporalBuffer<glm::vec3>& buffer);
    glm::vec3 calculateDisplacement(const TemporalBuffer<glm::vec3>& buffer);
    glm::vec3 calculateInstantaneousVelocity(const TemporalBuffer<glm::vec3>& buffer);

    // --- Orientation Analysis (for glm::quat) ---
    glm::vec3 calculateInstantaneousAngularVelocity(const TemporalBuffer<glm::quat>& buffer);

    // --- Point Cloud Analysis (operates on a common format) ---
    AABB calculateAABB(const std::vector<glm::vec3>& cloud);
    glm::vec3 calculateCentroid(const std::vector<glm::vec3>& cloud);
    float calculateDensity(const std::vector<glm::vec3>& cloud, const glm::vec3& center, float radius);
    glm::vec4 calculatePlaneOfBestFit(const std::vector<glm::vec3>& cloud); // Returns {nx, ny, nz, d}

    // --- Gradient & Field Analysis ---
    // Calculates the gradient of a scalar field represented by discrete points.
    glm::vec3 estimateGradientAtPoint(const std::vector<std::pair<glm::vec3, float>>& scalarField, const glm::vec3& point);

    // --- Robotics & Control (Leveraging your components.hpp) ---

    /**
     * @brief Simulates one step of a rigid body's motion using symplectic Euler integration.
     * @param body The rigidbody's current state (velocity, mass, etc.).
     * @param transform The body's current transform.
     * @param dt The time step for the simulation.
     */
    void integrateRigidBody(RigidBodyComponent& body, TransformComponent& transform, float dt);

} // namespace DataAnalysis