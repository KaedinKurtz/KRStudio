// KNode.cpp -- see KNode.hpp. The .knode subgraph document model + versioned JSON round-trip.
#include "KNode.hpp"
#include "KParts.hpp"
#include "KDoc.hpp"          // the shared Node<->JSON codec the interior-node `state` records use
#include "Node.hpp"          // gate: build interior state from real live nodes
#include "NodeFactory.hpp"   // gate: createNode

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QUuid>

#include <cstdio>
#include <cmath>
#include <set>

namespace krs::knode {

std::vector<ExposedPort> KNodeDoc::inputs() const {
    std::vector<ExposedPort> v; for (const auto& p : ports) if (p.isInput) v.push_back(p); return v;
}
std::vector<ExposedPort> KNodeDoc::outputs() const {
    std::vector<ExposedPort> v; for (const auto& p : ports) if (!p.isInput) v.push_back(p); return v;
}

bool KNodeDoc::valid(QString* why) const {
    auto fail = [&](const QString& m) { if (why) *why = m; return false; };
    std::set<QString> ids;
    for (const auto& n : nodes) {
        if (n.id.isEmpty()) return fail(QStringLiteral("an interior node has an empty id"));
        if (!ids.insert(n.id).second) return fail(QStringLiteral("duplicate interior node id: %1").arg(n.id));
        if (n.typeId.isEmpty()) return fail(QStringLiteral("node %1 has no typeId").arg(n.id));
    }
    auto known = [&](const QString& id) { return ids.count(id) != 0; };
    for (const auto& c : connections) {
        if (!known(c.fromNode) || !known(c.toNode))
            return fail(QStringLiteral("connection references a missing node (%1 -> %2)").arg(c.fromNode, c.toNode));
        if (c.fromPort.isEmpty() || c.toPort.isEmpty())
            return fail(QStringLiteral("connection has an empty port name"));
    }
    std::set<QString> inNames, outNames;
    for (const auto& p : ports) {
        if (!known(p.interiorNode))
            return fail(QStringLiteral("exposed port '%1' maps to a missing node %2").arg(p.name, p.interiorNode));
        auto& bag = p.isInput ? inNames : outNames;
        if (!bag.insert(p.name).second)
            return fail(QStringLiteral("duplicate exposed %1 port name: %2").arg(p.isInput ? "input" : "output", p.name));
    }
    return true;
}

namespace {
QString fileSha1(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha1).toHex());
}
} // namespace

