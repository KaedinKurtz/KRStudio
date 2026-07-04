// ===========================================================================
// KGraph.cpp -- see KGraph.hpp. The live DataFlowGraphModel <-> .kgraph bridge.
// ===========================================================================
#include "KGraph.hpp"
#include "KNode.hpp"           // KNodeDoc + shared writeDoc/readDoc
#include "KDoc.hpp"            // nodeToJson / applyJsonToNode
#include "Node.hpp"
#include "NodeDelegate.hpp"    // typeId() / backendNode()
#include "NodeEditorGate.hpp"  // makeNodeGraphModel / portIndexByName
#include "RobotGraph.hpp"      // spawnDefaultRobotGraph (gate)
#include "Scene.hpp"

#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/Definitions>
#include <QPointF>
#include <QVariant>
#include <QDir>
#include <QApplication>
#include <QCryptographicHash>
#include <QFile>

#include <cstdio>
#include <cmath>
#include <string>
#include <unordered_map>

using QtNodes::DataFlowGraphModel;
using QtNodes::NodeId;
using QtNodes::ConnectionId;
using QtNodes::PortIndex;
using QtNodes::NodeRole;
using krs::nodes::makeNodeGraphModel;   // NodeEditorGate.hpp (krs::nodes)
using krs::nodes::portIndexByName;

namespace krs::kgraph {
namespace {

// index -> name for the Nth port of a given direction (same mapping the eval engine + portIndexByName use).
const std::string* nthPortName(Node* n, Port::Direction dir, int idx) {
    int c = 0;
    for (const auto& p : n->getPorts())
        if (p.direction == dir) { if (c == idx) return &p.name; ++c; }
    return nullptr;
}
QString sid(NodeId id) { return QStringLiteral("n%1").arg(quint64(id)); }

Node* backendOf(DataFlowGraphModel& model, NodeId id) {
    auto* d = model.delegateModel<NodeDelegate>(id);
    return d ? d->backendNode() : nullptr;
}

} // namespace

krs::knode::KNodeDoc harvestGraph(DataFlowGraphModel& model) {
    using namespace krs::knode;
    KNodeDoc doc;
    for (NodeId id : model.allNodeIds()) {
        auto* d = model.delegateModel<NodeDelegate>(id);
        if (!d) continue;
        Node* n = d->backendNode();
        if (!n) continue;
        InteriorNode in;
        in.id     = sid(id);
        in.typeId = QString::fromStdString(d->typeId());
        in.state  = krs::kdoc::nodeToJson(*n, d->typeId());
        const QPointF pos = model.nodeData(id, NodeRole::Position).value<QPointF>();
        in.x = pos.x(); in.y = pos.y();
        doc.nodes.push_back(std::move(in));
    }
    // connections: count each ONCE at its out-node; store endpoints BY PORT NAME.
    for (NodeId id : model.allNodeIds()) {
        Node* on = backendOf(model, id);
        if (!on) continue;
        for (const ConnectionId& c : model.allConnectionIds(id)) {
            if (c.outNodeId != id) continue;
            Node* in = backendOf(model, c.inNodeId);
            if (!in) continue;
            const std::string* op = nthPortName(on, Port::Direction::Output, int(c.outPortIndex));
            const std::string* ip = nthPortName(in, Port::Direction::Input,  int(c.inPortIndex));
            if (!op || !ip) continue;
            Connection conn;
            conn.fromNode = sid(c.outNodeId); conn.fromPort = QString::fromStdString(*op);
            conn.toNode   = sid(c.inNodeId);  conn.toPort   = QString::fromStdString(*ip);
            doc.connections.push_back(std::move(conn));
        }
    }
    return doc;
}

bool materializeGraph(const krs::knode::KNodeDoc& doc, DataFlowGraphModel& model, Scene* scene, QString* err) {
    std::unordered_map<QString, NodeId> idMap;
    int skipped = 0;
    for (const auto& in : doc.nodes) {
        const NodeId id = model.addNode(in.typeId);
        if (id == QtNodes::InvalidNodeId) { ++skipped; continue; }   // unknown factory id -> skip, don't crash
        idMap[in.id] = id;
        if (Node* n = backendOf(model, id)) {
            if (scene) n->setScene(scene);
            krs::kdoc::applyJsonToNode(*n, in.state, nullptr);        // params -> selectNamedOption(rebuild ports) -> literals -> policy
        }
        model.setNodeData(id, NodeRole::Position, QVariant::fromValue(QPointF(in.x, in.y)));
    }
    // re-wire: resolve port NAMES -> current positional indices on the freshly built nodes.
    for (const auto& c : doc.connections) {
        auto itF = idMap.find(c.fromNode), itT = idMap.find(c.toNode);
        if (itF == idMap.end() || itT == idMap.end()) continue;      // endpoint node was skipped
        Node* fn = backendOf(model, itF->second);
        Node* tn = backendOf(model, itT->second);
        if (!fn || !tn) continue;
        const int oi = portIndexByName(fn, Port::Direction::Output, c.fromPort.toStdString().c_str());
        const int ii = portIndexByName(tn, Port::Direction::Input,  c.toPort.toStdString().c_str());
        if (oi < 0 || ii < 0) continue;                              // port renamed/removed -> drop the wire
        const ConnectionId conn{ itF->second, PortIndex(oi), itT->second, PortIndex(ii) };
        if (model.connectionPossible(conn)) model.addConnection(conn);
    }
    if (skipped && err) *err = QStringLiteral("%1 node(s) had an unknown factory id and were skipped").arg(skipped);
    return true;
}

QString saveKGraph(DataFlowGraphModel& model, const QString& absPath, const QString& name) {
    krs::knode::KNodeDoc doc = harvestGraph(model);
    if (!name.isEmpty()) doc.name = name;
    return krs::knode::writeDoc(doc, absPath, QStringLiteral("kgraph"));
}

bool loadKGraph(const QString& absPath, DataFlowGraphModel& model, Scene* scene, QString* err) {
    krs::knode::KNodeDoc doc;
    if (!krs::knode::readDoc(absPath, doc, QStringLiteral("kgraph"), err)) return false;
    return materializeGraph(doc, model, scene, err);
}

// ================================================================================================
// GATE KGRAPH
// ================================================================================================
namespace {
QString fileSha1(const QString& path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha1).toHex());
}
Node* findByType(DataFlowGraphModel& model, const char* typeId) {
    for (NodeId id : model.allNodeIds()) {
        auto* d = model.delegateModel<NodeDelegate>(id);
        if (d && d->typeId() == typeId) return d->backendNode();
    }
    return nullptr;
}
bool nodeHasPort(Node* n, const char* name) {
    if (!n) return false;
    for (const auto& p : n->getPorts()) if (p.name == name) return true;
    return false;
}
} // namespace

