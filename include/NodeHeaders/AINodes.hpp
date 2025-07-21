#pragma once

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "components.hpp" // For InferenceModel, Blackboard, etc.
#include <entt/entt.hpp>
#include <Eigen/Dense>
#include <optional>
#include <functional>

namespace NodeLibrary {

    // The status result for Behavior Tree nodes
    enum class BTStatus { Success, Failure, Running };

    // --- Free Functions (to be wrapped by nodes) ---

    /** @brief Runs a pre-loaded ML model to get an inference. */
    Eigen::VectorXf runInference(const InferenceModel& model_handle, const Eigen::VectorXf& input_data);

    // --- Node Classes ---

    /** @brief Node that wraps the runInference function. */
    class RunInferenceNode : public Node {
    public:
        RunInferenceNode();
        void compute() override; // FIX: Was process()
    };

    /** @brief Node to set a float value in a blackboard. */
    class SetBlackboardFloatNode : public Node {
    public:
        SetBlackboardFloatNode();
        void compute() override; // FIX: Was process()
    };

    /** @brief Node to get a float value from a blackboard. */
    class GetBlackboardFloatNode : public Node {
    public:
        GetBlackboardFloatNode();
        void compute() override; // FIX: Was process()
    };

    // --- Behavior Tree Nodes ---

    /** @brief A BT leaf node that performs an action. */
    class BTActionNode : public Node {
    public:
        BTActionNode();
        void compute() override; // FIX: Was process()
        // In a real system, this function would be configured in the IDE
        std::function<BTStatus()> tick_func = []() { return BTStatus::Success; };
    };

    /** @brief A BT composite node that executes children sequentially. Fails if any child fails. */
    class BTSequenceNode : public Node {
    public:
        BTSequenceNode();
        void compute() override; // FIX: Was process()
    };

    /** @brief A BT composite node that executes children sequentially. Succeeds if any child succeeds. */
    class BTSelectorNode : public Node {
    public:
        BTSelectorNode();
        void compute() override; // FIX: Was process()
    };

} // namespace NodeLibrary
