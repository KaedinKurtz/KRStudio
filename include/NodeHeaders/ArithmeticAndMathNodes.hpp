#pragma once

#include <QWidget>
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "components.hpp" // For ProfiledData
#include <cmath>
#include <optional>
#include <numeric> // For std::lerp in C++20, or a custom implementation

namespace NodeLibrary {

    // --- Arithmetic Nodes ---
    class AdditionNode : public Node { public:
        QWidget* createCustomWidget() override; AdditionNode(); void compute() override; };
    class SubtractNode : public Node { public:
        QWidget* createCustomWidget() override; SubtractNode(); void compute() override; };
    class MultiplyNode : public Node { public:
        QWidget* createCustomWidget() override; MultiplyNode(); void compute() override; };
    class DivideNode : public Node { public:
        QWidget* createCustomWidget() override; DivideNode(); void compute() override; };
    class ModuloNode : public Node { public:
        QWidget* createCustomWidget() override; ModuloNode(); void compute() override; };

    // --- Trigonometry Nodes ---
    class SineNode : public Node { public:
        QWidget* createCustomWidget() override; SineNode(); void compute() override; };
    class CosineNode : public Node { public:
        QWidget* createCustomWidget() override; CosineNode(); void compute() override; };
    class TangentNode : public Node { public:
        QWidget* createCustomWidget() override; TangentNode(); void compute() override; };
    class ArcTan2Node : public Node { public:
        QWidget* createCustomWidget() override; ArcTan2Node(); void compute() override; };

    // --- Exponential & Power Nodes ---
    class PowerNode : public Node { public:
        QWidget* createCustomWidget() override; PowerNode(); void compute() override; };
    class SqrtNode : public Node { public:
        QWidget* createCustomWidget() override; SqrtNode(); void compute() override; };
    class LogNode : public Node { public:
        QWidget* createCustomWidget() override; LogNode(); void compute() override; };

    // --- Comparison Nodes ---
    class GreaterThanNode : public Node { public:
        QWidget* createCustomWidget() override; GreaterThanNode(); void compute() override; };
    class LessThanNode : public Node { public:
        QWidget* createCustomWidget() override; LessThanNode(); void compute() override; };
    class EqualsNode : public Node { public:
        QWidget* createCustomWidget() override; EqualsNode(); void compute() override; };

    // --- General Math Nodes ---
    class AbsNode : public Node { public:
        QWidget* createCustomWidget() override; AbsNode(); void compute() override; };
    class ClampNode : public Node { public:
        QWidget* createCustomWidget() override; ClampNode(); void compute() override; };
    class LerpNode : public Node { public:
        QWidget* createCustomWidget() override; LerpNode(); void compute() override; }; // Linear Interpolation
    class MinNode : public Node { public:
        QWidget* createCustomWidget() override; MinNode(); void compute() override; };
    class MaxNode : public Node { public:
        QWidget* createCustomWidget() override; MaxNode(); void compute() override; };


    // --- Calculus Nodes ---
    class DerivativeNode : public Node {
    public:
        QWidget* createCustomWidget() override;
        DerivativeNode();
        void compute() override;
    private:
        std::optional<float> m_last_value;
        std::optional<double> m_last_timestamp;
    };

    class IntegralNode : public Node {
    public:
        QWidget* createCustomWidget() override;
        IntegralNode();
        void compute() override;
    private:
        std::optional<float> m_last_value;
        std::optional<double> m_last_timestamp;
        double m_integral_sum = 0.0;
    };

} // namespace NodeLibrary
