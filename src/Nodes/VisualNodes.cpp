#include "VisualNodes.hpp"
#include <iostream> 
#include <algorithm> // Required for std::clamp
#include <memory>    // Required for std::make_unique

namespace NodeLibrary {

    // --- Visualization Node Implementations ---

    // ConditionalLightNode
    ConditionalLightNode::ConditionalLightNode() {
	m_id = "viz_conditional_light";
        // FIX: Use nested initializer for DataType
        m_ports.push_back({ "Input", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Color", {"glm::vec4", "rgba"}, Port::Direction::Output, this });
    }
    void ConditionalLightNode::compute() {
        auto input = getInput<float>("Input");
        if (input) {
            for (const auto& rule : conditions) {
                if (rule.condition && rule.condition(*input)) {
                    setOutput("Color", rule.color);
                    return; // First matching rule wins
                }
            }
        }
        setOutput("Color", defaultColor); // No rule matched
    }
    namespace {
        // FIX: Added semicolon after struct definition
        struct ConditionalLightRegistrar {
            ConditionalLightRegistrar() {
                NodeDescriptor desc = { "Conditional Light", "Visualization", "Displays a color based on input conditions." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("viz_conditional_light", desc, []() { return std::make_unique<ConditionalLightNode>(); });
            }
        };
    } // namespace
    // FIX: Declared static instance on its own line
    static ConditionalLightRegistrar g_conditionalLightRegistrar;


    // DialGaugeNode
    DialGaugeNode::DialGaugeNode() {
	m_id = "viz_dial_gauge";
        // FIX: Use nested initializer for DataType
        m_ports.push_back({ "Value", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Min", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Max", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Normalized", {"float", "normalized"}, Port::Direction::Output, this });
    }
    void DialGaugeNode::compute() {
        auto val = getInput<float>("Value");
        auto min = getInput<float>("Min");
        auto max = getInput<float>("Max");
        if (val && min && max && (*max > *min)) {
            float normalized = (*val - *min) / (*max - *min);
            setOutput("Normalized", std::clamp(normalized, 0.0f, 1.0f));
        }
    }
    namespace {
        // FIX: Added semicolon after struct definition
        struct DialGaugeRegistrar {
            DialGaugeRegistrar() {
                NodeDescriptor desc = { "Dial Gauge", "Visualization", "Visualizes a value in a min/max range." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("viz_dial_gauge", desc, []() { return std::make_unique<DialGaugeNode>(); });
            }
        };
    } // namespace
    // FIX: Declared static instance on its own line
    static DialGaugeRegistrar g_dialGaugeRegistrar;


    // ValuePlotterNode
    ValuePlotterNode::ValuePlotterNode() {
	m_id = "viz_plotter";
        // FIX: Use nested initializer for DataType
        m_ports.push_back({ "Time", {"float", "seconds"}, Port::Direction::Input, this });
        m_ports.push_back({ "Value", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "History", {"std::deque<std::pair<float, float>>", "timeseries"}, Port::Direction::Output, this });
    }
    void ValuePlotterNode::compute() {
        auto time = getInput<float>("Time");
        auto value = getInput<float>("Value");
        if (time && value) {
            m_history.push_back({ *time, *value });
            if (m_history.size() > MAX_HISTORY) {
                m_history.pop_front();
            }
            setOutput("History", m_history);
        }
    }
    namespace {
        // FIX: Added semicolon after struct definition
        struct ValuePlotterRegistrar {
            ValuePlotterRegistrar() {
                NodeDescriptor desc = { "Value Plotter", "Visualization", "Plots a value over time." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("viz_plotter", desc, []() { return std::make_unique<ValuePlotterNode>(); });
            }
        };
    } // namespace
    // FIX: Declared static instance on its own line
    static ValuePlotterRegistrar g_valuePlotterRegistrar;


    // --- Monitoring & Logging Node Implementations ---

    // DataMonitorNode
    DataMonitorNode::DataMonitorNode() {
	m_id = "mon_data_monitor";
        // FIX: Use nested initializer for DataType
        m_ports.push_back({ "Data In", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Data Out", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void DataMonitorNode::compute() {
        auto input = getInput<float>("Data In");
        if (input) {
            if (errorCondition && errorCondition(*input)) {
                // In a real engine, this would push to a global ErrorLogManager service.
                std::cerr << "[ERROR LOG] Node '" << getId() << "': " << errorMessage
                    << " (Value: " << *input << ")\n";
            }
            // Always pass the data through, regardless of the error state.
            setOutput("Data Out", *input);
        }
    }
    namespace {
        // FIX: Added semicolon after struct definition
        struct DataMonitorRegistrar {
            DataMonitorRegistrar() {
                NodeDescriptor desc = { "Data Monitor", "Monitoring", "Logs an error if a condition is met." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("mon_data_monitor", desc, []() { return std::make_unique<DataMonitorNode>(); });
            }
        };
    } // namespace
    // FIX: Declared static instance on its own line
    static DataMonitorRegistrar g_dataMonitorRegistrar;



QWidget* DataMonitorNode::createCustomWidget()
{
    // TODO: Implement custom widget for "DataMonitorNode"
    return nullptr;
}


QWidget* ValuePlotterNode::createCustomWidget()
{
    // TODO: Implement custom widget for "ValuePlotterNode"
    return nullptr;
}


QWidget* DialGaugeNode::createCustomWidget()
{
    // TODO: Implement custom widget for "DialGaugeNode"
    return nullptr;
}


QWidget* ConditionalLightNode::createCustomWidget()
{
    // TODO: Implement custom widget for "ConditionalLightNode"
    return nullptr;
}
} // namespace NodeLibrary