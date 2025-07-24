#pragma once

#include "Node.hpp"
#include "NodeFactory.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace NodeLibrary {

    // --- Data Structures & Enums ---

    struct LowPassFilterState {
        double alpha = 0.5; // Smoothing factor
        double last_y = 0.0;
        bool initialized = false;
    };

    enum class WaveformType { Sine, Square, Sawtooth };
    enum class NoiseType { Gaussian, Uniform };


    // --- Free Functions (to be wrapped by nodes) ---

    float applyLowPassFilter(float input, LowPassFilterState& state);
    float generateWaveform(WaveformType type, float time, float frequency, float amplitude);


    // --- Node Classes ---

    class LowPassFilterNode : public Node {
    public:
        QWidget* createCustomWidget() override;
        LowPassFilterNode();
        void compute() override;
    private:
        LowPassFilterState m_filterState; // Each node instance holds its own state
    };

    class DotProductNode : public Node {
    public:
        DotProductNode();
        void compute() override;
    };

    class DecomposeTransformNode : public Node {
    public:
        DecomposeTransformNode();
        void compute() override;
    };

    class ComposeTransformNode : public Node {
    public:
        ComposeTransformNode();
        void compute() override;
    };

    class GenerateWaveformNode : public Node {
    public:
        // These would be configured in the IDE per-instance
        WaveformType waveformType = WaveformType::Sine;
        GenerateWaveformNode();
        void compute() override;
    };

} // namespace NodeLibrary