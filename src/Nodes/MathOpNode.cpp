// MathOpNode.cpp -- a two-input math node whose OPERATION is chosen by an in-node ENUM combo (the Phase-1
// combo-input path). A and B are float spin-box inputs; "Op" is an enum combo (Add/Subtract/Multiply/Divide)
// whose selection is read by compute() via getInput<int>("Op"). Doubles as the GATE COMBO-INPUT target: the
// combo selection genuinely changes the node's operation.
#include "Node.hpp"
#include "NodeFactory.hpp"

#include <cmath>
#include <memory>

namespace krs::nodes {
namespace {

class MathOpNode : public Node {
public:
    MathOpNode() {
        m_id = "math_op";
        m_ports.push_back({ "A", { "float", "unitless" }, Port::Direction::Input, this });
        m_ports.push_back({ "B", { "float", "unitless" }, Port::Direction::Input, this });
        addEnumInputPort("Op", { "Add", "Subtract", "Multiply", "Divide" });   // selection = int index
        m_ports.push_back({ "Result", { "float", "unitless" }, Port::Direction::Output, this });
    }
    void compute() override {
        const double a = getInputD("A", 0.0);
        const double b = getInputD("B", 0.0);
        const int op = getInput<int>("Op").value_or(0);   // the combo selection (0=Add default)
        double r;
        switch (op) {
        case 1:  r = a - b; break;
        case 2:  r = a * b; break;
        case 3:  r = (std::abs(b) > 1e-12) ? a / b : 0.0; break;
        default: r = a + b; break;   // 0 = Add
        }
        setOutput<float>("Result", float(r));
    }
};

struct MathOpRegistrar {
    MathOpRegistrar() {
        NodeFactory::instance().registerNodeType("math_op",
            { "Math Op", "Math/Arithmetic", "Combines A and B by the selected operation (Add/Subtract/Multiply/Divide)." },
            []() { return std::make_unique<MathOpNode>(); });
    }
};
static MathOpRegistrar g_mathOpRegistrar;

} // namespace
} // namespace krs::nodes
