// GeneratorNodes.cpp -- Phase 3.2 + Phase 1.3: proper param-driven signal generators with IN-NODE dial
// UIs. The pre-existing signal_waveform reads time/freq/amp from ports with an internal type and no
// phase/offset; these add distinct sine/square/triangle/saw nodes whose freq/amp/phase/offset are PARAMS
// driven by in-node dials (the NodeWidgets framework). Each emits out(t) = amp*wave(2pi*freq*t+phase)+off
// from a single time input -- gated against the analytic waveform in NodeLibraryGate (NODE-LIB / NODE-UI).

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "NodeWidgets.hpp"

#include <QWidget>
#include <cmath>
#include <memory>
#include <string>

namespace NodeLibrary {

class ParamSignalGenNode : public Node {
public:
    enum Wave { Sine, Square, Triangle, Saw };
    ParamSignalGenNode(Wave w, std::string id) : m_wave(w) {
        m_id = std::move(id);
        m_ports.push_back({ "t",   {"double","s"},        Port::Direction::Input,  this });
        m_ports.push_back({ "Out", {"double","unitless"}, Port::Direction::Output, this });
        setParam<double>("freq", 1.0); setParam<double>("amp", 1.0);
        setParam<double>("phase", 0.0); setParam<double>("offset", 0.0);
    }
    QWidget* createCustomWidget() override {
        using krs::nodeui::ControlSpec;
        return krs::nodeui::buildControlWidget(this, {
            { ControlSpec::Dial,   "Freq",  "freq",   0.0, 10.0, 0.01, 1.0 },
            { ControlSpec::Dial,   "Amp",   "amp",    0.0,  5.0, 0.01, 1.0 },
            { ControlSpec::Slider, "Phase", "phase", -3.14159265, 3.14159265, 0.01, 0.0 } });
    }
    void compute() override {
        constexpr double TAU = 6.283185307179586;
        const double t = getInputD("t", 0.0);
        const double f = getParam<double>("freq", 1.0), a = getParam<double>("amp", 1.0);
        const double ph = getParam<double>("phase", 0.0), off = getParam<double>("offset", 0.0);
        const double x = TAU * f * t + ph;
        double w = 0.0;
        switch (m_wave) {
            case Sine:     w = std::sin(x); break;
            case Square:   w = std::sin(x) >= 0.0 ? 1.0 : -1.0; break;
            case Triangle: { double p = x / TAU; p -= std::floor(p); w = 4.0 * std::abs(p - 0.5) - 1.0; break; }
            case Saw:      { double p = x / TAU; p -= std::floor(p); w = 2.0 * p - 1.0; break; }
        }
        setOutput<double>("Out", a * w + off);
    }
private:
    Wave m_wave;
};

// affine map out = in*gain + offset (double; in-node sliders). Used as the "potentiometer offset" stage
// of an all-double canvas chain (gen -> affine -> joint command).
class AffineNode : public Node {
public:
    AffineNode() {
        m_id = "math_affine";
        m_ports.push_back({ "In",  {"double","unitless"}, Port::Direction::Input,  this });
        m_ports.push_back({ "Out", {"double","unitless"}, Port::Direction::Output, this });
        setParam<double>("gain", 1.0); setParam<double>("offset", 0.0);
    }
    QWidget* createCustomWidget() override {
        using krs::nodeui::ControlSpec;
        return krs::nodeui::buildControlWidget(this, {
            { ControlSpec::Slider, "Gain",   "gain",   -5.0, 5.0, 0.01, 1.0 },
            { ControlSpec::Slider, "Offset", "offset", -5.0, 5.0, 0.01, 0.0 } });
    }
    void compute() override {
        setOutput<double>("Out", getInputD("In", 0.0) * getParam<double>("gain", 1.0) + getParam<double>("offset", 0.0));
    }
};

namespace {
void regGen(const char* id, const char* name, ParamSignalGenNode::Wave w) {
    NodeFactory::instance().registerNodeType(id,
        NodeDescriptor{ name, "Signal/Generator", "amp*wave(2pi*freq*t+phase)+offset (in-node dials)" },
        [w, id = std::string(id)]() { return std::make_unique<ParamSignalGenNode>(w, id); });
}
struct GeneratorRegistrar {
    GeneratorRegistrar() {
        regGen("gen_sine",     "Sine Gen",     ParamSignalGenNode::Sine);
        regGen("gen_square",   "Square Gen",   ParamSignalGenNode::Square);
        regGen("gen_triangle", "Triangle Gen", ParamSignalGenNode::Triangle);
        regGen("gen_saw",      "Sawtooth Gen", ParamSignalGenNode::Saw);
        NodeFactory::instance().registerNodeType("math_affine",
            NodeDescriptor{ "Affine (gain+offset)", "Math/Primitive", "out = in*gain + offset (sliders)" },
            []() { return std::make_unique<AffineNode>(); });
    }
};
static GeneratorRegistrar g_generatorRegistrar;
} // namespace

} // namespace NodeLibrary
