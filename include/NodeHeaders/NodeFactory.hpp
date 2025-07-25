#pragma once

#include <QWidget>

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include "Node.hpp"

// The metadata for each node type, used by the IDE to build menus.
struct NodeDescriptor {
    std::string aui_name;       // The user-friendly name, e.g., "Add (float)"
    std::string category;     // The category for menus, e.g., "Math/Arithmetic"
    std::string description;  // A tooltip description.
};

/**
 * @brief A singleton factory for creating node instances from a string ID.
 * This class holds the central registry of all available node types.
 */
class NodeFactory {
public:
    // A function pointer that creates a new instance of a node.
    using NodeCreationFunc = std::function<std::unique_ptr<Node>()>;

    /**
     * @brief Access the singleton instance of the factory.
     */
    static NodeFactory& instance();

    /**
     * @brief Called by registrar objects at startup to add a node type to the registry.
     * @param type_id A unique internal string identifier for the node type.
     * @param desc The user-facing metadata for the node.
     * @param func The function that will create a new instance of this node.
     */
    void registerNodeType(const std::string& type_id, const NodeDescriptor& desc, NodeCreationFunc func);

    /**
     * @brief Creates a new node instance given its unique type ID.
     * @param type_id The internal string identifier of the node to create.
     * @return A unique_ptr to the new Node instance, or nullptr if the ID is not found.
     */
    std::unique_ptr<Node> createNode(const std::string& type_id);

    /**
     * @brief Returns the complete map of all registered node types and their descriptors.
     * The IDE uses this to populate its "Add Node" context menu.
     */
    const std::map<std::string, NodeDescriptor>& getRegisteredNodeTypes() const;

private:
    // Private constructor and deleted operators to enforce the singleton pattern.
    NodeFactory() = default;
    ~NodeFactory() = default;
    NodeFactory(const NodeFactory&) = delete;
    NodeFactory& operator=(const NodeFactory&) = delete;

    // The map from internal type ID to the function that creates the node.
    std::map<std::string, NodeCreationFunc> m_creation_registry;

    // The map from internal type ID to the node's user-facing metadata.
    std::map<std::string, NodeDescriptor> m_descriptor_registry;
};
