#include "UtilityNodes.hpp"
#include <iostream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <deque>
#include <memory> // Required for std::make_unique

namespace NodeLibrary {

    // RerouteNode
    RerouteNode::RerouteNode() {
	m_id = "util_reroute"; setDataType({ "std::any", "wildcard" }); }
    void RerouteNode::compute() {
        // The input port is at index 1 (after the default "Trigger" port)
        if (m_ports.size() > 1 && m_ports[1].packet.has_value()) {
            setOutput("Output", m_ports[1].packet->data);
        }
    }
    void RerouteNode::setDataType(const DataType& type) {
        m_ports.clear();
        m_ports.push_back({ "Trigger", {"bool", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Input", type, Port::Direction::Input, this });
        m_ports.push_back({ "Output", type, Port::Direction::Output, this });
    }
    namespace {
        struct RerouteRegistrar {
            RerouteRegistrar() {
                NodeDescriptor desc = { "Reroute", "Utilities/Graph", "A passthrough node for organizing connections." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("util_reroute", desc, []() { return std::make_unique<RerouteNode>(); });
            }
        };
    }
    static RerouteRegistrar g_rerouteRegistrar;

    // CommentNode
    CommentNode::CommentNode() {
	m_id = "util_comment"; m_ports.clear(); }
    void CommentNode::compute() { /* Does nothing */ }
    namespace {
        struct CommentRegistrar {
            CommentRegistrar() {
                NodeDescriptor desc = { "Comment Box", "Utilities/Graph", "A non-functional box for adding comments." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("util_comment", desc, []() { return std::make_unique<CommentNode>(); });
            }
        };
    }
    static CommentRegistrar g_commentRegistrar;

    // SwitchNode
    SwitchNode::SwitchNode() {
	m_id = "util_switch";
        m_ports.push_back({ "Condition", {"bool", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "If True", {"std::any", "wildcard"}, Port::Direction::Input, this });
        m_ports.push_back({ "If False", {"std::any", "wildcard"}, Port::Direction::Input, this });
        m_ports.push_back({ "Output", {"std::any", "wildcard"}, Port::Direction::Output, this });
    }
    void SwitchNode::compute() {
        auto condition = getInput<bool>("Condition");
        if (condition) { // Only proceed if condition is available
            if (*condition) {
                auto ifTrue = getInput<std::any>("If True");
                if (ifTrue) setOutput("Output", *ifTrue);
            }
            else {
                auto ifFalse = getInput<std::any>("If False");
                if (ifFalse) setOutput("Output", *ifFalse);
            }
        }
    }
    namespace {
        struct SwitchRegistrar {
            SwitchRegistrar() {
                NodeDescriptor desc = { "Switch (If/Else)", "Utilities/Logic", "Outputs one of two values based on a condition." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("util_switch", desc, []() { return std::make_unique<SwitchNode>(); });
            }
        };
    }
    static SwitchRegistrar g_switchRegistrar;

    // SwitchCaseNode
    SwitchCaseNode::SwitchCaseNode() {
	m_id = "util_switch_case";
        m_ports.push_back({ "Selector", {"int", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Default", {"std::any", "wildcard"}, Port::Direction::Input, this });
        m_ports.push_back({ "Output", {"std::any", "wildcard"}, Port::Direction::Output, this });
    }
    void SwitchCaseNode::addCase(int caseValue) {
        if (std::find(m_cases.begin(), m_cases.end(), caseValue) == m_cases.end()) {
            m_cases.push_back(caseValue);
            m_ports.push_back({ "Case " + std::to_string(caseValue), {"std::any", "wildcard"}, Port::Direction::Input, this });
        }
    }
    void SwitchCaseNode::removeCase(int caseValue) {
        auto it = std::find(m_cases.begin(), m_cases.end(), caseValue);
        if (it != m_cases.end()) {
            m_cases.erase(it);
            std::string portName = "Case " + std::to_string(caseValue);
            m_ports.erase(std::remove_if(m_ports.begin(), m_ports.end(),
                [&](const Port& p) { return p.name == portName; }), m_ports.end());
        }
    }
    void SwitchCaseNode::compute() {
        auto selector = getInput<int>("Selector");
        if (!selector) {
            auto defaultCase = getInput<std::any>("Default");
            if (defaultCase) setOutput("Output", *defaultCase);
            return;
        }

        auto it = std::find(m_cases.begin(), m_cases.end(), *selector);
        if (it != m_cases.end()) {
            auto caseInput = getInput<std::any>("Case " + std::to_string(*selector));
            if (caseInput) {
                setOutput("Output", *caseInput);
                return;
            }
        }

        auto defaultCase = getInput<std::any>("Default");
        if (defaultCase) setOutput("Output", *defaultCase);
    }
    namespace {
        struct SwitchCaseRegistrar {
            SwitchCaseRegistrar() {
                NodeDescriptor desc = { "Switch (Case)", "Utilities/Logic", "Selects an output based on an integer selector." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("util_switch_case", desc, []() { return std::make_unique<SwitchCaseNode>(); });
            }
        };
    }
    static SwitchCaseRegistrar g_switchCaseRegistrar;

    // CustomScriptNode
    CustomScriptNode::CustomScriptNode() {
	m_id = "util_script"; parseAndRebuildPorts(); }
    void CustomScriptNode::compute() {}
    void CustomScriptNode::parseAndRebuildPorts() {
        m_ports.clear();
        m_ports.push_back({ "Trigger", {"bool", "unitless"}, Port::Direction::Input, this });
        std::regex port_regex(R"(\b(in|out)\s+([a-zA-Z0-9_:]+)(?:\s*\(([^)]+)\))?\s+([a-zA-Z0-9_]+);)");
        std::istringstream stream(scriptContent);
        std::string line;

        while (std::getline(stream, line)) {
            std::smatch match;
            if (std::regex_search(line, match, port_regex)) {
                std::string dir_str = match[1].str();
                std::string type_str = match[2].str();
                std::string unit_str = match[3].str().empty() ? "unitless" : match[3].str();
                std::string name_str = match[4].str();
                Port::Direction dir = (dir_str == "in") ? Port::Direction::Input : Port::Direction::Output;
                m_ports.push_back({ name_str, {type_str, unit_str}, dir, this });
            }
        }
    }
    namespace {
        struct CustomScriptRegistrar {
            CustomScriptRegistrar() {
                NodeDescriptor desc = { "Custom Script", "Utilities/Scripting", "Executes user-defined code." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("util_script", desc, []() { return std::make_unique<CustomScriptNode>(); });
            }
        };
    }
    static CustomScriptRegistrar g_customScriptRegistrar;

    // LatchNode
    LatchNode::LatchNode() {
	m_id = "util_latch";
        m_ports.push_back({ "Data In", {"std::any", "wildcard"}, Port::Direction::Input, this });
        m_ports.push_back({ "Store", {"bool", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Data Out", {"std::any", "wildcard"}, Port::Direction::Output, this });
    }
    void LatchNode::compute() {
        auto store = getInput<bool>("Store");
        bool rising_edge = store && *store && !m_lastLatchState;
        m_lastLatchState = store.value_or(false);

        if (rising_edge) {
            auto dataIn = getInput<std::any>("Data In");
            if (dataIn) {
                m_latchedData = *dataIn;
            }
        }

        if (m_latchedData.has_value()) {
            setOutput("Data Out", m_latchedData.value());
        }
    }
    namespace {
        struct LatchRegistrar {
            LatchRegistrar() {
                NodeDescriptor desc = { "Latch (Sample & Hold)", "Utilities/Logic", "Stores a value on a trigger and holds it." };
                NodeFactory::instance().registerNodeType("util_latch", desc, []() { return std::make_unique<LatchNode>(); });
            }
        };
    }
    static LatchRegistrar g_latchRegistrar;

    // DelayNode
    DelayNode::DelayNode() {
	m_id = "util_delay";
        m_ports.push_back({ "Data In", {"std::any", "wildcard"}, Port::Direction::Input, this });
        m_ports.push_back({ "Delay (s)", {"float", "seconds"}, Port::Direction::Input, this });
        m_ports.push_back({ "Time", {"float", "seconds"}, Port::Direction::Input, this });
        m_ports.push_back({ "Data Out", {"std::any", "wildcard"}, Port::Direction::Output, this });
    }
    void DelayNode::compute() {
        auto time = getInput<float>("Time");
        if (!time) return; // Cannot do anything without time

        // Check if new data has arrived
        if (m_ports[1].isFresh) {
            auto dataIn = getInput<std::any>("Data In");
            auto delay = getInput<float>("Delay (s)");
            if (dataIn && delay) {
                m_buffer.push_back({ *time + *delay, *dataIn });
            }
        }

        // Check the front of the queue
        if (!m_buffer.empty() && *time >= m_buffer.front().release_time) {
            setOutput("Data Out", m_buffer.front().data);
            m_buffer.pop_front();
        }
    }
    namespace {
        struct DelayRegistrar {
            DelayRegistrar() {
                NodeDescriptor desc = { "Delay", "Utilities/Timing", "Delays a data packet by a specified time." };
                NodeFactory::instance().registerNodeType("util_delay", desc, []() { return std::make_unique<DelayNode>(); });
            }
        };
    }
    static DelayRegistrar g_delayRegistrar;

} // namespace NodeLibrary