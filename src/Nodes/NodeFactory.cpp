#include "NodeFactory.hpp"

/**
 * @brief Access the singleton instance of the factory.
 * The static local variable ensures it's created only once.
 */
NodeFactory& NodeFactory::instance() {
    static NodeFactory factory;
    return factory;
}

/**
 * @brief Adds a new node type to the factory's registries.
 */
void NodeFactory::registerNodeType(const std::string& type_id, const NodeDescriptor& desc, NodeCreationFunc func) {
    m_creation_registry[type_id] = func;
    m_descriptor_registry[type_id] = desc;
}

/**
 * @brief Creates a new node instance by looking up its creation function in the registry.
 */
std::unique_ptr<Node> NodeFactory::createNode(const std::string& type_id) {
    auto it = m_creation_registry.find(type_id);
    if (it != m_creation_registry.end()) {
        return it->second(); // Execute the stored creation function.
    }
    return nullptr; // Return nullptr if the type ID is not registered.
}

/**
 * @brief Returns a copy of the descriptor map for the IDE to use.
 */
const std::map<std::string, NodeDescriptor>& NodeFactory::getRegisteredNodeTypes() const {
    return m_descriptor_registry;
}
