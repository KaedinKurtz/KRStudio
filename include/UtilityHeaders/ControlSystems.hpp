#pragma once

#include <Eigen/Dense>
#include <vector>
#include <functional>
#include <optional>

namespace ControlSystems {

    // --- Core Data Structures (FIX: Added missing definitions) ---

    /**
     * @brief Represents a linear time-invariant (LTI) system.
     * x_dot = Ax + Bu
     * y     = Cx + Du
     */
    struct StateSpaceModel {
        Eigen::MatrixXd A; // System matrix
        Eigen::MatrixXd B; // Input matrix
        Eigen::MatrixXd C; // Output matrix
        Eigen::MatrixXd D; // Feedforward matrix
    };

    /** @brief Defines the cost matrices for an LQR controller. */
    struct CostFunctionLQR {
        Eigen::MatrixXd Q; // State cost
        Eigen::MatrixXd R; // Input cost
    };

    /** @brief Contains the results of a controller design, primarily the gain matrix. */
    struct OptimalControlLaw {
        Eigen::MatrixXd gainMatrixK;
    };

    /** @brief Represents a classical PID controller's gains. */
    struct PIDController {
        double Kp = 0.0; // Proportional gain
        double Ki = 0.0; // Integral gain
        double Kd = 0.0; // Derivative gain
    };
    
    /** @brief Represents a system in the Laplace domain. */
    struct TransferFunction {
        std::vector<double> numerator_coeffs;
        std::vector<double> denominator_coeffs;
    };

    //==============================================================================
    // System Analysis
    //==============================================================================

    Eigen::MatrixXd computeControllabilityMatrix(const StateSpaceModel& model);
    bool isControllable(const StateSpaceModel& model);
    Eigen::MatrixXd computeObservabilityMatrix(const StateSpaceModel& model);
    bool isObservable(const StateSpaceModel& model);

    inline Eigen::MatrixXd computeNumericalJacobian(
        const std::function<Eigen::VectorXd(const Eigen::VectorXd&)>& func,
        const Eigen::VectorXd& x,
        double epsilon = 1e-6);

    // FIX: Added missing type 'const Eigen::VectorXd&' to the 'u' parameter
    inline Eigen::MatrixXd computeNumericalJacobian_A(
        const std::function<Eigen::VectorXd(const Eigen::VectorXd&, const Eigen::VectorXd&)>& func,
        const Eigen::VectorXd& x,
        const Eigen::VectorXd& u,
        double epsilon = 1e-6);

    inline Eigen::MatrixXd computeNumericalJacobian_B(
        const std::function<Eigen::VectorXd(const Eigen::VectorXd&, const Eigen::VectorXd&)>& func,
        const Eigen::VectorXd& x,
        const Eigen::VectorXd& u,
        double epsilon = 1e-6);

    //==============================================================================
    // Linear & State-Space Controllers
    //==============================================================================

    PIDController tunePID_ZN(const TransferFunction& model);
    OptimalControlLaw placePoles(const StateSpaceModel& model, const Eigen::VectorXcd& desired_poles);
    OptimalControlLaw solveCARE(const StateSpaceModel& model, const CostFunctionLQR& cost);
    OptimalControlLaw solveDARE(const StateSpaceModel& model, const CostFunctionLQR& cost);

    //==============================================================================
    // Nonlinear Controllers
    //==============================================================================
    
    struct SlidingModeParameters {
        Eigen::VectorXd lambda;
        double eta = 1.0;
        double boundary_layer_thickness = 0.05;
    };

    Eigen::VectorXd computeSlidingModeControl(const Eigen::VectorXd& state, const Eigen::VectorXd& desired_state, const SlidingModeParameters& params);

    //==============================================================================
    // Trajectory Optimization & Model Predictive Control (MPC)
    //==============================================================================

    struct FeedbackPolicy { /* Placeholder */ };

    struct TrajectoryOptimizationProblem {
        std::function<Eigen::VectorXd(const Eigen::VectorXd&, const Eigen::VectorXd&)> dynamics_func;
        std::function<double(const Eigen::VectorXd&, const Eigen::VectorXd&)> running_cost_func;
        std::function<double(const Eigen::VectorXd&)> final_cost_func;
        Eigen::VectorXd initialState;
        size_t horizon;
    };

    struct TrajectoryOptimizationSolution {
        std::vector<Eigen::VectorXd> state_trajectory;
        std::vector<Eigen::VectorXd> control_trajectory;
        FeedbackPolicy policy;
        bool has_converged = false;
    };

    struct MPCConstraints {
        std::optional<Eigen::VectorXd> min_state;
        std::optional<Eigen::VectorXd> max_state;
        std::optional<Eigen::VectorXd> min_control;
        std::optional<Eigen::VectorXd> max_control;
    };

    TrajectoryOptimizationSolution solve_iLQR(const TrajectoryOptimizationProblem& problem);
    TrajectoryOptimizationSolution solve_DDP(const TrajectoryOptimizationProblem& problem);

    // FIX: Removed the duplicate definition of this class
    class ModelPredictiveController {
    public:
        ModelPredictiveController(const TrajectoryOptimizationProblem& problem,
            const MPCConstraints& constraints);
        Eigen::VectorXd computeControl(const Eigen::VectorXd& currentState);
    };

    //==============================================================================
    // State Estimation
    //==============================================================================

    struct KalmanFilterState {
        Eigen::VectorXd x; // State estimate
        Eigen::MatrixXd P; // State covariance (uncertainty)
    };

    KalmanFilterState kalmanPredict(const KalmanFilterState& currentState, const StateSpaceModel& model, const Eigen::VectorXd& controlInput, const Eigen::MatrixXd& Q_process_noise);
    KalmanFilterState kalmanUpdate(const KalmanFilterState& predictedState, const StateSpaceModel& model, const Eigen::VectorXd& measurement, const Eigen::MatrixXd& R_measurement_noise);

} // namespace ControlSystems
