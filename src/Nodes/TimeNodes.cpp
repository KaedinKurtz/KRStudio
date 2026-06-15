// TimeNodes.cpp -- Phase F: a real LIVE time source. TimeSourceNode emits wall-clock elapsed seconds from
// std::chrono each time it is evaluated; a graph tick (a QTimer in MainWindow) re-evaluates it every frame
// so downstream time-parametric nodes (sine/ramp/oscillator) actually move over wall-clock instead of
// emitting a constant. GATE TIME proves a sine driven by the live time source oscillates over real time.

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "NodeEditorGate.hpp"

#include <QThread>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <memory>

namespace NodeLibrary {

class TimeSourceNode : public Node {
public:
    TimeSourceNode() {
        m_id = "time_source";
        m_ports.push_back({ "Time", {"double","s"}, Port::Direction::Output, this });
        m_start = std::chrono::steady_clock::now();
    }
    bool needsExecutionControls() const override { return false; }
    void compute() override {
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
        setOutput<double>("Time", t);
    }
private:
    std::chrono::steady_clock::time_point m_start;
};

namespace {
struct TimeSourceRegistrar {
    TimeSourceRegistrar() {
        NodeFactory::instance().registerNodeType("time_source",
            NodeDescriptor{ "Time Source", "Time", "live wall-clock elapsed seconds (drives time-parametric nodes)" },
            []() { return std::make_unique<TimeSourceNode>(); });
    }
};
static TimeSourceRegistrar g_timeSourceRegistrar;
} // namespace

} // namespace NodeLibrary

// ---------------------------------------------------------------------------------------------------
namespace krs::nodes {
namespace {
constexpr double TAU = 6.283185307179586;
double outOf(Node& n, const char* port) {
    for (const auto& p : n.getPorts())
        if (p.direction == Port::Direction::Output && p.name == port && p.packet.has_value())
            try { return std::any_cast<double>(p.packet->data); } catch (...) {}
    return std::nan("");
}
void feedD(Node& n, const char* port, double v) { PortDataPacket pk; pk.data = v; pk.type = { "", "" }; n.setInput(port, pk); }
} // namespace

bool runTimeGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[time] GATE TIME -- a sine driven by the LIVE time source oscillates over wall-clock\n");

    auto tnode = NodeFactory::instance().createNode("time_source");
    auto sine  = NodeFactory::instance().createNode("gen_sine");
    if (!tnode || !sine) { printf("[time] FAIL: nodes\n"); return false; }
    sine->setParam<double>("freq", 1.0); sine->setParam<double>("amp", 1.0); sine->setParam<double>("phase", 0.0);

    // sample the LIVE time source, feed it to the sine, twice, with real wall-clock between.
    tnode->process(); const double t1 = outOf(*tnode, "Time");
    feedD(*sine, "t", t1); sine->process(); const double s1 = outOf(*sine, "Out");
    QThread::msleep(150);
    tnode->process(); const double t2 = outOf(*tnode, "Time");
    feedD(*sine, "t", t2); sine->process(); const double s2 = outOf(*sine, "Out");

    const bool timeAdvanced = std::isfinite(t1) && std::isfinite(t2) && (t2 - t1) > 0.05;       // ~0.15s elapsed
    const bool sineMoved    = std::abs(s2 - s1) > 1e-6;                                          // output changed
    const bool tracks = std::abs(s1 - std::sin(TAU * t1)) < 1e-5 && std::abs(s2 - std::sin(TAU * t2)) < 1e-5;

    // NEG-CTRL: with NO time source connected, the sine's "t" stays unset -> constant output (the bug).
    auto sine2 = NodeFactory::instance().createNode("gen_sine");
    sine2->setParam<double>("freq", 1.0); sine2->setParam<double>("amp", 1.0);
    sine2->process(); const double c1 = outOf(*sine2, "Out");
    QThread::msleep(150);
    sine2->process(); const double c2 = outOf(*sine2, "Out");
    const bool constantWhenDisconnected = std::abs(c2 - c1) < 1e-12;

    const bool pass = timeAdvanced && sineMoved && tracks && constantWhenDisconnected;
    printf("[time]   live time: t1=%.4f -> t2=%.4f (advanced %.3fs)  %s\n", t1, t2, t2 - t1, timeAdvanced ? "yes" : "NO");
    printf("[time]   sine over wall-clock: s1=%.4f -> s2=%.4f (moved %s; tracks sin(2pi*t) %s)  %s\n",
           s1, s2, sineMoved ? "yes" : "NO", tracks ? "yes" : "NO", (sineMoved && tracks) ? "PASS" : "FAIL");
    printf("[time]   NEG-CTRL no time source -> sine constant (%.4f==%.4f)  %s\n", c1, c2, constantWhenDisconnected ? "PASS" : "FAIL!");
    printf("[time] %s\n", pass ? "ALL PASS (live time source drives the sine over wall-clock; disconnected -> constant)"
                               : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
