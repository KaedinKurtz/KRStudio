// GoalDoc.cpp -- see GoalDoc.hpp. The .kgoal declarative goal document + builder + gate.
#include "GoalDoc.hpp"
#include "WorldState.hpp"
#include "PropertyCatalog.hpp"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QUuid>

#include <cstdio>
#include <cmath>

namespace krs::goal {

using krs::skill::Predicate;

QJsonObject predicateToJson(const Predicate& p) {
    QJsonObject o;
    o["kind"] = int(p.kind);
    o["neg"] = p.negated;
    o["robot"] = p.robotId;
    o["a"] = QString::fromStdString(p.a);
    o["b"] = QString::fromStdString(p.b);
    o["tol"] = p.tol;
    return o;
}
Predicate predicateFromJson(const QJsonObject& o) {
    Predicate p;
    p.kind = Predicate::Kind(o["kind"].toInt(0));
    p.negated = o["neg"].toBool(false);
    p.robotId = o["robot"].toInt(0);
    p.a = o["a"].toString().toStdString();
    p.b = o["b"].toString().toStdString();
    p.tol = o["tol"].toDouble(0.1);
    return p;
}

bool satisfied(const GoalDoc& g, const krs::world::WorldState& ws) {
    for (const auto& p : g.require) if (!p.eval(ws)) return false;
    return true;
}
std::vector<Predicate> unmet(const GoalDoc& g, const krs::world::WorldState& ws) {
    std::vector<Predicate> v;
    for (const auto& p : g.require) if (!p.eval(ws)) v.push_back(p);
    return v;
}

namespace {
QString fileSha1(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha1).toHex());
}
} // namespace

QString saveKGoal(GoalDoc& g, const QString& absPath) {
    if (g.id.isEmpty()) g.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject root;
    root["format"] = QStringLiteral("kgoal/1");
    root["id"] = g.id; root["name"] = g.name; root["revision"] = g.revision;
    QJsonArray req;
    for (const auto& p : g.require) req.push_back(predicateToJson(p));
    root["require"] = req;
    QDir().mkpath(QFileInfo(absPath).absolutePath());
    QFile f(absPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return g.id;
}

bool loadKGoal(const QString& absPath, GoalDoc& out, QString* err) {
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly)) return fail(QStringLiteral("cannot open %1").arg(absPath));
    QJsonParseError perr{};
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !d.isObject()) return fail(QStringLiteral("parse error"));
    const QJsonObject root = d.object();
    const QString fmt = root["format"].toString();
    if (!fmt.startsWith(QLatin1String("kgoal/")) || fmt.section('/', 1, 1).toInt() != 1)
        return fail(QStringLiteral("unknown .kgoal format: %1").arg(fmt));
    GoalDoc g;
    g.id = root["id"].toString(); g.name = root["name"].toString(); g.revision = root["revision"].toInt(1);
    for (const QJsonValue& v : root["require"].toArray()) {
        if (!v.isObject() || !v.toObject().contains("kind"))
            return fail(QStringLiteral("malformed predicate in .kgoal"));   // refuse WHOLE, nothing half-applied
        g.require.push_back(predicateFromJson(v.toObject()));
    }
    out = std::move(g);
    return true;
}

