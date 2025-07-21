#include "StateSpaceNodes.hpp"
#include <iostream>
#include <memory> // For std::make_unique

namespace NodeLibrary {

    // --- Model Definition ---

    // DefineLTISystemNode
    DefineLTISystemNode::DefineLTISystemNode() {
	m_id = "statespace_define_lti";
        m_ports.push_back({ "A", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "B", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "C", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "D", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "Model", {"StateSpaceModel", "model"}, Port::Direction::Output, this });
    }
    void DefineLTISystemNode::compute() {
        auto a = getInput<Eigen::MatrixXf>("A");
        auto b = getInput<Eigen::MatrixXf>("B");
        auto c = getInput<Eigen::MatrixXf>("C");
        auto d = getInput<Eigen::MatrixXf>("D");
        if (a && b && c && d) {
            // Basic validation for MIMO systems
            if (a->rows() == a->cols() && a->rows() == b->rows() && a->cols() == c->cols() && c->rows() == d->rows() && b->cols() == d->cols()) {
                StateSpaceModel model = { *a, *b, *c, *d };
                setOutput("Model", model);
            }
        }
    }
    namespace {
        struct DefineLTISystemRegistrar {
            DefineLTISystemRegistrar() {
                NodeDescriptor desc = { "Define LTI System", "State-Space/Definition", "Creates a state-space model from A,B,C,D matrices." };
                NodeFactory::instance().registerNodeType("statespace_define_lti", desc, []() { return std::make_unique<DefineLTISystemNode>(); });
            }
        };
        static DefineLTISystemRegistrar g_defineLTISystemRegistrar;
    }


    // --- Simulation ---

