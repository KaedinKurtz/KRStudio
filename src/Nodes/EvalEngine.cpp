// EvalEngine.cpp -- see EvalEngine.hpp. Quiet (no-scene-signal) graph evaluation + capped UI refresh.

#include "EvalEngine.hpp"
#include "NodeDelegate.hpp"
#include "Node.hpp"

#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/Definitions>

#include <queue>
#include <vector>
#include <unordered_map>
#include <optional>
#include <string>
#include <chrono>

namespace krs::nodes {
namespace {

// The idx-th port of a direction, in the same order NodeDelegate exposes to QtNodes.
const std::string* nthPortName(Node* n, Port::Direction dir, int idx) {
    int c = 0;
    for (const auto& p : n->getPorts())
        if (p.direction == dir) { if (c == idx) return &p.name; ++c; }
    return nullptr;
}
std::optional<PortDataPacket> nthOutPacket(Node* n, int idx) {
    int c = 0;
    for (const auto& p : n->getPorts())
        if (p.direction == Port::Direction::Output) { if (c == idx) return p.packet; ++c; }
    return std::nullopt;
}

} // namespace

void evaluateGraphQuiet(QtNodes::DataFlowGraphModel& model)
{
    const auto nodeIds = model.allNodeIds();
    std::unordered_map<QtNodes::NodeId, int> indeg;
    std::unordered_map<QtNodes::NodeId, std::vector<QtNodes::ConnectionId>> outEdges;
    indeg.reserve(nodeIds.size());
    for (QtNodes::NodeId id : nodeIds) indeg[id] = 0;
    // Count each connection ONCE (at its out-node) -> build adjacency + in-degrees.
    for (QtNodes::NodeId id : nodeIds)
        for (const QtNodes::ConnectionId& c : model.allConnectionIds(id))
            if (c.outNodeId == id) { outEdges[id].push_back(c); indeg[c.inNodeId]++; }

    auto backend = [&](QtNodes::NodeId id) -> Node* {
        auto* d = model.delegateModel<NodeDelegate>(id);
        return d ? d->backendNode() : nullptr;
    };

    std::queue<QtNodes::NodeId> q;
    for (const auto& kv : indeg) if (kv.second == 0) q.push(kv.first);

    std::size_t processed = 0;
    while (!q.empty()) {
        const QtNodes::NodeId id = q.front(); q.pop(); ++processed;
        if (Node* n = backend(id)) {
            n->process();                                   // compute with current inputs (math only)
            auto it = outEdges.find(id);
            if (it != outEdges.end())
                for (const QtNodes::ConnectionId& c : it->second) {
                    if (Node* dn = backend(c.inNodeId)) {   // copy this node's output packet -> downstream input
                        auto pkt = nthOutPacket(n, int(c.outPortIndex));
                        const std::string* inName = nthPortName(dn, Port::Direction::Input, int(c.inPortIndex));
                        if (pkt && inName) dn->setInput(*inName, *pkt);
                    }
                    if (--indeg[c.inNodeId] == 0) q.push(c.inNodeId);
                }
        } else {
            auto it = outEdges.find(id);
            if (it != outEdges.end())
                for (const QtNodes::ConnectionId& c : it->second)
                    if (--indeg[c.inNodeId] == 0) q.push(c.inNodeId);
        }
    }
    // Cycle guard (editor graphs are DAGs, but never wedge): best-effort process any nodes left in a cycle.
    if (processed < nodeIds.size())
        for (QtNodes::NodeId id : nodeIds) if (Node* n = backend(id)) n->process();
}

int refreshGraphUi(QtNodes::DataFlowGraphModel& model)
{
    int repainted = 0;
    for (QtNodes::NodeId id : model.allNodeIds()) {
        auto* d = model.delegateModel<NodeDelegate>(id);
        if (d && d->backendNode()) repainted += d->backendNode()->refreshUi() ? 1 : 0;
    }
    return repainted;
}

EvalLoopStats runEvalLoop(double evalHz, double uiCapHz, double durSec,
                          const std::function<void()>& evalFn, const std::function<void()>& uiFn)
{
    using clock = std::chrono::steady_clock;
    EvalLoopStats st;
    const double evalPeriod = (evalHz > 0.0) ? 1.0 / evalHz : 0.0;
    const double uiPeriod = (uiCapHz > 0.0) ? 1.0 / uiCapHz : 0.0;
    const auto t0 = clock::now();
    double nextEval = 0.0, nextUi = 0.0;
    for (;;) {
        const double now = std::chrono::duration<double>(clock::now() - t0).count();
        if (now >= durSec) { st.seconds = now; break; }
        if (now >= nextEval) { if (evalFn) evalFn(); ++st.evals; nextEval += evalPeriod; if (nextEval < now) nextEval = now + evalPeriod; }
        if (now >= nextUi)   { if (uiFn) uiFn();     ++st.uiRefreshes; nextUi += uiPeriod; if (nextUi < now) nextUi = now + uiPeriod; }
    }
    return st;
}

} // namespace krs::nodes