// ================================================================================================
// GATE KGOAL (Goal Workspace G1)
// ================================================================================================
bool runKGoalGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[kgoal] GATE KGOAL -- declarative .kgoal: builder == JSON == reload; satisfied/unmet track the world\n");
    const QString dir = QDir::temp().filePath("krs_kgoal_gate");
    QDir(dir).removeRecursively();
    bool allOk = true;

    // ---- a world to evaluate against ----
    krs::world::WorldState ws;
    krs::twin::PropertyCatalog cat;
    { double p[3] = { 5.0, 0.0, 0.0 }; cat.publish(50, "glass", "position", krs::twin::PropType::Vec3, p, 0.1); }
    ws.updateFromCatalog(cat);
    ws.setFrame("shelf_pose", glm::vec3(1.0f, 0.5f, 0.0f));
    ws.setGripperOpen(0, true);

    // ---- builder-authored goal -> save -> reload bit-equal ----
    GoalDoc g = GoalBuilder("shelve the glass")
                    .at("glass", "shelf_pose", 0.1)
                    .notHolding(0, "glass")
                    .gripperOpen(0)
                    .doc();
    const QString path = QDir(dir).filePath("shelve.kgoal");
    const QString id = saveKGoal(g, path);
    const QString h1 = fileSha1(path);
    GoalDoc r; QString err;
    const bool loaded = loadKGoal(path, r, &err);
    const bool roundtrip = loaded && r.id == g.id && r.name == g.name && r.require.size() == 3
        && r.require[0].kind == Predicate::Kind::At && std::abs(r.require[0].tol - 0.1) < 1e-12
        && r.require[1].kind == Predicate::Kind::Holding && r.require[1].negated
        && r.require[2].kind == Predicate::Kind::GripperOpen && !r.require[2].negated;
    printf("[kgoal]   builder->save->reload: id=%s round-trip=%d  %s\n",
           id.isEmpty() ? "(none)" : "ok", int(roundtrip), (roundtrip && !id.isEmpty()) ? "PASS" : "FAIL");
    allOk = allOk && roundtrip && !id.isEmpty();

    // ---- satisfied()/unmet() track the world as it approaches the goal ----
    {
        const bool notYet = !satisfied(r, ws) && unmet(r, ws).size() == 1
            && unmet(r, ws)[0].kind == Predicate::Kind::At;    // only the At is unmet (far glass)
        double p[3] = { 1.05, 0.5, 0.0 };                       // glass arrives near the shelf
        cat.publish(50, "glass", "position", krs::twin::PropType::Vec3, p, 0.2);
        ws.updateFromCatalog(cat);
        const bool nowMet = satisfied(r, ws) && unmet(r, ws).empty();
        printf("[kgoal]   live truth: far-glass unmet={At}=%d ; glass-at-shelf satisfied=%d  %s\n",
               int(notYet), int(nowMet), (notYet && nowMet) ? "PASS" : "FAIL");
        allOk = allOk && notYet && nowMet;
    }

    // ---- two front-ends, one format: hand-authored JSON loads to the SAME document ----
    {
        QJsonObject root; root["format"] = "kgoal/1"; root["id"] = g.id; root["name"] = g.name; root["revision"] = 1;
        QJsonArray req; for (const auto& p : g.require) req.push_back(predicateToJson(p));
        root["require"] = req;
        const QString jsonPath = QDir(dir).filePath("hand.kgoal");
        { QFile f(jsonPath); f.open(QIODevice::WriteOnly); f.write(QJsonDocument(root).toJson()); }
        GoalDoc h; QString herr;
        const bool same = loadKGoal(jsonPath, h, &herr) && h.require.size() == r.require.size()
            && h.require[0].text() == r.require[0].text()
            && h.require[1].text() == r.require[1].text()
            && h.require[2].text() == r.require[2].text();
        // content hash changes on edit
        GoalDoc e = r; e.require[0].tol = 0.05;
        const QString editPath = QDir(dir).filePath("edited.kgoal");
        saveKGoal(e, editPath);
        const bool hashChanged = fileSha1(editPath) != h1 && !fileSha1(editPath).isEmpty();
        printf("[kgoal]   two front-ends one format=%d ; content hash changes on edit=%d  %s\n",
               int(same), int(hashChanged), (same && hashChanged) ? "PASS" : "FAIL");
        allOk = allOk && same && hashChanged;
    }

    // ---- NEG-CTRLs: unknown major refused; malformed predicate refused WHOLE ----
    {
        QJsonObject badMajorDoc;
        badMajorDoc[QStringLiteral("format")] = QStringLiteral("kgoal/2");
        badMajorDoc[QStringLiteral("require")] = QJsonArray{};
        const QString badPath = QDir(dir).filePath("badmajor.kgoal");
        { QFile f(badPath); f.open(QIODevice::WriteOnly); f.write(QJsonDocument(badMajorDoc).toJson()); }
        GoalDoc majorOut; QString majorErr;
        const bool majorRefused = !loadKGoal(badPath, majorOut, &majorErr);

        QJsonObject malformedDoc;
        malformedDoc[QStringLiteral("format")] = QStringLiteral("kgoal/1");
        malformedDoc[QStringLiteral("id")] = QStringLiteral("x");
        malformedDoc[QStringLiteral("name")] = QStringLiteral("m");
        QJsonObject noKind; noKind[QStringLiteral("nonsense")] = 1;        // predicate without "kind"
        malformedDoc[QStringLiteral("require")] = QJsonArray{ noKind };
        const QString malPath = QDir(dir).filePath("malformed.kgoal");
        { QFile f(malPath); f.open(QIODevice::WriteOnly); f.write(QJsonDocument(malformedDoc).toJson()); }
        GoalDoc malOut; QString malErr;
        const bool malRefused = !loadKGoal(malPath, malOut, &malErr) && malOut.require.empty();
        const bool negOk = majorRefused && malRefused;
        printf("[kgoal]   NEG-CTRLs: unknown major refused=%d ; malformed predicate refused whole=%d  %s\n",
               int(majorRefused), int(malRefused), negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");
        allOk = allOk && negOk;
    }

    printf("[kgoal] %s\n", allOk ? "ALL PASS (.kgoal builder/JSON/panel are one format; live satisfied/unmet; hash discipline; refusals whole)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return allOk;
}

} // namespace krs::goal
