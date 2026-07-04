// ===========================================================================
// SubgraphNode.cpp -- see SubgraphNode.hpp. Interior-subgraph node + the
// library->factory "knode:<id>" registration bridge.
// ===========================================================================
#include "SubgraphNode.hpp"
#include "NodeFactory.hpp"
#include "KDoc.hpp"
#include "KParts.hpp"

#include <QString>

#include <cstdio>
#include <cmath>
#include <queue>
#include <set>
#include <string>

namespace krs::nodes {

using krs::knode::KNodeDoc;
using krs::knode::Connection;

// Map a saved canonical dataType name to a Port DataType (unit is cosmetic; the connection id is
// derived from the type name by PortTypes). Empty -> a plain scalar.
static DataType portType(const QString& dataType) {
    return { dataType.isEmpty() ? std::string("double") : dataType.toStdString(), "unitless" };
}

SubgraphNode::SubgraphNode(const KNodeDoc& doc) : m_doc(doc) {
    m_id = doc.id.isEmpty() ? std::string("subgraph") : ("knode:" + doc.id).toStdString();

    // Cycle guard: ids currently mid-construction on this thread. If an interior node is (transitively)
    // THIS subgraph, omit it rather than recurse -- a self-referential definition can never wedge.
    static thread_local std::set<QString> building;
    const bool tracked = !doc.id.isEmpty();
    if (tracked) building.insert(doc.id);

    for (const auto& in : doc.nodes) {
        if (in.typeId.startsWith(QStringLiteral("knode:"))) {
            const QString childId = in.typeId.mid(6);
            if (building.count(childId)) { m_byDocId[in.id] = nullptr; continue; }   // cyclic -> omit
        }
        auto n = NodeFactory::instance().createNode(in.typeId.toStdString());
        if (!n) { m_byDocId[in.id] = nullptr; continue; }                            // unknown interior type -> omit
        krs::kdoc::applyJsonToNode(*n, in.state, nullptr);
        m_byDocId[in.id] = n.get();
        m_interior.push_back(std::move(n));
    }

    if (tracked) building.erase(doc.id);

    // Expose the promoted boundary ports as this node's outer ports (typed from the saved dataType).
    for (const auto& ep : doc.inputs())
        m_ports.push_back({ ep.name.toStdString(), portType(ep.dataType), Port::Direction::Input,  this });
    for (const auto& ep : doc.outputs())
        m_ports.push_back({ ep.name.toStdString(), portType(ep.dataType), Port::Direction::Output, this });
}

void SubgraphNode::evalInterior() {
    // Kahn topological order over the interior connections (both endpoints must resolve).
    std::unordered_map<QString, int> indeg;
    std::unordered_map<QString, std::vector<const Connection*>> out;
    for (const auto& in : m_doc.nodes) if (m_byDocId[in.id]) indeg[in.id] = 0;
    for (const auto& c : m_doc.connections) {
        auto f = m_byDocId.find(c.fromNode), t = m_byDocId.find(c.toNode);
        if (f == m_byDocId.end() || t == m_byDocId.end() || !f->second || !t->second) continue;
        out[c.fromNode].push_back(&c);
        indeg[c.toNode]++;
    }
    std::queue<QString> q;
    for (const auto& kv : indeg) if (kv.second == 0) q.push(kv.first);
    std::size_t processed = 0;
    while (!q.empty()) {
        const QString id = q.front(); q.pop(); ++processed;
        Node* n = m_byDocId[id];
        if (n) n->process();
        auto it = out.find(id);
        if (it != out.end())
            for (const Connection* c : it->second) {
                Node* dn = m_byDocId[c->toNode];
                if (n && dn)
                    if (const PortDataPacket* pk = n->outputPacket(c->fromPort.toStdString()))
                        dn->setInput(c->toPort.toStdString(), *pk);
                if (--indeg[c->toNode] == 0) q.push(c->toNode);
            }
    }
    // interior cycle guard (definitions are DAGs, but never wedge): best-effort process the remainder.
    if (processed < indeg.size())
        for (const auto& in : m_doc.nodes) if (Node* n = m_byDocId[in.id]) n->process();
}

void SubgraphNode::compute() {
    // 1. marshal this node's exposed inputs onto the interior boundary node inputs.
    for (const auto& ep : m_doc.inputs()) {
        auto it = m_byDocId.find(ep.interiorNode);
        Node* interior = (it != m_byDocId.end()) ? it->second : nullptr;
        if (!interior) continue;
        if (const PortDataPacket* pk = inputValue(ep.name.toStdString()))
            interior->setInput(ep.interiorPort.toStdString(), *pk);
    }
    // 2. evaluate the interior.
    evalInterior();
    // 3. marshal the interior boundary node outputs back out onto this node's exposed outputs.
    for (const auto& ep : m_doc.outputs()) {
        auto it = m_byDocId.find(ep.interiorNode);
        Node* interior = (it != m_byDocId.end()) ? it->second : nullptr;
        if (!interior) continue;
        if (const PortDataPacket* pk = interior->outputPacket(ep.interiorPort.toStdString()))
            setOutputPacket(ep.name.toStdString(), *pk);
    }
}

int registerDiscoveredKNodes(krs::parts::PartLibrary& lib) {
    int count = 0;
    for (const auto& e : lib.byType(krs::parts::PartType::Node)) {
        KNodeDoc doc; QString err;
        if (!krs::knode::loadKNode(e.absPath, doc, &err)) continue;   // corrupt / cyclic-doc / bad-format -> skip
        if (doc.id.isEmpty()) continue;
        const std::string typeId = ("knode:" + doc.id).toStdString();
        const std::string name   = doc.name.isEmpty() ? doc.id.toStdString() : doc.name.toStdString();
        const std::string cat    = doc.category.isEmpty() ? std::string("Custom") : doc.category.toStdString();
        NodeFactory::instance().registerNodeType(typeId, { name, cat, "Custom subgraph (.knode)" },
            [doc]() { return std::make_unique<SubgraphNode>(doc); });
        ++count;
    }
    return count;
}

// ================================================================================================
// GATE SUBGRAPH
// ================================================================================================
namespace {
void feedD(Node& n, const std::string& port, double v) {
    PortDataPacket pk; pk.data = v; pk.type = { "double", "unitless" }; n.setInput(port, pk);
}
double readD(Node& n, const std::string& port) {
    if (const PortDataPacket* pk = n.outputPacket(port)) { try { return std::any_cast<double>(pk->data); } catch (...) {} }
    return std::nan("");
}
// Build a KNodeDoc wrapping ONE interior node (typeId) with its kdoc state, exposing (inName->port) and
// (outName<-port). `state` is the interior node's serialized params.
KNodeDoc wrapOne(const QString& id, const QString& name, const QString& typeId, const QJsonObject& state,
                 const char* inPort, const char* outPort) {
    KNodeDoc d; d.id = id; d.name = name; d.category = "Custom";
    krs::knode::InteriorNode n; n.id = "m"; n.typeId = typeId; n.state = state;
    d.nodes.push_back(n);
    d.ports.push_back({ "x", "m", inPort,  true,  "double" });
    d.ports.push_back({ "y", "m", outPort, false, "double" });
    return d;
}
} // namespace

bool runSubgraphGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[subgraph] GATE SUBGRAPH -- .knode registered as knode:<id> evaluates its interior; nesting; cycle-safe\n");
    auto& F = NodeFactory::instance();
    bool allOk = true;

