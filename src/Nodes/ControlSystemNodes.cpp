#include "ControlSystemsNodes.hpp"
#include <memory> // Required for std::make_unique

namespace NodeLibrary {

    // --- System Analysis ---

    IsControllableNode::IsControllableNode() {
	m_id = "control_is_controllable";
        m_ports.push_back({ "Model", {"StateSpaceModel", "model"}, Port::Direction::Input, this });
        m_ports.push_back({ "Is Controllable", {"bool", "boolean"}, Port::Direction::Output, this });
        m_ports.push_back({ "Controllability Matrix", {"Eigen::MatrixXd", "matrix"}, Port::Direction::Output, this });
    }
    void IsControllableNode::compute() {
        auto model = getInput<ControlSystems::StateSpaceModel>("Model");
        if (model) {
            setOutput("Is Controllable", ControlSystems::isControllable(*model));
            setOutput("Controllability Matrix", ControlSystems::computeControllabilityMatrix(*model));
        }
    }
    namespace {
        struct IsControllableRegistrar {
            IsControllableRegistrar() {
                NodeDescriptor desc = { "Is Controllable", "Control/Analysis", "Checks if a state-space system is controllable." };
                NodeFactory::instance().registerNodeType("control_is_controllable", desc, []() { return std::make_unique<IsControllableNode>(); });
            }
        };
        static IsControllableRegistrar g_isControllableRegistrar;
    }

    IsObservableNode::IsObservableNode() {
	m_id = "control_is_observable";
        m_ports.push_back({ "Model", {"StateSpaceModel", "model"}, Port::Direction::Input, this });
        m_ports.push_back({ "Is Observable", {"bool", "boolean"}, Port::Direction::Output, this });
        m_ports.push_back({ "Observability Matrix", {"Eigen::MatrixXd", "matrix"}, Port::Direction::Output, this });
    }
    void IsObservableNode::compute() {
        auto model = getInput<ControlSystems::StateSpaceModel>("Model");
        if (model) {
            setOutput("Is Observable", ControlSystems::isObservable(*model));
            setOutput("Observability Matrix", ControlSystems::computeObservabilityMatrix(*model));
        }
    }
    namespace {
        struct IsObservableRegistrar {
            IsObservableRegistrar() {
                NodeDescriptor desc = { "Is Observable", "Control/Analysis", "Checks if a state-space system is observable." };
                NodeFactory::instance().registerNodeType("control_is_observable", desc, []() { return std::make_unique<IsObservableNode>(); });
            }
        };
        static IsObservableRegistrar g_isObservableRegistrar;
    }

    // --- Controller Design ---

    PolePlacementNode::PolePlacementNode() {
	m_id = "control_pole_placement";
        m_ports.push_back({ "Model", {"StateSpaceModel", "model"}, Port::Direction::Input, this });
        m_ports.push_back({ "Desired Poles", {"Eigen::VectorXcd", "poles"}, Port::Direction::Input, this });
        m_ports.push_back({ "K (Gain)", {"Eigen::MatrixXd", "matrix"}, Port::Direction::Output, this });
    }
    void PolePlacementNode::compute() {
        auto model = getInput<ControlSystems::StateSpaceModel>("Model");
        auto poles = getInput<Eigen::VectorXcd>("Desired Poles");
        if (model && poles) {
            ControlSystems::OptimalControlLaw result = ControlSystems::placePoles(*model, *poles);
            setOutput("K (Gain)", result.gainMatrixK);
        }
    }
    namespace {
        struct PolePlacementRegistrar {
            PolePlacementRegistrar() {
                NodeDescriptor desc = { "Pole Placement (Ackermann)", "Control/Design", "Computes feedback gain K to place closed-loop poles." };
                NodeFactory::instance().registerNodeType("control_pole_placement", desc, []() { return std::make_unique<PolePlacementNode>(); });
            }
        };
        static PolePlacementRegistrar g_polePlacementRegistrar;
    }

    LQRDesignNode::LQRDesignNode() {
	m_id = "control_lqr_design";
        m_ports.push_back({ "Model", {"StateSpaceModel", "model"}, Port::Direction::Input, this });
        m_ports.push_back({ "Q (State Cost)", {"Eigen::MatrixXd", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "R (Input Cost)", {"Eigen::MatrixXd", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "K (Gain)", {"Eigen::MatrixXd", "matrix"}, Port::Direction::Output, this });
    }
    void LQRDesignNode::compute() {
        auto model = getInput<ControlSystems::StateSpaceModel>("Model");
        auto q = getInput<Eigen::MatrixXd>("Q (State Cost)");
        auto r = getInput<Eigen::MatrixXd>("R (Input Cost)");
        if (model && q && r) {
            ControlSystems::CostFunctionLQR cost = { *q, *r };
            ControlSystems::OptimalControlLaw result = ControlSystems::solveDARE(*model, cost);
            setOutput("K (Gain)", result.gainMatrixK);
        }
    }
    namespace {
        struct LQRDesignRegistrar {
            LQRDesignRegistrar() {
                NodeDescriptor desc = { "LQR Design", "Control/Design", "Computes the optimal LQR feedback gain K." };
                NodeFactory::instance().registerNodeType("control_lqr_design", desc, []() { return std::make_unique<LQRDesignNode>(); });
            }
        };
        static LQRDesignRegistrar g_lqrDesignRegistrar;
    }

    // --- State Estimation ---

    KalmanFilterNode::KalmanFilterNode() {
	m_id = "control_kalman_filter";
        m_ports.push_back({ "Model", {"StateSpaceModel", "model"}, Port::Direction::Input, this });
        m_ports.push_back({ "u (Control)", {"Eigen::VectorXd", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "z (Measurement)", {"Eigen::VectorXd", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "Q (Process Noise)", {"Eigen::MatrixXd", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "R (Measurement Noise)", {"Eigen::MatrixXd", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "x_hat (Estimate)", {"Eigen::VectorXd", "vector"}, Port::Direction::Output, this });
    }
    void KalmanFilterNode::compute() {
        auto model = getInput<ControlSystems::StateSpaceModel>("Model");
        if (!m_is_initialized) {
            if (model) {
                m_state.x = Eigen::VectorXd::Zero(model->A.rows());
                m_state.P = Eigen::MatrixXd::Identity(model->A.rows(), model->A.cols());
                m_is_initialized = true;
            }
            else return;
        }

        auto u = getInput<Eigen::VectorXd>("u (Control)");
        auto z = getInput<Eigen::VectorXd>("z (Measurement)");
        auto Q = getInput<Eigen::MatrixXd>("Q (Process Noise)");
        auto R = getInput<Eigen::MatrixXd>("R (Measurement Noise)");

        if (model && u && z && Q && R) {
            auto predicted_state = ControlSystems::kalmanPredict(m_state, *model, *u, *Q);
            m_state = ControlSystems::kalmanUpdate(predicted_state, *model, *z, *R);
            setOutput("x_hat (Estimate)", m_state.x);
        }
    }
    namespace {
        struct KalmanFilterRegistrar {
            KalmanFilterRegistrar() {
                NodeDescriptor desc = { "Kalman Filter", "Control/Estimation", "Estimates system state from noisy measurements." };
                NodeFactory::instance().registerNodeType("control_kalman_filter", desc, []() { return std::make_unique<KalmanFilterNode>(); });
            }
        };
        static KalmanFilterRegistrar g_kalmanFilterRegistrar;
    }

} // namespace NodeLibrary