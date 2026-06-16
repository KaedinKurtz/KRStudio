#pragma once

#include <QWidget>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QHBoxLayout>

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "NodeEditQueue.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <Eigen/Dense>
#include <string>
#include <vector>
#include <type_traits>

namespace NodeLibrary {

    /**
     * @brief A generic template for a node that outputs a constant, user-editable value.
     * @tparam T The data type of the value.
     */
    template<typename T>
    class StaticNode : public Node {
    public:
        // A real value editor bound to `value` for the clean scalar/vector/string constants (the math ones).
        // Editing it posts to the NodeEditQueue (consistent with the rest of the node UI), sets `value`, and
        // re-evaluates so the constant the node emits is the one shown. Matrix/quat/Eigen constants stay
        // field-less for now (no simple single-field editor). Controls are tagged krs_static_value for gating.
        QWidget* createCustomWidget() override {
            auto* node = this;
            if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
                auto* sb = new QDoubleSpinBox(); sb->setRange(-1.0e9, 1.0e9); sb->setDecimals(4); sb->setSingleStep(0.1);
                sb->setValue(double(value)); sb->setProperty("krs_static_value", true);
                QObject::connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [node](double v) {
                    krs::nodes::NodeEditQueue::instance().post(node, "static", [node, v]{ node->value = T(v); node->process(); }); });
                return sb;
            } else if constexpr (std::is_same_v<T, int>) {
                auto* sb = new QSpinBox(); sb->setRange(-1000000, 1000000); sb->setValue(int(value));
                sb->setProperty("krs_static_value", true);
                QObject::connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), [node](int v) {
                    krs::nodes::NodeEditQueue::instance().post(node, "static", [node, v]{ node->value = v; node->process(); }); });
                return sb;
            } else if constexpr (std::is_same_v<T, bool>) {
                auto* cb = new QCheckBox(); cb->setChecked(bool(value)); cb->setProperty("krs_static_value", true);
                QObject::connect(cb, &QCheckBox::toggled, [node](bool v) {
                    krs::nodes::NodeEditQueue::instance().post(node, "static", [node, v]{ node->value = v; node->process(); }); });
                return cb;
            } else if constexpr (std::is_same_v<T, std::string>) {
                auto* le = new QLineEdit(); le->setText(QString::fromStdString(value)); le->setProperty("krs_static_value", true);
                QObject::connect(le, &QLineEdit::textChanged, [node](const QString& s) {
                    krs::nodes::NodeEditQueue::instance().post(node, "static", [node, str = s.toStdString()]{ node->value = str; node->process(); }); });
                return le;
            } else if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>) {
                constexpr int N = std::is_same_v<T, glm::vec2> ? 2 : (std::is_same_v<T, glm::vec3> ? 3 : 4);
                auto* w = new QWidget(); auto* l = new QHBoxLayout(w); l->setContentsMargins(0, 0, 0, 0); l->setSpacing(2);
                for (int k = 0; k < N; ++k) {
                    auto* sb = new QDoubleSpinBox(); sb->setRange(-1.0e9, 1.0e9); sb->setDecimals(3); sb->setValue(double(value[k]));
                    if (k == 0) sb->setProperty("krs_static_value", true);
                    QObject::connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [node, k](double v) {
                        krs::nodes::NodeEditQueue::instance().post(node, "static", [node, k, v]{ node->value[k] = float(v); node->process(); }); });
                    l->addWidget(sb);
                }
                return w;
            }
            return nullptr;   // matrices / quaternions / Eigen constants: no single-field editor yet
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
