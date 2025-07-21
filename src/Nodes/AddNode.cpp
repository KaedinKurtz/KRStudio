#include "AddNode.hpp"
#include "NodeFactory.hpp" // Include the factory to register with it

namespace NodeLibrary {

    AddNode::AddNode() {
        m_ports.push_back({ "A", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "B", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"float", "unitless"}, Port::Direction::Output, this });
    }

    void AddNode::compute() {
        auto a = getInput<float>("A");
        auto b = getInput<float>("B");
        if (a && b) {
            setOutput("Result", *a + *b);
        }
    }

    // --- The Magic of Self-Registration ---

    /**
     * @brief A helper struct whose only purpose is to register the AddNode.
     */
    struct AddNodeRegistrar {
        AddNodeRegistrar() {
            NodeDescriptor desc = {
                "Add",                          // User-facing name
                "Math/Arithmetic",              // Category in the IDE menu
                "Outputs the sum of A and B."   // Tooltip description
            };
            // Call the factory singleton to register this node type.
            NodeFactory::instance().registerNodeType(
                "math_add",                                     // Unique internal ID
                desc,                                           // The metadata
                []() { return std::make_unique<AddNode>(); }      // A lambda that creates a new instance
            );
        }
    };

    /**
     * @brief A global static instance of the registrar.
     * C++ guarantees that the constructor for this object will be called before main() starts.
     * This is what populates the factory at runtime. Simply by linking this .cpp file
     * into your final executable, the AddNode becomes available to the factory.
     */
    static AddNodeRegistrar g_addNodeRegistrar;

} // namespace NodeLibrary