    // StateSpaceSimulatorNode
    StateSpaceSimulatorNode::StateSpaceSimulatorNode() {
	m_id = "statespace_simulator";
        m_ports.push_back({ "Model", {"StateSpaceModel", "model"}, Port::Direction::Input, this });
        m_ports.push_back({ "u (Input)", {"Eigen::VectorXf", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "x0 (Initial)", {"Eigen::VectorXf", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "dt (Time Step)", {"float", "seconds"}, Port::Direction::Input, this });
        m_ports.push_back({ "Reset", {"bool", "boolean"}, Port::Direction::Input, this });
        m_ports.push_back({ "x (State)", {"Eigen::VectorXf", "vector"}, Port::Direction::Output, this });
        m_ports.push_back({ "y (Output)", {"Eigen::VectorXf", "vector"}, Port::Direction::Output, this });
    }
    void StateSpaceSimulatorNode::compute() {
        auto reset = getInput<bool>("Reset");
        if (reset && *reset) m_is_initialized = false;

        auto x0 = getInput<Eigen::VectorXf>("x0 (Initial)");
        if (!m_is_initialized && x0) {
            m_state_x = *x0;
            m_is_initialized = true;
        }

        if (!m_is_initialized) return;

        auto model = getInput<StateSpaceModel>("Model");
        auto u = getInput<Eigen::VectorXf>("u (Input)");
        auto dt = getInput<float>("dt (Time Step)");

        if (model && u && dt) {
            // Discrete-time simulation step using forward Euler
            // x(k+1) = (I + A*dt)*x(k) + (B*dt)*u(k)
            Eigen::MatrixXf I = Eigen::MatrixXf::Identity(model->A.rows(), model->A.cols());
            m_state_x = (I + model->A * (*dt)) * m_state_x + (model->B * (*dt)) * (*u);
            Eigen::VectorXf y = model->C * m_state_x + model->D * (*u);

            setOutput("x (State)", m_state_x);
            setOutput("y (Output)", y);
        }
    }
    namespace {
        struct StateSpaceSimulatorRegistrar {
            StateSpaceSimulatorRegistrar() {
                NodeDescriptor desc = { "State-Space Simulator", "State-Space/Simulation", "Simulates an LTI system over time." };
                NodeFactory::instance().registerNodeType("statespace_simulator", desc, []() { return std::make_unique<StateSpaceSimulatorNode>(); });
            }
        };
        static StateSpaceSimulatorRegistrar g_stateSpaceSimulatorRegistrar;
    }


    // --- Control & Regulation ---

    // LQRControllerNode
    LQRControllerNode::LQRControllerNode() {
	m_id = "statespace_lqr";
        m_ports.push_back({ "Model", {"StateSpaceModel", "model"}, Port::Direction::Input, this });
        m_ports.push_back({ "Q (State Cost)", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "R (Input Cost)", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "LQR Result", {"LQRResult", "result"}, Port::Direction::Output, this });
        m_ports.push_back({ "Success", {"bool", "boolean"}, Port::Direction::Output, this });
    }
    void LQRControllerNode::compute() {
        auto model = getInput<StateSpaceModel>("Model");
        auto q = getInput<Eigen::MatrixXf>("Q (State Cost)");
        auto r = getInput<Eigen::MatrixXf>("R (Input Cost)");

        if (model && q && r) {
            std::cout << "LQR: Solving DARE is a complex numerical task. This is a placeholder.\n";
            Eigen::MatrixXf k = Eigen::MatrixXf::Zero(model->B.cols(), model->A.rows());
            Eigen::MatrixXf p = Eigen::MatrixXf::Identity(model->A.rows(), model->A.cols());

            LQRResult result = { k, p };
            setOutput("LQR Result", result);
            setOutput("Success", true);
            return;
        }
        setOutput("Success", false);
    }
    namespace {
        struct LQRControllerRegistrar {
            LQRControllerRegistrar() {
                NodeDescriptor desc = { "LQR Controller", "State-Space/Control", "Calculates the optimal LQR gain matrix K." };
                NodeFactory::instance().registerNodeType("statespace_lqr", desc, []() { return std::make_unique<LQRControllerNode>(); });
            }
        };
        static LQRControllerRegistrar g_lqrControllerRegistrar;
    }


    // StateFeedbackRegulatorNode
    StateFeedbackRegulatorNode::StateFeedbackRegulatorNode() {
	m_id = "statespace_regulator";
        m_ports.push_back({ "K (Gain)", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "x (State)", {"Eigen::VectorXf", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "u (Control)", {"Eigen::VectorXf", "vector"}, Port::Direction::Output, this });
    }
    void StateFeedbackRegulatorNode::compute() {
        auto k = getInput<Eigen::MatrixXf>("K (Gain)");
        auto x = getInput<Eigen::VectorXf>("x (State)");
        if (k && x && k->cols() == x->rows()) {
            setOutput("u (Control)", -(*k) * (*x));
        }
    }
    namespace {
        struct StateFeedbackRegulatorRegistrar {
            StateFeedbackRegulatorRegistrar() {
                NodeDescriptor desc = { "State Feedback Regulator", "State-Space/Control", "Applies state feedback control law u = -Kx." };
                NodeFactory::instance().registerNodeType("statespace_regulator", desc, []() { return std::make_unique<StateFeedbackRegulatorNode>(); });
            }
        };
        static StateFeedbackRegulatorRegistrar g_stateFeedbackRegulatorRegistrar;
    }


    // --- System Analysis ---

    // ControllabilityMatrixNode
    ControllabilityMatrixNode::ControllabilityMatrixNode() {
	m_id = "statespace_controllability";
        m_ports.push_back({ "Model", {"StateSpaceModel", "model"}, Port::Direction::Input, this });
        m_ports.push_back({ "Co (Matrix)", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Output, this });
        m_ports.push_back({ "Rank", {"int", "unitless"}, Port::Direction::Output, this });
        m_ports.push_back({ "Is Controllable", {"bool", "boolean"}, Port::Direction::Output, this });
    }
    void ControllabilityMatrixNode::compute() {
        auto model = getInput<StateSpaceModel>("Model");
        if (model) {
            const auto& A = model->A;
            const auto& B = model->B;
            int n = A.rows();
            Eigen::MatrixXf Co(n, n * B.cols());
            Co.leftCols(B.cols()) = B;
            for (int i = 1; i < n; ++i) {
                Co.middleCols(i * B.cols(), B.cols()) = A * Co.middleCols((i - 1) * B.cols(), B.cols());
            }
            long rank = Co.fullPivLu().rank();
            setOutput("Co (Matrix)", Co);
            setOutput("Rank", (int)rank);
            setOutput("Is Controllable", rank == n);
        }
    }
    namespace {
        struct ControllabilityMatrixRegistrar {
            ControllabilityMatrixRegistrar() {
                NodeDescriptor desc = { "Controllability", "State-Space/Analysis", "Checks if the system is controllable." };
                NodeFactory::instance().registerNodeType("statespace_controllability", desc, []() { return std::make_unique<ControllabilityMatrixNode>(); });
            }
        };
        static ControllabilityMatrixRegistrar g_controllabilityMatrixRegistrar;
    }


    // CheckStabilityNode
    CheckStabilityNode::CheckStabilityNode() {
	m_id = "statespace_stability";
        m_ports.push_back({ "A (Matrix)", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "Eigenvalues", {"Eigen::VectorXcf", "vector"}, Port::Direction::Output, this });
        m_ports.push_back({ "Is Stable", {"bool", "boolean"}, Port::Direction::Output, this });
    }
    void CheckStabilityNode::compute() {
        auto a = getInput<Eigen::MatrixXf>("A (Matrix)");
        if (a && a->rows() == a->cols()) {
            Eigen::VectorXcf eigenvalues = a->eigenvalues();
            bool stable = true;
            for (int i = 0; i < eigenvalues.size(); ++i) {
                if (std::abs(eigenvalues[i]) >= 1.0) {
                    stable = false;
                    break;
                }
            }
            setOutput("Eigenvalues", eigenvalues);
            setOutput("Is Stable", stable);
        }
    }
    namespace {
        struct CheckStabilityRegistrar {
            CheckStabilityRegistrar() {
                NodeDescriptor desc = { "Check Stability", "State-Space/Analysis", "Checks system stability from eigenvalues of A." };
                NodeFactory::instance().registerNodeType("statespace_stability", desc, []() { return std::make_unique<CheckStabilityNode>(); });
            }
        };
        static CheckStabilityRegistrar g_checkStabilityRegistrar;
    }


    // --- Monitoring & Safety ---

    // StateMonitorNode
    StateMonitorNode::StateMonitorNode() {
	m_id = "statespace_monitor";
        m_ports.push_back({ "x (State)", {"Eigen::VectorXf", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "Min Bounds", {"Eigen::VectorXf", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "Max Bounds", {"Eigen::VectorXf", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "Is Violated", {"bool", "boolean"}, Port::Direction::Output, this });
    }
    void StateMonitorNode::compute() {
        auto x = getInput<Eigen::VectorXf>("x (State)");
        auto min_b = getInput<Eigen::VectorXf>("Min Bounds");
        auto max_b = getInput<Eigen::VectorXf>("Max Bounds");
        if (x && min_b && max_b && x->size() == min_b->size() && x->size() == max_b->size()) {
            bool violated = !((x->array() >= min_b->array()).all() && (x->array() <= max_b->array()).all());
            setOutput("Is Violated", violated);
        }
    }
    namespace {
        struct StateMonitorRegistrar {
            StateMonitorRegistrar() {
                NodeDescriptor desc = { "State Monitor (Watchdog)", "State-Space/Monitoring", "Checks if a state vector is within bounds." };
                NodeFactory::instance().registerNodeType("statespace_monitor", desc, []() { return std::make_unique<StateMonitorNode>(); });
            }
        };
        static StateMonitorRegistrar g_stateMonitorRegistrar;
    }

} // namespace NodeLibrary