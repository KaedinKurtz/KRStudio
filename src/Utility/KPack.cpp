// ===========================================================================
// KPack.cpp -- see KPack.hpp. The .knodepack cross-user subgraph sharing layer:
// bundle a subgraph + its transitive nested closure into one portable file a
// colleague imports into their library and drags from the Add menu.
// ===========================================================================
#include "KPack.hpp"
#include "KNode.hpp"           // KNodeDoc + saveKNode/loadKNode/docFromJson
#include "KParts.hpp"          // PartLibrary / PartEntry / PartType
#include "SubgraphNode.hpp"    // registerDiscoveredKNodes
#include "NodeFactory.hpp"
#include "KDoc.hpp"            // gate: nodeToJson
#include "Node.hpp"            // gate: feed/read

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QByteArray>

#include <cstdio>
#include <cmath>
#include <functional>
#include <set>
#include <string>

namespace krs::kpack {

namespace {
QByteArray readAllBytes(const QString& path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}
QString sha1Hex(const QByteArray& b) {
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex());
}
// path-traversal defense: an id is uuid-ish [A-Za-z0-9-_]; a hash is lowercase hex. Reject anything
// else BEFORE it reaches a filesystem path.
bool safeId(const QString& s) {
    if (s.isEmpty()) return false;
    for (QChar c : s) if (!(c.isLetterOrNumber() || c == '-' || c == '_')) return false;
    return true;
}
bool safeHash(const QString& s) {
    if (s.isEmpty()) return false;
    for (QChar c : s) { const char l = c.toLower().toLatin1(); if (!((l >= '0' && l <= '9') || (l >= 'a' && l <= 'f'))) return false; }
    return true;
}
} // namespace

bool packFormatOk(const QString& fmt) {
    return fmt.startsWith(QLatin1String("kpack/")) && fmt.section('/', 1, 1).toInt() == 1;
}

QStringList collectClosure(const QString& rootKnodeAbsPath, krs::parts::PartLibrary& lib,
                           QStringList* missing, QString* err) {
    QStringList out;
    std::set<QString> visited;
    std::function<void(const QString&)> walk = [&](const QString& path) {
        krs::knode::KNodeDoc doc; QString e;
        if (!krs::knode::loadKNode(path, doc, &e)) { if (err && err->isEmpty()) *err = e; return; }
        if (!doc.id.isEmpty()) { if (visited.count(doc.id)) return; visited.insert(doc.id); }   // cycle guard
        out.push_back(path);
        std::set<QString> deps;
        for (const auto& nr : doc.nested) if (!nr.id.isEmpty()) deps.insert(nr.id);
        for (const auto& n : doc.nodes)
            if (n.typeId.startsWith(QStringLiteral("knode:"))) deps.insert(n.typeId.mid(6));
        for (const QString& depId : deps) {
            if (visited.count(depId)) continue;
            const krs::parts::PartEntry* pe = lib.findById(depId);
            if (!pe) { if (missing && !missing->contains(depId)) *missing << depId; continue; }   // report, don't crash
            walk(pe->absPath);
        }
    };
    walk(rootKnodeAbsPath);
    return out;
}

