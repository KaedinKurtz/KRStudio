#include "SignalProcessingNodes.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <iostream>
#include <cmath>
#include <memory> // Required for std::make_unique
#include <QFormLayout>
#include <QDoubleSpinBox>

namespace NodeLibrary {

    // --- Free Function Implementations ---
    // (Implementations remain the same)
    float applyLowPassFilter(float input, LowPassFilterState& state) {
        if (!state.initialized) {
            state.last_y = input;
            state.initialized = true;
            return input;
        }
        double y = state.alpha * input + (1.0 - state.alpha) * state.last_y;
        state.last_y = y;
        return static_cast<float>(y);
    }
    float generateWaveform(WaveformType type, float time, float frequency, float amplitude) {
        float t = time * frequency;
        switch (type) {
        case WaveformType::Sine:
            return std::sin(t * 2.0f * 3.14159f) * amplitude;
        case WaveformType::Square:
            return (std::fmod(t, 1.0f) > 0.5f ? 1.0f : -1.0f) * amplitude;
        case WaveformType::Sawtooth:
            return (std::fmod(t, 1.0f) * 2.0f - 1.0f) * amplitude;
        }
        return 0.0f;
    }

    // --- Node Implementations & Registrations ---

    // LowPassFilterNode
    LowPassFilterNode::LowPassFilterNode() {
	m_id = "signal_lowpass";
        // FIX: Use nested initializer {name, unit} for DataType
        m_ports.push_back({ "Input", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Alpha", {"float", "factor"}, Port::Direction::Input, this });
        m_ports.push_back({ "Output", {"float", "unitless"}, Port::Direction::Output, this });
    }

    void LowPassFilterNode::compute() {
        auto input = getInput<float>("Input");
        auto alpha = getInput<float>("Alpha");
        if (alpha) { // Prioritize connected alpha
            m_filterState.alpha = *alpha;
        }
        if (input) {
            setOutput("Output", applyLowPassFilter(*input, m_filterState));
        }
    }

    QWidget* LowPassFilterNode::createCustomWidget()
    {
        auto* widget = new QWidget();
        auto* layout = new QFormLayout(widget);
        layout->setContentsMargins(4, 4, 4, 4); // Add some padding

        auto* alphaSpinBox = new QDoubleSpinBox();
        alphaSpinBox->setRange(0.0, 1.0);
        alphaSpinBox->setDecimals(3);
        alphaSpinBox->setSingleStep(0.01);
        alphaSpinBox->setValue(m_filterState.alpha); // Set initial value from backend

        // Connect the UI control back to the node's logic
        QObject::connect(alphaSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double value) {
                this->m_filterState.alpha = value;

                // Trigger a re-computation when the value changes
                this->process();

                // You will likely need to add a signal here in your Node base class
                // that the NodeDelegate can connect to, in order to emit dataUpdated().
                // For example: Q_EMIT backendStateChanged();
            });

        layout->addRow("Alpha:", alphaSpinBox);
        widget->setLayout(layout);
        return widget;
    }

    namespace {
        struct LowPassFilterRegistrar {
            LowPassFilterRegistrar() {
                NodeDescriptor desc = { "Low Pass Filter", "Signal", "Smooths a noisy signal." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("signal_lowpass", desc, []() { return std::make_unique<LowPassFilterNode>(); });
            }
        };
    }
    static LowPassFilterRegistrar g_lowPassFilterRegistrar;

    // DotProductNode
    DotProductNode::DotProductNode() {
	m_id = "signal_dot_product";
        // FIX: Use nested initializer {name, unit} for DataType
        m_ports.push_back({ "A", {"glm::vec3", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "B", {"glm::vec3", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"float", "unitless"}, Port::Direction::Output, this });
    }

    void DotProductNode::compute() {
        auto a = getInput<glm::vec3>("A");
        auto b = getInput<glm::vec3>("B");
        if (a && b) {
            setOutput("Result", glm::dot(*a, *b));
        }
    }

    namespace {
        struct DotProductRegistrar {
            DotProductRegistrar() {
                NodeDescriptor desc = { "Dot Product", "Signal/Vector", "Calculates the dot product of two vectors." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("signal_dot_product", desc, []() { return std::make_unique<DotProductNode>(); });
            }
        };
    }
    static DotProductRegistrar g_dotProductRegistrar;

    // DecomposeTransformNode
    DecomposeTransformNode::DecomposeTransformNode() {
	m_id = "signal_decompose_transform";
        // FIX: Use nested initializer {name, unit} for DataType
        m_ports.push_back({ "Transform", {"glm::mat4", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "Translation", {"glm::vec3", "meters"}, Port::Direction::Output, this });
        m_ports.push_back({ "Rotation", {"glm::quat", "quaternion"}, Port::Direction::Output, this });
        m_ports.push_back({ "Scale", {"glm::vec3", "factor"}, Port::Direction::Output, this });
    }

    void DecomposeTransformNode::compute() {
        auto transform = getInput<glm::mat4>("Transform");
        if (transform) {
            glm::vec3 scale, translation, skew;
            glm::quat rotation;
            glm::vec4 perspective;
            glm::decompose(*transform, scale, rotation, translation, skew, perspective);
            setOutput("Translation", translation);
            setOutput("Rotation", rotation);
            setOutput("Scale", scale);
        }
    }

    namespace {
        struct DecomposeTransformRegistrar {
            DecomposeTransformRegistrar() {
                NodeDescriptor desc = { "Decompose Transform", "Signal/Transform", "Splits a 4x4 matrix into T, R, S." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("signal_decompose_transform", desc, []() { return std::make_unique<DecomposeTransformNode>(); });
            }
        };
    }
    static DecomposeTransformRegistrar g_decomposeTransformRegistrar;

    // ComposeTransformNode
    ComposeTransformNode::ComposeTransformNode() {
	m_id = "signal_compose_transform";
        // FIX: Use nested initializer {name, unit} for DataType
        m_ports.push_back({ "Translation", {"glm::vec3", "meters"}, Port::Direction::Input, this });
        m_ports.push_back({ "Rotation", {"glm::quat", "quaternion"}, Port::Direction::Input, this });
        m_ports.push_back({ "Scale", {"glm::vec3", "factor"}, Port::Direction::Input, this });
        m_ports.push_back({ "Transform", {"glm::mat4", "matrix"}, Port::Direction::Output, this });
    }

    void ComposeTransformNode::compute() {
        auto translation = getInput<glm::vec3>("Translation");
        auto rotation = getInput<glm::quat>("Rotation");
        auto scale = getInput<glm::vec3>("Scale");

        if (translation && rotation && scale) {
            glm::mat4 transMat = glm::translate(glm::mat4(1.0f), *translation);
            glm::mat4 rotMat = glm::mat4_cast(*rotation);
            glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), *scale);
            setOutput("Transform", transMat * rotMat * scaleMat);
        }
    }

    namespace {
        struct ComposeTransformRegistrar {
            ComposeTransformRegistrar() {
                NodeDescriptor desc = { "Compose Transform", "Signal/Transform", "Builds a 4x4 matrix from T, R, S." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("signal_compose_transform", desc, []() { return std::make_unique<ComposeTransformNode>(); });
            }
        };
    }
    static ComposeTransformRegistrar g_composeTransformRegistrar;

    // GenerateWaveformNode
    GenerateWaveformNode::GenerateWaveformNode() {
	m_id = "signal_waveform";
        // FIX: Use nested initializer {name, unit} for DataType
        m_ports.push_back({ "Time", {"float", "seconds"}, Port::Direction::Input, this });
        m_ports.push_back({ "Frequency", {"float", "hertz"}, Port::Direction::Input, this });
        m_ports.push_back({ "Amplitude", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Output", {"float", "unitless"}, Port::Direction::Output, this });
    }

    void GenerateWaveformNode::compute() {
        auto time = getInput<float>("Time");
        auto frequency = getInput<float>("Frequency");
        auto amplitude = getInput<float>("Amplitude");
        if (time && frequency && amplitude) {
            setOutput("Output", generateWaveform(this->waveformType, *time, *frequency, *amplitude));
        }
    }

    namespace {
        struct GenerateWaveformRegistrar {
            GenerateWaveformRegistrar() {
                NodeDescriptor desc = { "Generate Waveform", "Signal/Generators", "Generates a periodic signal." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("signal_waveform", desc, []() { return std::make_unique<GenerateWaveformNode>(); });
            }
        };
    }
    static GenerateWaveformRegistrar g_generateWaveformRegistrar;

} // namespace NodeLibrary