QString saveKNode(KNodeDoc& doc, const QString& absPath) {
    if (doc.id.isEmpty()) doc.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject root;
    root["format"] = QStringLiteral("knode/1");
    root["id"] = doc.id; root["name"] = doc.name; root["category"] = doc.category;
    root["revision"] = doc.revision;
    QJsonArray nodes;
    for (const auto& n : doc.nodes) {
        QJsonObject o; o["id"] = n.id; o["typeId"] = n.typeId; o["x"] = n.x; o["y"] = n.y;
        o["state"] = n.state;   // structured krs::kdoc node record (typed params/literals/namedOption/policy)
        nodes.push_back(o);
    }
    root["nodes"] = nodes;
    QJsonArray conns;
    for (const auto& c : doc.connections) {
        QJsonObject o; o["fromNode"] = c.fromNode; o["fromPort"] = c.fromPort;
        o["toNode"] = c.toNode; o["toPort"] = c.toPort; conns.push_back(o);
    }
    root["connections"] = conns;
    QJsonArray ports;
    for (const auto& p : doc.ports) {
        QJsonObject o; o["name"] = p.name; o["node"] = p.interiorNode; o["port"] = p.interiorPort;
        o["dir"] = p.isInput ? QStringLiteral("in") : QStringLiteral("out");
        if (!p.dataType.isEmpty()) o["dataType"] = p.dataType;
        ports.push_back(o);
    }
    root["ports"] = ports;
    QJsonArray nested;                          // nested subgraph deps (content-addressed triples)
    for (const auto& nr : doc.nested) {
        QJsonObject o; o["ref"] = nr.ref; o["id"] = nr.id; o["contentHash"] = nr.contentHash;
        nested.push_back(o);
    }
    root["nested"] = nested;

    QDir().mkpath(QFileInfo(absPath).absolutePath());
    QFile f(absPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return doc.id;
}

bool loadKNode(const QString& absPath, KNodeDoc& out, QString* err) {
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly)) return fail(QStringLiteral("cannot open %1").arg(absPath));
    QJsonParseError perr{};
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !d.isObject()) return fail(QStringLiteral("parse error"));
    const QJsonObject root = d.object();
    const QString fmt = root["format"].toString();
    if (!fmt.startsWith(QLatin1String("knode/")) || fmt.section('/', 1, 1).toInt() != 1)
        return fail(QStringLiteral("unknown .knode format: %1").arg(fmt));

    KNodeDoc doc;
    doc.id = root["id"].toString(); doc.name = root["name"].toString(); doc.category = root["category"].toString();
    doc.revision = root["revision"].toInt(1);
    for (const QJsonValue& v : root["nodes"].toArray()) {
        const QJsonObject o = v.toObject();
        InteriorNode n; n.id = o["id"].toString(); n.typeId = o["typeId"].toString();
        n.x = o["x"].toDouble(); n.y = o["y"].toDouble();
        n.state = o["state"].toObject();   // structured kdoc record (verbatim; applied to a live node by materialize)
        doc.nodes.push_back(std::move(n));
    }
    for (const QJsonValue& v : root["connections"].toArray()) {
        const QJsonObject o = v.toObject();
        Connection c; c.fromNode = o["fromNode"].toString(); c.fromPort = o["fromPort"].toString();
        c.toNode = o["toNode"].toString(); c.toPort = o["toPort"].toString();
        doc.connections.push_back(c);
    }
    for (const QJsonValue& v : root["ports"].toArray()) {
        const QJsonObject o = v.toObject();
        ExposedPort p; p.name = o["name"].toString(); p.interiorNode = o["node"].toString();
        p.interiorPort = o["port"].toString(); p.isInput = (o["dir"].toString() != QLatin1String("out"));
        p.dataType = o["dataType"].toString();
        doc.ports.push_back(p);
    }
    for (const QJsonValue& v : root["nested"].toArray()) {
        const QJsonObject o = v.toObject();
        NestedRef nr; nr.ref = o["ref"].toString(); nr.id = o["id"].toString(); nr.contentHash = o["contentHash"].toString();
        doc.nested.push_back(nr);
    }
    QString why;
    if (!doc.valid(&why)) return fail(QStringLiteral("invalid .knode document: %1").arg(why));
    out = std::move(doc);
    return true;
}