    // ---- (1) a gain+offset subgraph (interior = one math_affine, gain=2 offset=1) -> y = 2x+1 ----
    auto aff = F.createNode("math_affine");
    if (!aff) { printf("[subgraph]   math_affine missing -- FAIL\n"); return false; }
    aff->setParam<double>("gain", 2.0); aff->setParam<double>("offset", 1.0);
    const QJsonObject affState = krs::kdoc::nodeToJson(*aff, "math_affine");
    const KNodeDoc A = wrapOne("A-uuid", "Gain2Off1", "math_affine", affState, "In", "Out");
    F.registerNodeType("knode:A-uuid", { "Gain2Off1", "Custom", "Custom subgraph" },
                       [A]() { return std::make_unique<SubgraphNode>(A); });

    bool oneOk = false;
    if (auto sg = F.createNode("knode:A-uuid")) {
        feedD(*sg, "x", 5.0); sg->process();
        const double y = readD(*sg, "y");
        oneOk = std::abs(y - 11.0) < 1e-9;                  // 2*5 + 1
        printf("[subgraph]   single-level knode:A(x=5) -> y=%.3f (want 11)  %s\n", y, oneOk ? "PASS" : "FAIL");
    } else printf("[subgraph]   createNode(knode:A) null -- FAIL\n");
    allOk = allOk && oneOk;

