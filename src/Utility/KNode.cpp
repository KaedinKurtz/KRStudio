// KNode.cpp -- see KNode.hpp. The .knode subgraph document model + versioned JSON round-trip.
#include "KNode.hpp"
#include "KParts.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QUuid>

#include <cstdio>
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
QJsonObject paramsToJson(const QString& blob) {
    if (blob.isEmpty()) return {};
    const QJsonDocument d = QJsonDocument::fromJson(blob.toUtf8());
    return d.isObject() ? d.object() : QJsonObject{};
}
} // namespace

QString saveKNode(KNodeDoc& doc, const QString& absPath) {
    if (doc.id.isEmpty()) doc.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject root;
    root["format"] = QStringLiteral("knode/1");
    root["id"] = doc.id; root["name"] = doc.name; root["category"] = doc.category;
    QJsonArray nodes;
    for (const auto& n : doc.nodes) {
        QJsonObject o; o["id"] = n.id; o["typeId"] = n.typeId; o["x"] = n.x; o["y"] = n.y;
        o["params"] = paramsToJson(n.paramsJson);   // embed as a real object (round-trips + human-diffable)
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
        ports.push_back(o);
    }
    root["ports"] = ports;

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
    for (const QJsonValue& v : root["nodes"].toArray()) {
        const QJsonObject o = v.toObject();
        InteriorNode n; n.id = o["id"].toString(); n.typeId = o["typeId"].toString();
        n.x = o["x"].toDouble(); n.y = o["y"].toDouble();
        n.paramsJson = QString::fromUtf8(QJsonDocument(o["params"].toObject()).toJson(QJsonDocument::Compact));
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
        doc.ports.push_back(p);
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
    printf("[knode] GATE KNODE -- subgraph document round-trip + content hash + library index + validity\n");
    const QString dir = QDir::temp().filePath("krs_knode_gate");
    QDir(dir).removeRecursively();
    const QString path = QDir(dir).filePath("PID_Clamp.knode");

    // Author a subgraph: a Compare feeds an If; two exposed inputs, one exposed output.
    KNodeDoc doc; doc.name = "PID + Clamp"; doc.category = "Control";
    doc.nodes.push_back({ "n1", "logic_compare", R"({"op":"gt","threshold":0.5})", 0, 0 });
    doc.nodes.push_back({ "n2", "flow_if",       R"({})",                          120, 0 });
    doc.connections.push_back({ "n1", "Result", "n2", "Condition" });
    doc.ports.push_back({ "value",  "n1", "A",    true  });   // input
    doc.ports.push_back({ "enable", "n2", "In",   true  });   // input
    doc.ports.push_back({ "out",    "n2", "True", false });   // output

    const bool structOk = doc.valid();
    const QString id = saveKNode(doc, path);
    const QString h1 = fileSha1(path);
    const bool saved = !id.isEmpty() && QFile::exists(path) && !h1.isEmpty();

    // Round-trip: reload and compare structure.
    KNodeDoc r; QString err;
    const bool loaded = loadKNode(path, r, &err);
    const bool roundtrip = loaded && r.id == doc.id && r.name == doc.name
        && r.nodes.size() == 2 && r.connections.size() == 1 && r.ports.size() == 3
        && r.nodes[0].typeId == "logic_compare"
        && r.connections[0].fromPort == "Result" && r.connections[0].toPort == "Condition"
        && r.inputs().size() == 2 && r.outputs().size() == 1
        && r.outputs()[0].name == "out";
    // params round-trip (the opaque blob survives as an object)
    const bool paramsOk = loaded && r.nodes[0].paramsJson.contains("threshold");
    printf("[knode]   round-trip: valid=%s saved=%s reload struct-matches=%s params-kept=%s  %s\n",
           structOk?"yes":"no", saved?"yes":"no", roundtrip?"yes":"no", paramsOk?"yes":"no",
           (structOk && saved && roundtrip && paramsOk) ? "PASS" : "FAIL");

    // Content-hash change detection: edit the doc + resave -> different hash (recognize-as-different).
    r.nodes[0].paramsJson = R"({"op":"lt","threshold":0.9})";
    saveKNode(r, path);
    const QString h2 = fileSha1(path);
    const bool hashChanged = (h2 != h1) && !h2.isEmpty();
    printf("[knode]   content hash changes on edit=%s  %s\n", hashChanged?"yes":"NO", hashChanged?"PASS":"FAIL");

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

    const bool pass = structOk && saved && roundtrip && paramsOk && hashChanged && indexed
                   && danglingRejected && corruptRefused;
    printf("[knode] %s\n", pass ? "ALL PASS (subgraph .knode round-trips; content-hash change detection; library-indexed as Node; dangling/corrupt refused)"
                                : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::knode
