#pragma once

#include <QWidget>

#include "Node.hpp"
#include "NodeFactory.hpp"
#include <Eigen/Dense>

namespace NodeLibrary {

    // --- Core Data Structures for State-Space ---

    /**
     * @brief A structure to hold the four matrices (A, B, C, D) that define a
     * linear time-invariant (LTI) state-space model.
     * x_dot = Ax + Bu
     * y     = Cx + Du
     */
    struct StateSpaceModel {
        Eigen::MatrixXf A; // State matrix
        Eigen::MatrixXf B; // Input matrix
        Eigen::MatrixXf C; // Output matrix
        Eigen::MatrixXf D; // Feed-forward matrix
    };

    /**
     * @brief A structure to hold the results of an LQR calculation.
     */
    struct LQRResult {
        Eigen::MatrixXf K; // Optimal state-feedback gain matrix
        Eigen::MatrixXf P; // Solution to the Riccati equation
    };


    // --- Node Classes ---

    // --- Model Definition ---
    class DefineLTISystemNode : public Node { public:
        QWidget* createCustomWidget() override; DefineLTISystemNode(); void compute() override; };

    // --- Simulation ---
    class StateSpaceSimulatorNode : public Node {
    public:
        QWidget* createCustomWidget() override;
        StateSpaceSimulatorNode();
        void compute() override;
    private:
        Eigen::VectorXf m_state_x; // Internal state vector
        bool m_is_initialized = false;
    };

    // --- Control & Regulation ---
    class LQRControllerNode : public Node { public:
        QWidget* createCustomWidget() override; LQRControllerNode(); void compute() override; };
    class StateFeedbackRegulatorNode : public Node { public:
        QWidget* createCustomWidget() override; StateFeedbackRegulatorNode(); void compute() override; };

    // --- System Analysis ---
    class ControllabilityMatrixNode : public Node { public:
        QWidget* createCustomWidget() override; ControllabilityMatrixNode(); void compute() override; };
    class ObservabilityMatrixNode : public Node { public:
        QWidget* createCustomWidget() override; ObservabilityMatrixNode(); void compute() override; };
    class CheckStabilityNode : public Node { public:
        QWidget* createCustomWidget() override; CheckStabilityNode(); void compute() override; };

    // --- Monitoring & Safety ---
    class StateMonitorNode : public Node { public:
        QWidget* createCustomWidget() override; StateMonitorNode(); void compute() override; };

} // namespace NodeLibrary
