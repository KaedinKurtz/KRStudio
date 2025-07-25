#pragma once

#include <QWidget>

#include "Node.hpp"
#include "NodeFactory.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <Eigen/Dense>
#include <string>
#include <vector>

namespace NodeLibrary {

    /**
     * @brief A generic template for a node that outputs a constant, user-editable value.
     * @tparam T The data type of the value.
     */
    template<typename T>
    class StaticNode : public Node {
    public:
        QWidget* createCustomWidget() override {
            // TODO: Implement a custom widget for StaticNode.
            // This widget would contain a control (e.g., QDoubleSpinBox, QLineEdit)
            // to edit the 'value' member variable.
            return nullptr;
        }

        T value{};

        // MODIFIED CONSTRUCTOR:
        // It now accepts the ID string and assigns it to m_id.
        StaticNode(const std::string& typeName, const std::string& id) {
            m_id = id; // Assign the unique ID

            m_ports.clear();
            m_ports.push_back({ "Value", {typeName, "value"}, Port::Direction::Output, this });
        }

        void compute() override {
            setOutput("Value", this->value);
        }
    };

    // --- C++ Standard Library Types ---
    using StaticFloatNode = StaticNode<float>;
    using StaticDoubleNode = StaticNode<double>;
    using StaticIntNode = StaticNode<int>;
    using StaticBoolNode = StaticNode<bool>;
    using StaticStringNode = StaticNode<std::string>;

    // --- GLM (OpenGL Mathematics) Library Types ---
    using StaticVec2Node = StaticNode<glm::vec2>;
    using StaticVec3Node = StaticNode<glm::vec3>;
    using StaticVec4Node = StaticNode<glm::vec4>;
    using StaticQuatNode = StaticNode<glm::quat>;
    using StaticMat3Node = StaticNode<glm::mat3>;
    using StaticMat4Node = StaticNode<glm::mat4>;

    // --- Eigen Library Types ---
    using StaticEigenVec2fNode = StaticNode<Eigen::Vector2f>;
    using StaticEigenVec3fNode = StaticNode<Eigen::Vector3f>;
    using StaticEigenVec4fNode = StaticNode<Eigen::Vector4f>;
    using StaticEigenQuatfNode = StaticNode<Eigen::Quaternionf>;
    using StaticEigenMat3fNode = StaticNode<Eigen::Matrix3f>;
    using StaticEigenMat4fNode = StaticNode<Eigen::Matrix4f>;

} // namespace NodeLibrary
