#pragma once
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "components.hpp" // For ProfiledData
#include <cmath>
#include <optional>
#include <numeric> // For std::lerp in C++20, or a custom implementation

namespace NodeLibrary {

    // --- Arithmetic Nodes ---
    class AdditionNode : public Node { public: AdditionNode(); void compute() override; };
    class SubtractNode : public Node { public: SubtractNode(); void compute() override; };
    class MultiplyNode : public Node { public: MultiplyNode(); void compute() override; };
    class DivideNode : public Node { public: DivideNode(); void compute() override; };
    class ModuloNode : public Node { public: ModuloNode(); void compute() override; };

    // --- Trigonometry Nodes ---
    class SineNode : public Node { public: SineNode(); void compute() override; };
    class CosineNode : public Node { public: CosineNode(); void compute() override; };
    class TangentNode : public Node { public: TangentNode(); void compute() override; };
    class ArcTan2Node : public Node { public: ArcTan2Node(); void compute() override; };

    // --- Exponential & Power Nodes ---
    class PowerNode : public Node { public: PowerNode(); void compute() override; };
    class SqrtNode : public Node { public: SqrtNode(); void compute() override; };
    class LogNode : public Node { public: LogNode(); void compute() override; };

    // --- Comparison Nodes ---
    class GreaterThanNode : public Node { public: GreaterThanNode(); void compute() override; };
    class LessThanNode : public Node { public: LessThanNode(); void compute() override; };
    class EqualsNode : public Node { public: EqualsNode(); void compute() override; };

    // --- General Math Nodes ---
    class AbsNode : public Node { public: AbsNode(); void compute() override; };
    class ClampNode : public Node { public: ClampNode(); void compute() override; };
    class LerpNode : public Node { public: LerpNode(); void compute() override; }; // Linear Interpolation
    class MinNode : public Node { public: MinNode(); void compute() override; };
    class MaxNode : public Node { public: MaxNode(); void compute() override; };


    // --- Calculus Nodes ---
    class DerivativeNode : public Node {
    public:
        DerivativeNode();
        void compute() override;
    private:
        std::optional<float> m_last_value;
        std::optional<double> m_last_timestamp;
    };

    class IntegralNode : public Node {
    public:
        IntegralNode();
        void compute() override;
    private:
        std::optional<float> m_last_value;
        std::optional<double> m_last_timestamp;
        double m_integral_sum = 0.0;
    };

} // namespace NodeLibrary