QString writePack(const QString& rootKnodeAbsPath, krs::parts::PartLibrary& lib,
                  const QString& outAbsPath, QString* err) {
    QStringList missing;
    const QStringList paths = collectClosure(rootKnodeAbsPath, lib, &missing, err);
    if (paths.isEmpty()) { if (err && err->isEmpty()) *err = QStringLiteral("empty closure"); return {}; }

    krs::knode::KNodeDoc rootDoc; krs::knode::loadKNode(rootKnodeAbsPath, rootDoc, nullptr);

    QJsonObject root;
    root["format"] = QStringLiteral("kpack/1");
    root["authorTool"] = QStringLiteral("KRStudio");
    QJsonObject rootRef; rootRef["id"] = rootDoc.id;
    QJsonArray entries;
    for (const QString& p : paths) {
        const QByteArray bytes = readAllBytes(p);
        if (bytes.isEmpty()) continue;
        krs::knode::KNodeDoc d; krs::knode::loadKNode(p, d, nullptr);
        QJsonObject e;
        e["id"] = d.id; e["contentHash"] = sha1Hex(bytes); e["ext"] = QStringLiteral(".knode");
        e["name"] = d.name; e["category"] = d.category; e["revision"] = d.revision;
        e["description"] = d.description;   // the manifest text now round-trips (was written empty)
        e["bytes"] = QString::fromLatin1(bytes.toBase64());   // VERBATIM original bytes (hash-preserving)
        if (d.id == rootDoc.id) rootRef["contentHash"] = e["contentHash"];
        entries.push_back(e);
    }
    root["root"] = rootRef;
    root["entries"] = entries;
    if (!missing.isEmpty()) root["missingDeps"] = QJsonArray::fromStringList(missing);

    QDir().mkpath(QFileInfo(outAbsPath).absolutePath());
    QFile f(outAbsPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { if (err) *err = QStringLiteral("cannot write %1").arg(outAbsPath); return {}; }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return rootDoc.id;
}

bool loadPack(const QString& absPath, PackDoc& out, QString* err) {
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly)) return fail(QStringLiteral("cannot open %1").arg(absPath));
    QJsonParseError perr{};
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !d.isObject()) return fail(QStringLiteral("parse error"));
    const QJsonObject root = d.object();
    if (!packFormatOk(root["format"].toString())) return fail(QStringLiteral("unknown .knodepack format: %1").arg(root["format"].toString()));

    PackDoc pack;
    pack.rootId   = root["root"].toObject()["id"].toString();
    pack.rootHash = root["root"].toObject()["contentHash"].toString();
    for (const QJsonValue& v : root["entries"].toArray()) {
        const QJsonObject o = v.toObject();
        PackEntry e;
        e.id = o["id"].toString(); e.contentHash = o["contentHash"].toString(); e.ext = o["ext"].toString();
        e.name = o["name"].toString(); e.category = o["category"].toString();
        e.description = o["description"].toString(); e.revision = o["revision"].toInt(1);
        e.bytes = QByteArray::fromBase64(o["bytes"].toString().toLatin1());
        // integrity: the embedded bytes must hash to the claimed content-address.
        if (sha1Hex(e.bytes) != e.contentHash)
            return fail(QStringLiteral("member %1 failed the content-hash check (tampered?)").arg(e.id));
        // structural: the bytes must be a valid .knode document.
        krs::knode::KNodeDoc tmp; QString kerr;
        const QJsonDocument md = QJsonDocument::fromJson(e.bytes);
        if (!md.isObject() || !krs::knode::docFromJson(md.object(), tmp, QStringLiteral("knode"), &kerr))
            return fail(QStringLiteral("member %1 is not a valid .knode: %2").arg(e.id, kerr));
        pack.entries.push_back(std::move(e));
    }
    out = std::move(pack);
    return true;
}

bool installPack(const PackDoc& pack, const QString& libRoot, InstallReport& rep, QString* err) {
    const QString destDir = QDir(libRoot).filePath("imported");
    QDir().mkpath(destDir);
    for (const auto& e : pack.entries) {
        if (!safeId(e.id) || !safeHash(e.contentHash)) {   // path-traversal defense
            rep.warnings << QStringLiteral("rejected member with an unsafe id/hash (\"%1\")").arg(e.id);
            continue;
        }
        const QString dest = QDir(destDir).filePath(e.id + QStringLiteral("_") + e.contentHash + e.ext);
        if (QFile::exists(dest)) { ++rep.skippedDuplicate; continue; }   // content-addressed dedupe
        QFile f(dest);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { if (err) *err = QStringLiteral("cannot write %1").arg(dest); return false; }
        f.write(e.bytes);   // VERBATIM (hash-preserving)
        ++rep.installed;
    }
    return true;
}

bool importPack(const QString& absPath, const QString& libRoot, krs::parts::PartLibrary& lib,
                InstallReport& rep, QString* err) {
    PackDoc pack;
    if (!loadPack(absPath, pack, err)) return false;
    if (!installPack(pack, libRoot, rep, err)) return false;

    QStringList roots = lib.searchRoots(); if (!roots.contains(libRoot)) roots.prepend(libRoot);
    lib.setSearchRoots(roots);
    lib.rescan();
    const int n = krs::nodes::registerDiscoveredKNodes(lib);
    (void)n;

    // Report interior typeIds an imported member needs but the recipient still lacks (knode: OR built-in).
    const auto& reg = NodeFactory::instance().getRegisteredNodeTypes();
    for (const auto& pe : lib.byType(krs::parts::PartType::Node)) {
        rep.registered << (QStringLiteral("knode:") + pe.id);
        krs::knode::KNodeDoc d;
        if (!krs::knode::loadKNode(pe.absPath, d, nullptr)) continue;
        for (const auto& in : d.nodes)
            if (reg.count(in.typeId.toStdString()) == 0 && !rep.missingDeps.contains(in.typeId))
                rep.missingDeps << in.typeId;
    }
    return true;
}

