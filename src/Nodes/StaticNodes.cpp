#include "StaticNodes.hpp"
#include <memory> // Required for std::make_unique

namespace NodeLibrary {

    // --- Helper Macro for Registration ---
    // This macro reduces boilerplate code when registering many similar static nodes.
    // FIX: The lambda now returns a std::unique_ptr via std::make_unique.
    // FIX: The registrar struct definition now correctly ends with a semicolon ';'.
    // FIX: The static instance is declared separately after the struct definition.
#define REGISTER_STATIC_NODE(NodeType, TypeName, Category, Description) \
    namespace { \
        struct NodeType##Registrar { \
            NodeType##Registrar() { \
                NodeDescriptor desc = { #TypeName, "Inputs/" #Category, Description }; \
                NodeFactory::instance().registerNodeType( \
                    "static_" #NodeType, \
                    desc, \
                    []() { return std::make_unique<NodeType>( #TypeName, "static_" #NodeType ); } \
                ); \
            } \
        }; \
    } \
    static NodeType##Registrar g_##NodeType##Registrar;

    // --- C++ Standard Library Type Registrations ---
    REGISTER_STATIC_NODE(StaticFloatNode, float, Static, "Outputs a constant float value.");
    REGISTER_STATIC_NODE(StaticDoubleNode, double, Static, "Outputs a constant double value.");
    REGISTER_STATIC_NODE(StaticIntNode, int, Static, "Outputs a constant integer value.");
    REGISTER_STATIC_NODE(StaticBoolNode, bool, Static, "Outputs a constant boolean value.");
    REGISTER_STATIC_NODE(StaticStringNode, std::string, Static, "Outputs a constant string value.");

    // --- GLM (OpenGL Mathematics) Library Type Registrations ---
    REGISTER_STATIC_NODE(StaticVec2Node, glm::vec2, GLM, "Outputs a constant 2D vector.");
    REGISTER_STATIC_NODE(StaticVec3Node, glm::vec3, GLM, "Outputs a constant 3D vector.");
    REGISTER_STATIC_NODE(StaticVec4Node, glm::vec4, GLM, "Outputs a constant 4D vector.");
    REGISTER_STATIC_NODE(StaticQuatNode, glm::quat, GLM, "Outputs a constant quaternion.");
    REGISTER_STATIC_NODE(StaticMat3Node, glm::mat3, GLM, "Outputs a constant 3x3 matrix.");
    REGISTER_STATIC_NODE(StaticMat4Node, glm::mat4, GLM, "Outputs a constant 4x4 matrix.");

    // --- Eigen Library Type Registrations ---
    REGISTER_STATIC_NODE(StaticEigenVec2fNode, Eigen::Vector2f, Eigen, "Outputs a constant Eigen 2D float vector.");
    REGISTER_STATIC_NODE(StaticEigenVec3fNode, Eigen::Vector3f, Eigen, "Outputs a constant Eigen 3D float vector.");
    REGISTER_STATIC_NODE(StaticEigenVec4fNode, Eigen::Vector4f, Eigen, "Outputs a constant Eigen 4D float vector.");
    REGISTER_STATIC_NODE(StaticEigenQuatfNode, Eigen::Quaternionf, Eigen, "Outputs a constant Eigen float quaternion.");
    REGISTER_STATIC_NODE(StaticEigenMat3fNode, Eigen::Matrix3f, Eigen, "Outputs a constant Eigen 3x3 float matrix.");
    REGISTER_STATIC_NODE(StaticEigenMat4fNode, Eigen::Matrix4f, Eigen, "Outputs a constant Eigen 4x4 float matrix.");

} // namespace NodeLibrary