// ================================================================================================
// GATE KNODE
// ================================================================================================
bool runKNodeGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[knode] GATE KNODE -- subgraph .knode on the kdoc codec: LOSSLESS typed params/reconfigure + content hash + library index\n");
    const QString dir = QDir::temp().filePath("krs_knode_gate");
    QDir(dir).removeRecursively();
    const QString path = QDir(dir).filePath("PID_Clamp.knode");
    auto& F = NodeFactory::instance();

    // Author a subgraph from REAL live nodes, each serialized through the kdoc codec (not a hand blob):
    // gen_sine (typed double params) -> twin_property (a reconfigured port layout + namedOption).
    auto sine = F.createNode("gen_sine");
    auto prop = F.createNode("twin_property");
    if (!sine || !prop) { printf("[knode]   createNode returned null -- FAIL\n"); return false; }
    sine->setParam<double>("freq", 2.5); sine->setParam<double>("amp", 1.5);
    prop->selectNamedOption("orientation");                      // -> Quat layout (a Quaternion output port)

    KNodeDoc doc; doc.name = "PID + Clamp"; doc.category = "Control";
    doc.nodes.push_back({ "n1", "gen_sine",      krs::kdoc::nodeToJson(*sine, "gen_sine"),      0,   0 });
    doc.nodes.push_back({ "n2", "twin_property", krs::kdoc::nodeToJson(*prop, "twin_property"), 120, 0 });
    doc.connections.push_back({ "n1", "Out", "n2", "Object" });
    doc.ports.push_back({ "freq_in", "n1", "t",          true,  "double"    });   // typed input
    doc.ports.push_back({ "orient",  "n2", "Quaternion", false, "glm::quat" });   // typed output
    doc.nested.push_back({ "lib/common_filter.knode", "filt-uuid", "deadbeef" }); // a nested dep ref

    const bool structOk = doc.valid();
    const QString id = saveKNode(doc, path);
    const QString h1 = fileSha1(path);
    const bool saved = !id.isEmpty() && QFile::exists(path) && !h1.isEmpty();

    // Reload: structure + exposed-port TYPES + nested refs survive.
    KNodeDoc r; QString err;
    const bool loaded = loadKNode(path, r, &err);
    const bool structMatch = loaded && r.id == doc.id && r.name == doc.name && r.revision == 1
        && r.nodes.size() == 2 && r.connections.size() == 1 && r.ports.size() == 2 && r.nested.size() == 1
        && r.nodes[0].typeId == "gen_sine"
        && r.outputs().size() == 1 && r.outputs()[0].dataType == "glm::quat"        // exposed-port type survives
        && r.nested[0].contentHash == "deadbeef";                                    // nested dep ref survives

    // THE upgrade over the old opaque blob: the interior `state` reconstructs a LIVE node losslessly.
    bool sineOk = false, propOk = false;
    if (loaded) {
        auto s2 = F.createNode("gen_sine");
        if (s2) { krs::kdoc::applyJsonToNode(*s2, r.nodes[0].state, nullptr);
                  sineOk = std::abs(s2->getParam<double>("freq", 0.0) - 2.5) < 1e-12; }
        auto p2 = F.createNode("twin_property");
        if (p2) { krs::kdoc::applyJsonToNode(*p2, r.nodes[1].state, nullptr);
                  for (const auto& pr : p2->getPorts()) if (pr.name == "Quaternion") propOk = true; }  // namedOption survived
    }
    printf("[knode]   round-trip: valid=%s saved=%s struct+types+nested-match=%s ; kdoc state rebuilds live node (freq=2.5=%s reconfigure=%s)  %s\n",
           structOk?"yes":"no", saved?"yes":"no", structMatch?"yes":"no", sineOk?"yes":"no", propOk?"yes":"no",
           (structOk && saved && structMatch && sineOk && propOk) ? "PASS" : "FAIL");

    // Content-hash change detection: edit an interior param + resave -> different hash.
    sine->setParam<double>("freq", 9.9);
    r.nodes[0].state = krs::kdoc::nodeToJson(*sine, "gen_sine");
    saveKNode(r, path);
    const QString h2 = fileSha1(path);
    const bool hashChanged = (h2 != h1) && !h2.isEmpty();
    printf("[knode]   content hash changes on interior edit=%s  %s\n", hashChanged?"yes":"NO", hashChanged?"PASS":"FAIL");

    // Library indexes .knode as PartType::Node.
    krs::parts::PartLibrary lib;
    lib.setSearchRoots({ dir });
    lib.rescan();
    const bool indexed = !lib.byType(krs::parts::PartType::Node).empty()
        && lib.byType(krs::parts::PartType::Node)[0].name == "PID + Clamp";
    printf("[knode]   parts library indexes .knode as Node (name '%s')=%s  %s\n",
           indexed ? lib.byType(krs::parts::PartType::Node)[0].name.toUtf8().constData() : "(none)",
           indexed?"yes":"NO", indexed?"PASS":"FAIL");

    // NEG-CTRLs: a dangling connection is invalid; a corrupt file is refused.
    KNodeDoc bad = doc; bad.connections.push_back({ "n1", "Result", "nX", "In" });
    const bool danglingRejected = !bad.valid();
    KNodeDoc cd; QString cerr;
    { QFile cf(QDir(dir).filePath("corrupt.knode")); cf.open(QIODevice::WriteOnly); cf.write("{ not json"); cf.close(); }
    const bool corruptRefused = !loadKNode(QDir(dir).filePath("corrupt.knode"), cd, &cerr) && !cerr.isEmpty();
    printf("[knode]   NEG-CTRLs: dangling connection rejected=%s ; corrupt file refused=%s  %s\n",
           danglingRejected?"yes":"NO", corruptRefused?"yes":"NO",
           (danglingRejected && corruptRefused) ? "REJECTS(non-vacuous)" : "VACUOUS!");

    const bool pass = structOk && saved && structMatch && sineOk && propOk && hashChanged && indexed
                   && danglingRejected && corruptRefused;
    printf("[knode] %s\n", pass ? "ALL PASS (subgraph .knode round-trips LOSSLESS typed state via kdoc -- rebuilds live nodes incl. reconfigure; exposed-port types + nested refs survive; content-hash detection; library-indexed as Node; dangling/corrupt refused)"
                                : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::knode