// ================================================================================================
// GATE KPACK
// ================================================================================================
namespace {
void feedD(Node& n, const std::string& port, double v) {
    PortDataPacket pk; pk.data = v; pk.type = { "double", "unitless" }; n.setInput(port, pk);
}
double readD(Node& n, const std::string& port) {
    if (const PortDataPacket* pk = n.outputPacket(port)) { try { return std::any_cast<double>(pk->data); } catch (...) {} }
    return std::nan("");
}
} // namespace

bool runKPackGate() {
    using std::printf;
    using namespace krs::knode;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[kpack] GATE KPACK -- share a subgraph + nested closure as one .knodepack; a fresh library imports + uses it\n");
    auto& F = NodeFactory::instance();
    const QString dir = QDir::temp().filePath("krs_kpack_gate");
    QDir(dir).removeRecursively();
    const QString authorRoot = QDir(dir).filePath("author");
    const QString packPath   = QDir(dir).filePath("nested.knodepack");
    bool allOk = true;

    // ---- author a nested subgraph on disk: A (leaf, 2x+1) and B (nests knode:A then affine gain 3) ----
    auto affA = F.createNode("math_affine"); if (!affA) { printf("[kpack] math_affine missing -- FAIL\n"); return false; }
    affA->setParam<double>("gain", 2.0); affA->setParam<double>("offset", 1.0);
    KNodeDoc A; A.id = "Apack"; A.name = "LeafGain"; A.category = "Custom";
    { InteriorNode m; m.id = "m"; m.typeId = "math_affine"; m.state = krs::kdoc::nodeToJson(*affA, "math_affine"); A.nodes.push_back(m); }
    A.ports.push_back({ "x", "m", "In",  true,  "double" });
    A.ports.push_back({ "y", "m", "Out", false, "double" });
    saveKNode(A, QDir(authorRoot).filePath("A.knode"));

    auto affB = F.createNode("math_affine"); affB->setParam<double>("gain", 3.0); affB->setParam<double>("offset", 0.0);
    KNodeDoc B; B.id = "Bpack"; B.name = "NestedShare"; B.category = "Custom";
    { InteriorNode a; a.id = "a"; a.typeId = "knode:Apack"; a.state = QJsonObject{}; B.nodes.push_back(a); }
    { InteriorNode g; g.id = "g"; g.typeId = "math_affine"; g.state = krs::kdoc::nodeToJson(*affB, "math_affine"); B.nodes.push_back(g); }
    B.connections.push_back({ "a", "y", "g", "In" });
    B.ports.push_back({ "x", "a", "x",   true,  "double" });
    B.ports.push_back({ "y", "g", "Out", false, "double" });
    B.nested.push_back({ "A.knode", "Apack", "" });   // dependency ref
    saveKNode(B, QDir(authorRoot).filePath("B.knode"));

    // index the author library + pack B (NO NodeFactory registration on the author side).
    krs::parts::PartLibrary authorLib; authorLib.setSearchRoots({ authorRoot }); authorLib.rescan();
    QString perr;
    const QString packedRoot = writePack(QDir(authorRoot).filePath("B.knode"), authorLib, packPath, &perr);
    PackDoc pd; QString lerr;
    const bool loadedPack = loadPack(packPath, pd, &lerr);
    const bool packOk = packedRoot == "Bpack" && QFile::exists(packPath)
        && loadedPack && pd.entries.size() == 2 && pd.rootId == "Bpack";
    printf("[kpack]   pack B -> closure {B,A}: root=%s entries=%d verified=%s  %s\n",
           packedRoot.toUtf8().constData(), int(pd.entries.size()), loadedPack ? "yes" : "no", packOk ? "PASS" : "FAIL");
    allOk = allOk && packOk;

    // ---- a FRESH recipient library imports the ONE file; knode:B registers AND evaluates (nested A resolves) ----
    const QString recipientRoot = QDir(dir).filePath("recipient");
    krs::parts::PartLibrary recipLib;
    InstallReport rep;
    const bool imported = importPack(packPath, recipientRoot, recipLib, rep, &perr);
    bool evalOk = false;
    if (auto sg = F.createNode("knode:Bpack")) {
        feedD(*sg, "x", 5.0); sg->process();
        evalOk = std::abs(readD(*sg, "y") - 33.0) < 1e-9;   // 3*(2*5+1)
    }
    const bool importOk = imported && rep.installed == 2 && rep.registered.contains("knode:Bpack") && evalOk;
    printf("[kpack]   recipient import: installed=%d registered knode:Bpack=%s evaluates(x=5->y=33)=%s  %s\n",
           rep.installed, rep.registered.contains("knode:Bpack") ? "yes" : "no", evalOk ? "yes" : "no",
           importOk ? "PASS" : "FAIL");
    allOk = allOk && importOk;

    // ---- re-import de-dupes (content-addressed) ----
    krs::parts::PartLibrary recipLib2; InstallReport rep2;
    importPack(packPath, recipientRoot, recipLib2, rep2, &perr);
    const bool dedupeOk = rep2.installed == 0 && rep2.skippedDuplicate == 2;
    printf("[kpack]   re-import de-dupes: installed=%d skipped=%d  %s\n",
           rep2.installed, rep2.skippedDuplicate, dedupeOk ? "PASS" : "FAIL");
    allOk = allOk && dedupeOk;

    // ---- NEG-CTRLs: tampered member / unknown major / hostile id (path traversal) / self-ref closure ----
    bool tamperRej = false, majorRej = false, traversalRej = false, cycleSafe = false;
    {
        // (a) tamper: flip a char in an entry's base64 -> sha1 mismatch -> loadPack refuses.
        QByteArray raw = readAllBytes(packPath);
        QJsonObject o = QJsonDocument::fromJson(raw).object();
        QJsonArray es = o["entries"].toArray();
        { QJsonObject e0 = es[0].toObject(); QString b = e0["bytes"].toString(); b[10] = (b[10] == 'A' ? 'B' : 'A'); e0["bytes"] = b; es[0] = e0; }
        o["entries"] = es;
        const QString tPath = QDir(dir).filePath("tampered.knodepack");
        { QFile tf(tPath); tf.open(QIODevice::WriteOnly); tf.write(QJsonDocument(o).toJson()); }
        PackDoc td; QString terr; tamperRej = !loadPack(tPath, td, &terr) && terr.contains("content-hash");

        // (b) unknown major.
        QJsonObject o2 = QJsonDocument::fromJson(raw).object(); o2["format"] = "kpack/2";
        const QString mPath = QDir(dir).filePath("badmajor.knodepack");
        { QFile mf(mPath); mf.open(QIODevice::WriteOnly); mf.write(QJsonDocument(o2).toJson()); }
        PackDoc md; QString merr; majorRej = !loadPack(mPath, md, &merr);

        // (c) hostile id -> installPack must reject before writing outside the library root.
        PackDoc hostile; PackEntry he;
        he.bytes = "{}"; he.contentHash = sha1Hex(he.bytes); he.id = "../evil"; he.ext = ".knode";
        hostile.entries.push_back(he);
        const QString hostRoot = QDir(dir).filePath("hostroot");
        InstallReport hr; installPack(hostile, hostRoot, hr, nullptr);
        traversalRej = hr.installed == 0 && !hr.warnings.isEmpty()
            && !QFile::exists(QDir(dir).filePath("evil_" + he.contentHash + ".knode"));

        // (d) self-referential nest -> collectClosure must not loop.
        KNodeDoc C; C.id = "Cself"; C.name = "SelfNest"; C.category = "Custom";
        { InteriorNode s; s.id = "s"; s.typeId = "knode:Cself"; s.state = QJsonObject{}; C.nodes.push_back(s); }
        C.nested.push_back({ "C.knode", "Cself", "" });
        saveKNode(C, QDir(authorRoot).filePath("C.knode"));
        authorLib.rescan();
        QStringList miss; QString cerr;
        const QStringList closure = collectClosure(QDir(authorRoot).filePath("C.knode"), authorLib, &miss, &cerr);
        cycleSafe = closure.size() == 1;   // reached here (no hang) + visited-guard stopped the self-recursion
    }
    const bool negOk = tamperRej && majorRej && traversalRej && cycleSafe;
    printf("[kpack]   NEG-CTRLs: tamper-rejected=%s unknown-major-rejected=%s hostile-id-blocked=%s self-nest-no-loop=%s  %s\n",
           tamperRej?"yes":"no", majorRej?"yes":"no", traversalRej?"yes":"no", cycleSafe?"yes":"no",
           negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");
    allOk = allOk && negOk;

    printf("[kpack] %s\n", allOk ? "ALL PASS (a subgraph + its nested closure packs to one .knodepack; a fresh library imports the single file, both members land + register, the shared subgraph evaluates end-to-end; re-import de-dupes; tamper/major/path-traversal/cycle neg-ctrls)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return allOk;
}

} // namespace krs::kpack
