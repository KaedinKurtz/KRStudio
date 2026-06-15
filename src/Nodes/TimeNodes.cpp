// TimeNodes.cpp -- Phase F: a real LIVE time source. TimeSourceNode emits wall-clock elapsed seconds from
// std::chrono each time it is evaluated; a graph tick (a QTimer in MainWindow) re-evaluates it every frame
// so downstream time-parametric nodes (sine/ramp/oscillator) actually move over wall-clock instead of
// emitting a constant. GATE TIME proves a sine driven by the live time source oscillates over real time.

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "NodeDelegate.hpp"
#include "NodeEditorGate.hpp"

#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/Definitions>
#include <QApplication>
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
} // namespace

bool runTimeGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[time] GATE TIME -- a sine CONNECTED to the live time source (real edge) oscillates over wall-clock\n");
    if (!QApplication::instance()) { printf("[time] FAIL: needs QApplication\n"); return false; }

    // build a REAL graph: time_source -> gen_sine via an actual QtNodes connection; a second sine left
    // DISCONNECTED is the negative control.
    auto model = makeNodeGraphModel();
    const QtNodes::NodeId tId  = model->addNode("time_source");
    const QtNodes::NodeId sId  = model->addNode("gen_sine");
    const QtNodes::NodeId s2Id = model->addNode("gen_sine");
    auto* tDel  = model->delegateModel<NodeDelegate>(tId);
    auto* sDel  = model->delegateModel<NodeDelegate>(sId);
    auto* s2Del = model->delegateModel<NodeDelegate>(s2Id);
    if (!tDel || !sDel || !s2Del) { printf("[time] FAIL: delegates\n"); return false; }
    for (auto* d : { sDel, s2Del }) { Node* n = d->backendNode();
        n->setParam<double>("freq", 1.0); n->setParam<double>("amp", 1.0); n->setParam<double>("phase", 0.0); }

    const int oi = portIndexByName(tDel->backendNode(), Port::Direction::Output, "Time");
    const int ii = portIndexByName(sDel->backendNode(), Port::Direction::Input, "t");
    const QtNodes::ConnectionId conn{ tId, QtNodes::PortIndex(oi), sId, QtNodes::PortIndex(ii) };
    const bool connOk = model->connectionPossible(conn);          // the edit-time check the editor uses
    if (connOk) model->addConnection(conn);                       // form the REAL edge

    // tick the time source through the SAME path the 30Hz MainWindow timer uses; the connection propagates
    // the live time into the connected sine (via the model -> NodeDelegate::setInData). The disconnected
    // sine is ticked too but receives nothing.
    auto tickAndRead = [&](double& tOut, double& sOut, double& s2Out) {
        tDel->recomputeAndPropagate();
        tOut  = outOf(*tDel->backendNode(),  "Time");
        sOut  = outOf(*sDel->backendNode(),  "Out");
        s2Del->recomputeAndPropagate(); s2Out = outOf(*s2Del->backendNode(), "Out");
    };
    double t1, s1, c1; tickAndRead(t1, s1, c1);
    QThread::msleep(150);
    double t2, s2, c2; tickAndRead(t2, s2, c2);

    const bool timeAdvanced = std::isfinite(t1) && std::isfinite(t2) && (t2 - t1) > 0.05;
    const bool sineMoved    = std::isfinite(s1) && std::isfinite(s2) && std::abs(s2 - s1) > 1e-6;
    const bool tracks = std::abs(s1 - std::sin(TAU * t1)) < 1e-5 && std::abs(s2 - std::sin(TAU * t2)) < 1e-5;
    const bool constantWhenDisconnected = std::isfinite(c1) && std::abs(c2 - c1) < 1e-12;   // unconnected sine

    const bool pass = connOk && timeAdvanced && sineMoved && tracks && constantWhenDisconnected;
    printf("[time]   real edge time->sine connectable=%s; live time t1=%.4f -> t2=%.4f (advanced %.3fs)  %s\n",
           connOk ? "yes" : "NO", t1, t2, t2 - t1, timeAdvanced ? "yes" : "NO");
    printf("[time]   CONNECTED sine over wall-clock: s1=%.4f -> s2=%.4f (moved %s; tracks sin(2pi*t) %s)  %s\n",
           s1, s2, sineMoved ? "yes" : "NO", tracks ? "yes" : "NO", (sineMoved && tracks) ? "PASS" : "FAIL");
    printf("[time]   NEG-CTRL DISCONNECTED sine -> constant (%.4f==%.4f)  %s\n", c1, c2, constantWhenDisconnected ? "PASS" : "FAIL!");
    printf("[time] %s\n", pass ? "ALL PASS (real edge + live tick propagate time into the sine; disconnected -> constant)"
                               : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
