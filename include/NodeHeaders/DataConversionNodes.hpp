#pragma once

#include <QWidget>

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "DataConversion.hpp" // Your provided header
#include <string>
#include <vector>
#include <map>

namespace NodeLibrary {

    /**
     * @brief A dynamic node that converts between various data types.
     * In the IDE, this node would have two dropdowns (combo boxes) to select
     * the input and output types, which would dynamically change its ports.
     */
    class UniversalConverterNode : public Node {
    public:
        QWidget* createCustomWidget() override;
        UniversalConverterNode();
        void compute() override;

        // Called by the IDE when the user selects a new type from a dropdown.
        void setInputType(const std::string& typeName);
        void setOutputType(const std::string& typeName);

        // Provides the IDE with a list of possible conversions from a given type.
        static std::vector<std::string> getPossibleConversions(const std::string& inputType);

    private:
        void rebuildPorts(); // Helper to add/remove ports dynamically.

        std::string m_inputType = "glm::vec3";  // Default input
        std::string m_outputType = "Eigen::Vector3f"; // Default output

        // A map defining all valid conversion paths.
        static const std::map<std::string, std::vector<std::string>> conversionMap;
    };

} // namespace NodeLibrary