    // ---- (2) NESTED: knode:B wraps knode:A then an affine(gain=3) -> y = 3*(2x+1) ----
    // B interior: node "a" = knode:A-uuid ; node "g" = math_affine(gain=3,offset=0); a.y -> g.In.
    auto aff3 = F.createNode("math_affine");
    aff3->setParam<double>("gain", 3.0); aff3->setParam<double>("offset", 0.0);
    const QJsonObject aff3State = krs::kdoc::nodeToJson(*aff3, "math_affine");
    KNodeDoc B; B.id = "B-uuid"; B.name = "Nested"; B.category = "Custom";
    { krs::knode::InteriorNode a; a.id = "a"; a.typeId = "knode:A-uuid"; a.state = QJsonObject{}; B.nodes.push_back(a); }
    { krs::knode::InteriorNode g; g.id = "g"; g.typeId = "math_affine";  g.state = aff3State;      B.nodes.push_back(g); }
    B.connections.push_back({ "a", "y", "g", "In" });       // A's exposed output -> the outer affine's In
    B.ports.push_back({ "x", "a", "x",   true,  "double" }); // B.x -> A.x
    B.ports.push_back({ "y", "g", "Out", false, "double" }); // B.y <- affine.Out
    F.registerNodeType("knode:B-uuid", { "Nested", "Custom", "Custom subgraph" },
                       [B]() { return std::make_unique<SubgraphNode>(B); });

    bool nestOk = false;
    if (auto sg = F.createNode("knode:B-uuid")) {
        feedD(*sg, "x", 5.0); sg->process();
        const double y = readD(*sg, "y");
        nestOk = std::abs(y - 33.0) < 1e-9;                 // 3 * (2*5+1) = 33
        printf("[subgraph]   nested knode:B(x=5) -> y=%.3f (want 33 = 3*(2*5+1))  %s\n", y, nestOk ? "PASS" : "FAIL");
    } else printf("[subgraph]   createNode(knode:B) null -- FAIL\n");
    allOk = allOk && nestOk;

    // ---- NEG-CTRL: a self-referential subgraph instantiates SAFELY (cyclic interior omitted), no hang ----
    KNodeDoc C; C.id = "C-uuid"; C.name = "SelfRef"; C.category = "Custom";
    { krs::knode::InteriorNode s; s.id = "self"; s.typeId = "knode:C-uuid"; s.state = QJsonObject{}; C.nodes.push_back(s); }
    F.registerNodeType("knode:C-uuid", { "SelfRef", "Custom", "Custom subgraph" },
                       [C]() { return std::make_unique<SubgraphNode>(C); });
    auto cptr = F.createNode("knode:C-uuid");
    // Reached here => no infinite recursion. The cyclic interior node was omitted (interiorCount 0).
    auto* csg = dynamic_cast<SubgraphNode*>(cptr.get());
    const bool negOk = cptr && csg && csg->interiorCount() == 0;
    printf("[subgraph]   NEG-CTRL self-referential subgraph safe (created=%s, cyclic interior omitted, count=%d)  %s\n",
           cptr ? "yes" : "no", csg ? csg->interiorCount() : -1, negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");
    allOk = allOk && negOk;

    printf("[subgraph] %s\n", allOk ? "ALL PASS (knode:<id> subgraph instantiates + evaluates its interior; nests to the 2-level result; a cyclic definition is instantiated safely, not wedged)"
                                    : "FAILURES PRESENT");
    std::fflush(stdout);
    return allOk;
}

} // namespace krs::nodes
