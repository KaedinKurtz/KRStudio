#include "ArithmeticAndMathNodes.hpp"
#include <algorithm> // For std::min/max/clamp
#include <memory>    // Required for std::make_unique
#include <cmath>     // For std::abs, std::fmod, trig functions, etc.

namespace NodeLibrary {

    // --- Arithmetic Node Implementations & Registrations ---

    // AddNode
    AdditionNode::AdditionNode() {
	m_id = "math_add";
        m_ports.push_back({ "A", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "B", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void AdditionNode::compute() {
        auto a = getInput<float>("A");
        auto b = getInput<float>("B");
        if (a && b) setOutput("Result", *a + *b);
    }
    namespace {
        struct AdditionNodeRegistrar {
            AdditionNodeRegistrar() {
                NodeDescriptor desc = { "Add", "Math/Arithmetic", "Outputs the sum of A and B." };
                NodeFactory::instance().registerNodeType("math_add", desc, []() { return std::make_unique<AdditionNode>(); });
            }
        };
    }
    static AdditionNodeRegistrar g_addNodeRegistrar;

    // SubtractNode
    SubtractNode::SubtractNode() {
	m_id = "math_subtract";
        m_ports.push_back({ "A", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "B", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void SubtractNode::compute() {
        auto a = getInput<float>("A");
        auto b = getInput<float>("B");
        if (a && b) setOutput("Result", *a - *b);
    }
    namespace {
        struct SubtractNodeRegistrar {
            SubtractNodeRegistrar() {
                NodeDescriptor desc = { "Subtract", "Math/Arithmetic", "Outputs A - B." };
                NodeFactory::instance().registerNodeType("math_subtract", desc, []() { return std::make_unique<SubtractNode>(); });
            }
        };
    }
    static SubtractNodeRegistrar g_subtractNodeRegistrar;

    // MultiplyNode
    MultiplyNode::MultiplyNode() {
	m_id = "math_multiply";
        m_ports.push_back({ "A", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "B", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void MultiplyNode::compute() {
        auto a = getInput<float>("A");
        auto b = getInput<float>("B");
        if (a && b) setOutput("Result", *a * *b);
    }
    namespace {
        struct MultiplyNodeRegistrar {
            MultiplyNodeRegistrar() {
                NodeDescriptor desc = { "Multiply", "Math/Arithmetic", "Outputs A * B." };
                NodeFactory::instance().registerNodeType("math_multiply", desc, []() { return std::make_unique<MultiplyNode>(); });
            }
        };
    }
    static MultiplyNodeRegistrar g_multiplyNodeRegistrar;

    // DivideNode
    DivideNode::DivideNode() {
	m_id = "math_divide";
        m_ports.push_back({ "Numerator", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Denominator", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void DivideNode::compute() {
        auto a = getInput<float>("Numerator");
        auto b = getInput<float>("Denominator");
        if (a && b && std::abs(*b) > 1e-9f) setOutput("Result", *a / *b);
    }
    namespace {
        struct DivideNodeRegistrar {
            DivideNodeRegistrar() {
                NodeDescriptor desc = { "Divide", "Math/Arithmetic", "Outputs Numerator / Denominator." };
                NodeFactory::instance().registerNodeType("math_divide", desc, []() { return std::make_unique<DivideNode>(); });
            }
        };
    }
    static DivideNodeRegistrar g_divideNodeRegistrar;

    // ModuloNode
    ModuloNode::ModuloNode() {
	m_id = "math_modulo";
        m_ports.push_back({ "A", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "B", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void ModuloNode::compute() {
        auto a = getInput<float>("A");
        auto b = getInput<float>("B");
        if (a && b && std::abs(*b) > 1e-9f) setOutput("Result", std::fmod(*a, *b));
    }
    namespace {
        struct ModuloNodeRegistrar {
            ModuloNodeRegistrar() {
                NodeDescriptor desc = { "Modulo", "Math/Arithmetic", "Outputs the remainder of A / B." };
                NodeFactory::instance().registerNodeType("math_modulo", desc, []() { return std::make_unique<ModuloNode>(); });
            }
        };
    }
    static ModuloNodeRegistrar g_moduloNodeRegistrar;

    // --- Trigonometry Node Implementations ---

    SineNode::SineNode() {
	m_id = "math_sin";
        m_ports.push_back({ "Input (rad)", {"float", "radians"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void SineNode::compute() {
        auto a = getInput<float>("Input (rad)");
        if (a) setOutput("Result", std::sin(*a));
    }
    namespace {
        struct SineNodeRegistrar {
            SineNodeRegistrar() {
                NodeDescriptor desc = { "Sine", "Math/Trigonometry", "Outputs sin(Input)." };
                NodeFactory::instance().registerNodeType("math_sin", desc, []() { return std::make_unique<SineNode>(); });
            }
        };
    }
    static SineNodeRegistrar g_sinNodeRegistrar;

    CosineNode::CosineNode() {
	m_id = "math_cos";
        m_ports.push_back({ "Input (rad)", {"float", "radians"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void CosineNode::compute() {
        auto a = getInput<float>("Input (rad)");
        if (a) setOutput("Result", std::cos(*a));
    }
    namespace {
        struct CosineNodeRegistrar {
            CosineNodeRegistrar() {
                NodeDescriptor desc = { "Cosine", "Math/Trigonometry", "Outputs cos(Input)." };
                NodeFactory::instance().registerNodeType("math_cos", desc, []() { return std::make_unique<CosineNode>(); });
            }
        };
    }
    static CosineNodeRegistrar g_cosineNodeRegistrar;

    TangentNode::TangentNode() {
	m_id = "math_tan";
        m_ports.push_back({ "Input (rad)", {"float", "radians"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void TangentNode::compute() {
        auto a = getInput<float>("Input (rad)");
        if (a) setOutput("Result", std::tan(*a));
    }
    namespace {
        struct TangentNodeRegistrar {
            TangentNodeRegistrar() {
                NodeDescriptor desc = { "Tangent", "Math/Trigonometry", "Outputs tan(Input)." };
                NodeFactory::instance().registerNodeType("math_tan", desc, []() { return std::make_unique<TangentNode>(); });
            }
        };
    }
    static TangentNodeRegistrar g_tangentNodeRegistrar;

    ArcTan2Node::ArcTan2Node() {
	m_id = "math_atan2";
        m_ports.push_back({ "Y", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "X", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result (rad)", {"float", "radians"}, Port::Direction::Output, this });
    }
    void ArcTan2Node::compute() {
        auto y = getInput<float>("Y");
        auto x = getInput<float>("X");
        if (y && x) setOutput("Result (rad)", std::atan2(*y, *x));
    }
    namespace {
        struct ArcTan2NodeRegistrar {
            ArcTan2NodeRegistrar() {
                NodeDescriptor desc = { "ArcTan2", "Math/Trigonometry", "Outputs atan2(Y, X)." };
                NodeFactory::instance().registerNodeType("math_atan2", desc, []() { return std::make_unique<ArcTan2Node>(); });
            }
        };
    }
    static ArcTan2NodeRegistrar g_arcTan2NodeRegistrar;

    // --- Power & Exponential Nodes ---

    PowerNode::PowerNode() {
	m_id = "math_pow";
        m_ports.push_back({ "Base", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Exponent", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void PowerNode::compute() {
        auto b = getInput<float>("Base");
        auto e = getInput<float>("Exponent");
        if (b && e) setOutput("Result", std::pow(*b, *e));
    }
    namespace {
        struct PowerNodeRegistrar {
            PowerNodeRegistrar() {
                NodeDescriptor desc = { "Power", "Math/Power", "Outputs Base raised to the Exponent." };
                NodeFactory::instance().registerNodeType("math_pow", desc, []() { return std::make_unique<PowerNode>(); });
            }
        };
    }
    static PowerNodeRegistrar g_powerNodeRegistrar;

    SqrtNode::SqrtNode() {
	m_id = "math_sqrt";
        m_ports.push_back({ "Input", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void SqrtNode::compute() {
        auto a = getInput<float>("Input");
        if (a && *a >= 0.0f) setOutput("Result", std::sqrt(*a));
    }
    namespace {
        struct SqrtNodeRegistrar {
            SqrtNodeRegistrar() {
                NodeDescriptor desc = { "Square Root", "Math/Power", "Outputs the square root of the input." };
                NodeFactory::instance().registerNodeType("math_sqrt", desc, []() { return std::make_unique<SqrtNode>(); });
            }
        };
    }
    static SqrtNodeRegistrar g_sqrtNodeRegistrar;

    LogNode::LogNode() {
	m_id = "math_log";
        m_ports.push_back({ "Input", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void LogNode::compute() {
        auto a = getInput<float>("Input");
        if (a && *a > 0.0f) setOutput("Result", std::log(*a));
    }
    namespace {
        struct LogNodeRegistrar {
            LogNodeRegistrar() {
                NodeDescriptor desc = { "Natural Log", "Math/Power", "Outputs the natural logarithm of the input." };
                NodeFactory::instance().registerNodeType("math_log", desc, []() { return std::make_unique<LogNode>(); });
            }
        };
    }
    static LogNodeRegistrar g_logNodeRegistrar;

    // --- Comparison Nodes ---

    GreaterThanNode::GreaterThanNode() {
	m_id = "math_greater_than";
        m_ports.push_back({ "A", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "B", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"bool", "boolean"}, Port::Direction::Output, this });
    }
    void GreaterThanNode::compute() {
        auto a = getInput<float>("A");
        auto b = getInput<float>("B");
        if (a && b) setOutput("Result", *a > *b);
    }
    namespace {
        struct GreaterThanRegistrar {
            GreaterThanRegistrar() {
                NodeDescriptor desc = { "Greater Than", "Math/Comparison", "Outputs true if A > B." };
                NodeFactory::instance().registerNodeType("math_greater_than", desc, []() { return std::make_unique<GreaterThanNode>(); });
            }
        };
    }
    static GreaterThanRegistrar g_greaterThanRegistrar;

    LessThanNode::LessThanNode() {
	m_id = "math_less_than";
        m_ports.push_back({ "A", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "B", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"bool", "boolean"}, Port::Direction::Output, this });
    }
    void LessThanNode::compute() {
        auto a = getInput<float>("A");
        auto b = getInput<float>("B");
        if (a && b) setOutput("Result", *a < *b);
    }
    namespace {
        struct LessThanRegistrar {
            LessThanRegistrar() {
                NodeDescriptor desc = { "Less Than", "Math/Comparison", "Outputs true if A < B." };
                NodeFactory::instance().registerNodeType("math_less_than", desc, []() { return std::make_unique<LessThanNode>(); });
            }
        };
    }
    static LessThanRegistrar g_lessThanRegistrar;

    EqualsNode::EqualsNode() {
	m_id = "math_equals";
        m_ports.push_back({ "A", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "B", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Tolerance", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"bool", "boolean"}, Port::Direction::Output, this });
    }
    void EqualsNode::compute() {
        auto a = getInput<float>("A");
        auto b = getInput<float>("B");
        auto tol = getInput<float>("Tolerance");
        if (a && b) {
            float tolerance = tol.value_or(1e-9f);
            setOutput("Result", std::abs(*a - *b) <= tolerance);
        }
    }
    namespace {
        struct EqualsRegistrar {
            EqualsRegistrar() {
                NodeDescriptor desc = { "Equals", "Math/Comparison", "Outputs true if |A - B| <= Tolerance." };
                NodeFactory::instance().registerNodeType("math_equals", desc, []() { return std::make_unique<EqualsNode>(); });
            }
        };
    }
    static EqualsRegistrar g_equalsRegistrar;

    // --- General Math Nodes ---

    AbsNode::AbsNode() {
	m_id = "math_abs";
        m_ports.push_back({ "Input", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void AbsNode::compute() {
        auto a = getInput<float>("Input");
        if (a) setOutput("Result", std::abs(*a));
    }
    namespace {
        struct AbsRegistrar {
            AbsRegistrar() {
                NodeDescriptor desc = { "Absolute Value", "Math/General", "Outputs the absolute value of the input." };
                NodeFactory::instance().registerNodeType("math_abs", desc, []() { return std::make_unique<AbsNode>(); });
            }
        };
    }
    static AbsRegistrar g_absRegistrar;

    ClampNode::ClampNode() {
	m_id = "math_clamp";
        m_ports.push_back({ "Input", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Min", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Max", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void ClampNode::compute() {
        auto input = getInput<float>("Input");
        auto min = getInput<float>("Min");
        auto max = getInput<float>("Max");
        if (input && min && max) setOutput("Result", std::clamp(*input, *min, *max));
    }
    namespace {
        struct ClampRegistrar {
            ClampRegistrar() {
                NodeDescriptor desc = { "Clamp", "Math/Range", "Constrains Input to be within [Min, Max]." };
                NodeFactory::instance().registerNodeType("math_clamp", desc, []() { return std::make_unique<ClampNode>(); });
            }
        };
    }
    static ClampRegistrar g_clampRegistrar;

    LerpNode::LerpNode() {
	m_id = "math_lerp";
        m_ports.push_back({ "A", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "B", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Alpha", {"float", "factor"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void LerpNode::compute() {
        auto a = getInput<float>("A");
        auto b = getInput<float>("B");
        auto alpha = getInput<float>("Alpha");
        if (a && b && alpha) {
            float clampedAlpha = std::clamp(*alpha, 0.0f, 1.0f);
            setOutput("Result", (*a * (1.0f - clampedAlpha) + *b * clampedAlpha));
        }
    }
    namespace {
        struct LerpRegistrar {
            LerpRegistrar() {
                NodeDescriptor desc = { "Lerp", "Math/Range", "Linearly interpolates between A and B by Alpha." };
                NodeFactory::instance().registerNodeType("math_lerp", desc, []() { return std::make_unique<LerpNode>(); });
            }
        };
    }
    static LerpRegistrar g_lerpRegistrar;

    MinNode::MinNode() {
	m_id = "math_min";
        m_ports.push_back({ "A", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "B", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void MinNode::compute() {
        auto a = getInput<float>("A");
        auto b = getInput<float>("B");
        if (a && b) setOutput("Result", std::min(*a, *b));
    }
    namespace {
        struct MinRegistrar {
            MinRegistrar() {
                NodeDescriptor desc = { "Min", "Math/General", "Outputs the smaller of A and B." };
                NodeFactory::instance().registerNodeType("math_min", desc, []() { return std::make_unique<MinNode>(); });
            }
        };
    }
    static MinRegistrar g_minRegistrar;

    MaxNode::MaxNode() {
	m_id = "math_max";
        m_ports.push_back({ "A", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "B", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void MaxNode::compute() {
        auto a = getInput<float>("A");
        auto b = getInput<float>("B");
        if (a && b) setOutput("Result", std::max(*a, *b));
    }
    namespace {
        struct MaxRegistrar {
            MaxRegistrar() {
                NodeDescriptor desc = { "Max", "Math/General", "Outputs the larger of A and B." };
                NodeFactory::instance().registerNodeType("math_max", desc, []() { return std::make_unique<MaxNode>(); });
            }
        };
    }
    static MaxRegistrar g_maxRegistrar;

    // --- Calculus Node Implementations ---

    DerivativeNode::DerivativeNode() {
	m_id = "math_derivative";
        m_ports.push_back({ "Value", {"ProfiledData<float>", "data_packet"}, Port::Direction::Input, this });
        m_ports.push_back({ "dv/dt", {"float", "units/sec"}, Port::Direction::Output, this });
    }
    void DerivativeNode::compute() {
        auto input_data_ptr = getInput<std::shared_ptr<const ProfiledData<float> > >("Value");
        if (input_data_ptr) {
            const auto& input_data = *input_data_ptr;
            if (m_last_value && m_last_timestamp) {
                double dt = input_data->publication_timestamp - *m_last_timestamp;
                if (dt > 1e-9) {
                    float dv = input_data->value - *m_last_value;
                    setOutput("dv/dt", dv / static_cast<float>(dt));
                }
            }
            m_last_value = input_data->value;
            m_last_timestamp = input_data->publication_timestamp;
        }
    }
    namespace {
        struct DerivativeNodeRegistrar {
            DerivativeNodeRegistrar() {
                NodeDescriptor desc = { "Derivative", "Math/Calculus", "Calculates rate of change of an input." };
                NodeFactory::instance().registerNodeType("math_derivative", desc, []() { return std::make_unique<DerivativeNode>(); });
            }
        };
    }
    static DerivativeNodeRegistrar g_derivativeNodeRegistrar;

    IntegralNode::IntegralNode() {
	m_id = "math_integral";
        m_ports.push_back({ "Value", {"ProfiledData<float>", "data_packet"}, Port::Direction::Input, this });
        m_ports.push_back({ "Reset", {"bool", "trigger"}, Port::Direction::Input, this });
        m_ports.push_back({ "Integral", {"float", "units*sec"}, Port::Direction::Output, this });
    }
    void IntegralNode::compute() {
        auto reset = getInput<bool>("Reset");
        if (reset && *reset) {
            m_integral_sum = 0.0;
            m_last_value.reset();
            m_last_timestamp.reset();
        }

        auto input_data_ptr = getInput<std::shared_ptr<const ProfiledData<float>>>("Value");
        if (input_data_ptr) {
            const auto& input_data = *input_data_ptr;
            if (m_last_timestamp) {
                double dt = input_data->publication_timestamp - *m_last_timestamp;
                if (dt > 1e-9) {
                    m_integral_sum += (input_data->value + m_last_value.value_or(input_data->value)) * 0.5 * dt;
                }
            }
            m_last_value = input_data->value;
            m_last_timestamp = input_data->publication_timestamp;
            setOutput("Integral", static_cast<float>(m_integral_sum));
        }
    }
    namespace {
        struct IntegralNodeRegistrar {
            IntegralNodeRegistrar() {
                NodeDescriptor desc = { "Integral", "Math/Calculus", "Accumulates an input signal over time." };
                NodeFactory::instance().registerNodeType("math_integral", desc, []() { return std::make_unique<IntegralNode>(); });
            }
        };
    }
    static IntegralNodeRegistrar g_integralNodeRegistrar;

} // namespace NodeLibrary
