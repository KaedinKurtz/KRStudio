#include "AINodes.hpp"
#include <iostream> 
#include <memory>   // Required for std::make_unique
#include "components.hpp"

namespace NodeLibrary {

    // --- Free Function Implementations ---

    Eigen::VectorXf runInference(const InferenceModel& model_handle, const Eigen::VectorXf& input_data) {
        std::cout << "Running inference on model " << model_handle.model_id << "..." << std::endl;
        Eigen::VectorXf result = input_data * 0.5f; // Dummy operation.
        return result;
    }

    // --- Node Implementations & Registrations ---

    // RunInferenceNode
    RunInferenceNode::RunInferenceNode() {
	m_id = "ai_run_inference";
        m_ports.push_back({ "Model", {"InferenceModel", "model"}, Port::Direction::Input, this });
        m_ports.push_back({ "Input", {"Eigen::VectorXf", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "Output", {"Eigen::VectorXf", "vector"}, Port::Direction::Output, this });
    }

    void RunInferenceNode::compute() {
        auto model = getInput<InferenceModel>("Model");
        auto input = getInput<Eigen::VectorXf>("Input");
        if (model && input) {
            setOutput("Output", runInference(*model, *input));
        }
    }

    namespace {
        struct RunInferenceNodeRegistrar {
            RunInferenceNodeRegistrar() {
                NodeDescriptor desc = { "Run Inference", "AI", "Runs a machine learning model." };
                NodeFactory::instance().registerNodeType("ai_run_inference", desc, []() { return std::make_unique<RunInferenceNode>(); });
            }
        };
    }
    static RunInferenceNodeRegistrar g_runInferenceRegistrar;

    // SetBlackboardFloatNode
    SetBlackboardFloatNode::SetBlackboardFloatNode() {
	m_id = "ai_set_blackboard_float";
        m_ports.push_back({ "Blackboard In", {"Blackboard", "handle"}, Port::Direction::Input, this });
        m_ports.push_back({ "Key", {"std::string", "text"}, Port::Direction::Input, this });
        m_ports.push_back({ "Value", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Blackboard Out", {"Blackboard", "handle"}, Port::Direction::Output, this });
    }

    void SetBlackboardFloatNode::compute() {
        auto blackboard = getInput<Blackboard>("Blackboard In");
        auto key = getInput<std::string>("Key");
        auto value = getInput<float>("Value");
        if (blackboard && key && value) {
            blackboard->table[*key] = *value;
            setOutput("Blackboard Out", *blackboard);
        }
    }

    namespace {
        struct SetBlackboardFloatNodeRegistrar {
            SetBlackboardFloatNodeRegistrar() {
                NodeDescriptor desc = { "Set Blackboard (Float)", "AI/Blackboard", "Sets a float value in a blackboard." };
                NodeFactory::instance().registerNodeType("ai_set_blackboard_float", desc, []() { return std::make_unique<SetBlackboardFloatNode>(); });
            }
        };
    }
    static SetBlackboardFloatNodeRegistrar g_setBlackboardFloatRegistrar;

    // GetBlackboardFloatNode
    GetBlackboardFloatNode::GetBlackboardFloatNode() {
	m_id = "ai_get_blackboard_float";
        m_ports.push_back({ "Blackboard", {"Blackboard", "handle"}, Port::Direction::Input, this });
        m_ports.push_back({ "Key", {"std::string", "text"}, Port::Direction::Input, this });
        m_ports.push_back({ "Value", {"float", "unitless"}, Port::Direction::Output, this });
        m_ports.push_back({ "Found", {"bool", "boolean"}, Port::Direction::Output, this });
    }

    void GetBlackboardFloatNode::compute() {
        auto blackboard = getInput<Blackboard>("Blackboard");
        auto key = getInput<std::string>("Key");
        if (blackboard && key) {
            auto it = blackboard->table.find(*key);
            if (it != blackboard->table.end()) {
                if (auto* value = entt::any_cast<float>(&it->second)) {
                    setOutput("Value", *value);
                    setOutput("Found", true);
                    return;
                }
            }
        }
        setOutput("Found", false);
    }

    namespace {
        struct GetBlackboardFloatNodeRegistrar {
            GetBlackboardFloatNodeRegistrar() {
                NodeDescriptor desc = { "Get Blackboard (Float)", "AI/Blackboard", "Gets a float value from a blackboard." };
                NodeFactory::instance().registerNodeType("ai_get_blackboard_float", desc, []() { return std::make_unique<GetBlackboardFloatNode>(); });
            }
        };
    }
    static GetBlackboardFloatNodeRegistrar g_getBlackboardFloatRegistrar;

    // --- Behavior Tree Nodes ---

    // BTActionNode
    BTActionNode::BTActionNode() {
	m_id = "ai_bt_action";
        m_ports.push_back({ "In", {"Execution", "trigger"}, Port::Direction::Input, this });
        m_ports.push_back({ "Status", {"BTStatus", "enum"}, Port::Direction::Output, this });
    }

    void BTActionNode::compute() {
        if (tick_func) {
            setOutput("Status", tick_func());
        }
        else {
            setOutput("Status", BTStatus::Failure);
        }
    }

    namespace {
        struct BTActionNodeRegistrar {
            BTActionNodeRegistrar() {
                NodeDescriptor desc = { "BT Action", "AI/Behavior Tree", "A leaf node that performs an action." };
                NodeFactory::instance().registerNodeType("ai_bt_action", desc, []() { return std::make_unique<BTActionNode>(); });
            }
        };
    }
    static BTActionNodeRegistrar g_btActionRegistrar;

    // BTSequenceNode
    BTSequenceNode::BTSequenceNode() {
	m_id = "ai_bt_sequence";
        m_ports.push_back({ "Parent", {"Execution", "trigger"}, Port::Direction::Input, this });
        m_ports.push_back({ "Child 1 Status", {"BTStatus", "enum"}, Port::Direction::Input, this });
        m_ports.push_back({ "Child 2 Status", {"BTStatus", "enum"}, Port::Direction::Input, this });
        m_ports.push_back({ "Status", {"BTStatus", "enum"}, Port::Direction::Output, this });
    }

    void BTSequenceNode::compute() {
        auto child1_status = getInput<BTStatus>("Child 1 Status");
        if (child1_status && (*child1_status == BTStatus::Failure || *child1_status == BTStatus::Running)) {
            setOutput("Status", *child1_status);
            return;
        }

        auto child2_status = getInput<BTStatus>("Child 2 Status");
        if (child2_status) {
            setOutput("Status", *child2_status);
        }
        else {
            setOutput("Status", BTStatus::Success);
        }
    }

    namespace {
        struct BTSequenceNodeRegistrar {
            BTSequenceNodeRegistrar() {
                NodeDescriptor desc = { "BT Sequence", "AI/Behavior Tree", "Runs children in order. Fails if one fails." };
                NodeFactory::instance().registerNodeType("ai_bt_sequence", desc, []() { return std::make_unique<BTSequenceNode>(); });
            }
        };
    }
    static BTSequenceNodeRegistrar g_btSequenceRegistrar;

    // BTSelectorNode
    BTSelectorNode::BTSelectorNode() {
	m_id = "ai_bt_selector";
        m_ports.push_back({ "Parent", {"Execution", "trigger"}, Port::Direction::Input, this });
        m_ports.push_back({ "Child 1 Status", {"BTStatus", "enum"}, Port::Direction::Input, this });
        m_ports.push_back({ "Child 2 Status", {"BTStatus", "enum"}, Port::Direction::Input, this });
        m_ports.push_back({ "Status", {"BTStatus", "enum"}, Port::Direction::Output, this });
    }

    void BTSelectorNode::compute() {
        auto child1_status = getInput<BTStatus>("Child 1 Status");
        if (child1_status && (*child1_status == BTStatus::Success || *child1_status == BTStatus::Running)) {
            setOutput("Status", *child1_status);
            return;
        }

        auto child2_status = getInput<BTStatus>("Child 2 Status");
        if (child2_status) {
            setOutput("Status", *child2_status);
        }
        else {
            setOutput("Status", BTStatus::Failure);
        }
    }

    namespace {
        struct BTSelectorNodeRegistrar {
            BTSelectorNodeRegistrar() {
                NodeDescriptor desc = { "BT Selector", "AI/Behavior Tree", "Runs children in order. Succeeds if one succeeds." };
                NodeFactory::instance().registerNodeType("ai_bt_selector", desc, []() { return std::make_unique<BTSelectorNode>(); });
            }
        };
    }
    static BTSelectorNodeRegistrar g_btSelectorRegistrar;



QWidget* BTSelectorNode::createCustomWidget()
{
    // TODO: Implement custom widget for "BTSelectorNode"
    return nullptr;
}


QWidget* BTSequenceNode::createCustomWidget()
{
    // TODO: Implement custom widget for "BTSequenceNode"
    return nullptr;
}


QWidget* BTActionNode::createCustomWidget()
{
    // TODO: Implement custom widget for "BTActionNode"
    return nullptr;
}


QWidget* GetBlackboardFloatNode::createCustomWidget()
{
    // TODO: Implement custom widget for "GetBlackboardFloatNode"
    return nullptr;
}


QWidget* SetBlackboardFloatNode::createCustomWidget()
{
    // TODO: Implement custom widget for "SetBlackboardFloatNode"
    return nullptr;
}


QWidget* RunInferenceNode::createCustomWidget()
{
    // TODO: Implement custom widget for "RunInferenceNode"
    return nullptr;
}
} // namespace NodeLibrary
