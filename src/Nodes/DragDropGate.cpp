// DragDropGate.cpp -- GATE DRAGDROP (data half; the drag ghost + on-screen drop are OPERATOR
// VISUAL-CONFIRM). The catalog packs a node type id into a QMimeData; the drop handler extracts it and
// instances the node via the SAME shared helper (instanceDroppedNode) the real dropEvent uses. This gate
// drives that exact data path and asserts a drop creates the CORRECT node type with its ports + mounted
// widget at the drop position. NEG-CTRL: an unknown / empty type instances nothing.

#include "Node.hpp"
#include "NodeDelegate.hpp"
#include "NodeEditorGate.hpp"

#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/Definitions>
#include <QApplication>
#include <QMimeData>
#include <QPointF>
#include <QVariant>

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

namespace krs::nodes {

bool runDragDropGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[dragdrop] GATE DRAGDROP -- a catalog drop (mime type-id -> instance) creates the correct typed node with ports+widgets\n");
    if (!QApplication::instance()) { printf("[dragdrop] FAIL: needs QApplication\n"); return false; }

    auto model = makeNodeGraphModel();
    const char* types[] = { "gen_sine", "math_add", "viz_numeric_readout", "control_pid",
                            "physics_articulation_drive", "filter_kalman_1d" };
    int N = 0, ok = 0;
    std::vector<std::string> failures;
    for (const char* t : types) {
        ++N;
        // The exact catalog->drop data path: the type id travels as QMimeData text.
        QMimeData mime; mime.setText(t);
        const QString typeId = mime.text();                 // what dropEvent reads
        const double dropX = 100.0 * N, dropY = 50.0;
        const QtNodes::NodeId id = instanceDroppedNode(*model, typeId, dropX, dropY);
        if (id == QtNodes::InvalidNodeId) { failures.push_back(std::string(t) + "(not-instanced)"); continue; }

        auto* del = model->delegateModel<NodeDelegate>(id);
        Node* n = del ? del->backendNode() : nullptr;
        const bool typeOk   = n && n->getId() == std::string(t);                       // correct type
        const bool portsOk  = del && (del->nPorts(QtNodes::PortType::In) + del->nPorts(QtNodes::PortType::Out) > 0u); // frame+ports
        const bool widgetOk = del && del->embeddedWidget() != nullptr;                 // widgets mounted
        const QPointF pos = model->nodeData(id, QtNodes::NodeRole::Position).value<QPointF>();
        const bool posOk = std::abs(pos.x() - dropX) < 1e-6 && std::abs(pos.y() - dropY) < 1e-6;
        if (typeOk && portsOk && widgetOk && posOk) ++ok;
        else failures.push_back(std::string(t) + "(type=" + (typeOk?"y":"n") + " ports=" + (portsOk?"y":"n")
                                + " widget=" + (widgetOk?"y":"n") + " pos=" + (posOk?"y":"n") + ")");
    }

    // NEG-CTRL: an unknown type id and an empty mime instance NOTHING.
    const QtNodes::NodeId badId   = instanceDroppedNode(*model, QStringLiteral("____not_a_registered_node____"), 0, 0);
    const QtNodes::NodeId emptyId = instanceDroppedNode(*model, QString(), 0, 0);
    const bool negOk = (badId == QtNodes::InvalidNodeId) && (emptyId == QtNodes::InvalidNodeId);

    const bool pass = (N > 0) && (ok == N) && negOk;
    printf("[dragdrop]   drops instanced correctly: %d/%d (correct type + ports + mounted widget + drop position)\n", ok, N);
    if (!failures.empty()) { printf("[dragdrop]   FAILURES: "); for (auto& s : failures) printf("%s ", s.c_str()); printf("\n"); }
    printf("[dragdrop]   NEG-CTRL: unknown type -> %s, empty mime -> %s (instance nothing)  %s\n",
           badId == QtNodes::InvalidNodeId ? "none" : "NODE!", emptyId == QtNodes::InvalidNodeId ? "none" : "NODE!",
           negOk ? "PASS" : "FAIL!");
    printf("[dragdrop] %s (the drag ghost + on-screen drop = OPERATOR VISUAL-CONFIRM)\n",
           pass ? "ALL PASS (a drop instances the correct typed node with ports+widgets at the drop position)" : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
