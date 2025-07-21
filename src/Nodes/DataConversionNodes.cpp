#include "DataConversionNodes.hpp"
#include <memory> // For std::make_unique

namespace NodeLibrary {

    // Define the possible conversions. This map is the single source of truth.
    const std::map<std::string, std::vector<std::string>> UniversalConverterNode::conversionMap = {
        {"glm::vec3", {"Eigen::Vector3f", "std::vector<float>"}},
        {"Eigen::Vector3f", {"glm::vec3", "std::vector<float>"}},
        {"std::vector<float>", {"glm::vec3", "Eigen::Vector3f"}}
        // Add many more conversions here following the same pattern
    };

    UniversalConverterNode::UniversalConverterNode() {
	m_id = "conversion_universal";
        // Start with default ports. The IDE will call setInput/OutputType to change this.
        rebuildPorts();
    }

    void UniversalConverterNode::rebuildPorts() {
        m_ports.clear(); // Remove all existing ports

        // FIX 1: The Port's 'type' member is now a 'DataType' struct, not a simple string.
        // It must be initialized with both a type name and a unit.
        m_ports.push_back({ "Trigger", {"bool", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Input", {m_inputType, "variant"}, Port::Direction::Input, this });
        m_ports.push_back({ "Output", {m_outputType, "variant"}, Port::Direction::Output, this });
    }

    void UniversalConverterNode::setInputType(const std::string& typeName) {
        m_inputType = typeName;
        // Auto-select a valid output if the current one is no longer valid
        auto possible = getPossibleConversions(m_inputType);
        bool currentOutputIsValid = false;
        for (const auto& p : possible) {
            if (p == m_outputType) {
                currentOutputIsValid = true;
                break;
            }
        }
        if (!currentOutputIsValid && !possible.empty()) {
            m_outputType = possible[0];
        }
        rebuildPorts();
    }

    void UniversalConverterNode::setOutputType(const std::string& typeName) {
        m_outputType = typeName;
        rebuildPorts();
    }

    std::vector<std::string> UniversalConverterNode::getPossibleConversions(const std::string& inputType) {
        auto it = conversionMap.find(inputType);
        if (it != conversionMap.end()) {
            return it->second;
        }
        return {};
    }

    void UniversalConverterNode::compute() {
        // This is the heart of the dynamic dispatch. It checks the current types
        // and calls the appropriate conversion function.
        if (m_inputType == "glm::vec3" && m_outputType == "Eigen::Vector3f") {
            auto input = getInput<glm::vec3>("Input");
            if (input) setOutput("Output", DataConversions::to_eigen(*input));
        }
        else if (m_inputType == "Eigen::Vector3f" && m_outputType == "glm::vec3") {
            auto input = getInput<Eigen::Vector3f>("Input");
            if (input) setOutput("Output", DataConversions::to_glm(*input));
        }
        else if (m_inputType == "std::vector<float>" && m_outputType == "glm::vec3") {
            auto input = getInput<std::vector<float>>("Input");
            if (input) setOutput("Output", DataConversions::std_vector_to_vec<3>(*input));
        }
        // ... add else-if branches for every possible conversion in the map ...
    }

    namespace {
        struct UniversalConverterRegistrar {
            UniversalConverterRegistrar() {
                NodeDescriptor desc = { "Universal Converter", "Utilities/Conversion", "Converts between different data types." };
                // FIX 2: The factory requires a lambda that returns a std::unique_ptr<Node>, not a raw pointer.
                // Use std::make_unique to create the node instance.
                NodeFactory::instance().registerNodeType("conversion_universal", desc, []() { return std::make_unique<UniversalConverterNode>(); });
            }
        }; // FIX 3: A struct definition must end with a semicolon.

        static UniversalConverterRegistrar g_universalConverterRegistrar;
    }

} // namespace NodeLibrary