bool runKGraphGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[kgraph] GATE KGRAPH -- whole live graph <-> .kgraph round-trip (params/literals/wiring/position + reconfigure)\n");
    if (!QApplication::instance()) { printf("[kgraph] FAIL: needs QApplication\n"); return false; }
    const QString dir = QDir::temp().filePath("krs_kgraph_gate");
    QDir(dir).removeRecursively();
    const QString path = QDir(dir).filePath("cell.kgraph");
    Scene scene;

    // ---- author model A: the REAL boot graph (time->sine->drive) + a reconfigured twin_property ----
    auto A = makeNodeGraphModel();
    krs::nodes::BootGraphHandles h = krs::nodes::spawnDefaultRobotGraph(*A, &scene, /*joint*/2, /*freq*/1.7, /*amp*/0.6);
    if (!h.ok) { printf("[kgraph] FAIL: spawnDefaultRobotGraph\n"); return false; }
    const NodeId propId = A->addNode("twin_property");
    if (Node* pn = backendOf(*A, propId)) pn->selectNamedOption("orientation");   // -> Quaternion port layout
    A->setNodeData(propId, NodeRole::Position, QVariant::fromValue(QPointF(300, 120)));

    const QString id = saveKGraph(*A, path, "cell");
    const QString h1 = fileSha1(path);
    const bool saved = !id.isEmpty() && QFile::exists(path) && !h1.isEmpty();

    // ---- load into a FRESH model B ----
    auto B = makeNodeGraphModel();
    QString lerr;
    const bool loaded = loadKGraph(path, *B, &scene, &lerr);
    const int nA = int(A->allNodeIds().size()), nB = int(B->allNodeIds().size());

    Node* sineB  = findByType(*B, "gen_sine");
    Node* driveB = findByType(*B, "physics_articulation_drive");
    Node* propB  = findByType(*B, "twin_property");
    const double freqB = sineB  ? sineB->getParam<double>("freq", -1.0) : -1.0;
    const int    jntB  = driveB ? driveB->getInput<int>("Joint").value_or(-999) : -999;
    const bool reconfB = nodeHasPort(propB, "Quaternion");   // reconfigure materialized through the live delegate

    // connection count (each once) + one position round-trip.
    auto connCount = [](DataFlowGraphModel& m){ int k=0; for (NodeId id : m.allNodeIds())
        for (const ConnectionId& c : m.allConnectionIds(id)) if (c.outNodeId==id) ++k; return k; };
    const int cA = connCount(*A), cB = connCount(*B);
    bool posOk = false;
    if (propB) for (NodeId bid : B->allNodeIds()) {
        auto* d = B->delegateModel<NodeDelegate>(bid);
        if (d && d->typeId() == "twin_property") {
            const QPointF p = B->nodeData(bid, NodeRole::Position).value<QPointF>();
            posOk = std::abs(p.x() - 300) < 1e-6 && std::abs(p.y() - 120) < 1e-6;
        }
    }

    const bool fidelity = loaded && nB == nA && nB == 4 && std::abs(freqB - 1.7) < 1e-9
        && jntB == 2 && reconfB && cB == cA && cB == 2 && posOk;
    printf("[kgraph]   round-trip: saved=%s loaded=%s nodes=%d/%d sine.freq=%.3f drive.Joint=%d reconfigure=%s conns=%d/%d pos=%s  %s\n",
           saved?"yes":"no", loaded?"yes":"no", nB, nA, freqB, jntB, reconfB?"yes":"no", cB, cA, posOk?"yes":"no",
           fidelity ? "PASS" : "FAIL");

    // ---- content hash: deterministic + changes on an interior edit ----
    if (sineB) sineB->setParam<double>("freq", 9.9);
    const QString path2 = QDir(dir).filePath("cell_edited.kgraph");
    saveKGraph(*B, path2, "cell");
    const QString h2 = fileSha1(path2);
    const bool hashChanged = !h2.isEmpty() && h2 != h1;
    printf("[kgraph]   content hash changes on interior edit=%s  %s\n", hashChanged?"yes":"NO", hashChanged?"PASS":"FAIL");

    // ---- NEG-CTRLs: unknown format major refused; unknown factory id skipped (rest still materialize) ----
    // (a) tamper the format major.
    { QFile f(path); f.open(QIODevice::ReadOnly); QByteArray b = f.readAll(); f.close();
      b.replace("\"kgraph/1\"", "\"kgraph/2\"");
      const QString badPath = QDir(dir).filePath("badmajor.kgraph");
      QFile bf(badPath); bf.open(QIODevice::WriteOnly); bf.write(b); bf.close();
      auto C = makeNodeGraphModel(); QString cerr;
      const bool refusedMajor = !loadKGraph(badPath, *C, &scene, &cerr) && !cerr.isEmpty();
      // (b) inject an unknown factory id into a harvested doc -> that node skipped, valid ones materialize.
      krs::knode::KNodeDoc doc = harvestGraph(*A);
      krs::knode::InteriorNode ghost; ghost.id = "ghost"; ghost.typeId = "no_such_node_type";
      doc.nodes.push_back(ghost);
      auto D = makeNodeGraphModel(); QString derr;
      materializeGraph(doc, *D, &scene, &derr);
      const bool skipUnknown = int(D->allNodeIds().size()) == nA && derr.contains("unknown factory id");
      const bool negOk = refusedMajor && skipUnknown;
      printf("[kgraph]   NEG-CTRLs: unknown major refused=%s ; unknown factory id skipped (rest materialize %d==%d)=%s  %s\n",
             refusedMajor?"yes":"NO", int(D->allNodeIds().size()), nA, skipUnknown?"yes":"NO",
             negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");
      const bool pass = saved && fidelity && hashChanged && negOk;
      printf("[kgraph] %s\n", pass ? "ALL PASS (whole live graph round-trips a .kgraph: params/literals/wiring-by-name/positions + reconfigured-node materialize; content-hash detection; unknown major + unknown factory-id neg-ctrls)"
                                   : "FAILURES PRESENT");
      std::fflush(stdout);
      return pass;
    }
}

} // namespace krs::kgraph
