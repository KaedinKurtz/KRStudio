// AuthoringPersist.cpp -- see AuthoringPersist.hpp. Versioned JSON document for the authored
// RobotGraph + mate connectors/constraints, with a strict no-mis-bind overlay contract.
#include "AuthoringPersist.hpp"
#include "RobotBuilder.hpp"        // RobotGraph / RBJoint / MateConnector helpers
#include "RobotBuilderScene.hpp"   // buildDemoGraph / spawnGraphBodies (gate)
#include "Scene.hpp"
#include "components.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QCoreApplication>

#include <cstdio>
#include <algorithm>

namespace krs::persist {
namespace {

constexpr int kVersion = 1;

QJsonArray vec3ToJson(const glm::vec3& v) { return QJsonArray{ double(v.x), double(v.y), double(v.z) }; }
glm::vec3 vec3FromJson(const QJsonValue& v, const glm::vec3& fb = glm::vec3(0)) {
    const QJsonArray a = v.toArray();
    if (a.size() != 3) return fb;
    return { float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()) };
}
// 64-bit ids as strings: QJson doubles lose precision past 2^53.
QString u64ToJson(std::uint64_t v) { return QString::number(qulonglong(v)); }
std::uint64_t u64FromJson(const QJsonValue& v) { return std::uint64_t(v.toString().toULongLong()); }

// (graph body index, solid slot) <-> entity. Slot 0 = the body's primary entity; slot k>0 =
// extraEntities[k-1]. Stable across a re-import of the same STEP (deterministic chain builder).
bool entityToRef(const krs::rbuild::RobotGraph& g, entt::entity e, int& body, int& slot) {
    const int eid = int(static_cast<std::uint32_t>(e));
    for (int b = 0; b < int(g.bodies.size()); ++b) {
        if (g.bodies[b].entity == eid) { body = b; slot = 0; return true; }
        for (int k = 0; k < int(g.bodies[b].extraEntities.size()); ++k)
            if (g.bodies[b].extraEntities[k] == eid) { body = b; slot = k + 1; return true; }
    }
    return false;
}
entt::entity refToEntity(const krs::rbuild::RobotGraph& g, int body, int slot) {
    if (body < 0 || body >= int(g.bodies.size())) return entt::null;
    const auto& b = g.bodies[body];
    int eid = -1;
    if (slot == 0) eid = b.entity;
    else if (slot - 1 < int(b.extraEntities.size())) eid = b.extraEntities[slot - 1];
    return (eid >= 0) ? entt::entity(static_cast<std::uint32_t>(eid)) : entt::null;
}

} // namespace

std::string defaultAuthoringPath() {
    const QString dir = QCoreApplication::applicationDirPath().isEmpty()
                            ? QDir::currentPath() : QCoreApplication::applicationDirPath();
    return QDir(dir).filePath(QStringLiteral("robot_authoring.json")).toStdString();
}

bool saveAuthoring(entt::registry& reg, const krs::rbuild::RobotGraph& g,
                   const std::string& source, const std::string& filePath)
{
    QJsonObject root;
    root["version"]   = kVersion;
    root["source"]    = QString::fromStdString(source);
    root["robotId"]   = g.robotId;
    root["bodyCount"] = int(g.bodies.size());
    root["base"]      = g.base;
    root["nextJointId"] = u64ToJson(g.nextJointId);
    root["nextNodeId"]  = g.nextNodeId;

    QJsonArray joints;
    for (const auto& j : g.joints) {
        QJsonObject o;
        o["id"] = u64ToJson(j.id); o["nodeId"] = j.nodeId; o["name"] = QString::fromStdString(j.name);
        o["type"] = int(j.type); o["parent"] = j.parent; o["child"] = j.child;
        o["prov"] = int(j.prov); o["ambiguous"] = j.ambiguous; o["residual"] = j.residual;
        o["axisPos"] = vec3ToJson(j.axisPos); o["axisDir"] = vec3ToJson(j.axisDir); o["refDir"] = vec3ToJson(j.refDir);
        QJsonObject lim;
        lim["lower"] = j.limits.lower; lim["upper"] = j.limits.upper;
        lim["effort"] = j.limits.effort; lim["velocity"] = j.limits.velocity; lim["enabled"] = j.limits.enabled;
        o["limits"] = lim;
        joints.push_back(o);
    }
    root["joints"] = joints;

    // Mate connectors: every MateConnectorComponent whose entity maps to a body of THIS graph.
    QJsonArray conns;
    for (auto e : reg.view<MateConnectorComponent>()) {
        int body = -1, slot = -1;
        if (!entityToRef(g, e, body, slot)) continue;      // belongs to another robot / loose entity
        const auto& mc = reg.get<MateConnectorComponent>(e);
        QJsonObject o;
        o["body"] = body; o["slot"] = slot; o["nextId"] = int(mc.nextConnectorId);
        QJsonArray list;
        for (const auto& c : mc.connectors) {
            QJsonObject co;
            co["id"] = int(c.id); co["name"] = QString::fromStdString(c.name);
            co["pos"] = vec3ToJson(c.localPos); co["z"] = vec3ToJson(c.localZ); co["x"] = vec3ToJson(c.localX);
            co["key"] = u64ToJson(c.sourceFaceKey); co["ftype"] = c.sourceFaceType; co["radius"] = double(c.radius);
            list.push_back(co);
        }
        o["connectors"] = list;
        conns.push_back(o);
    }
    root["connectorSets"] = conns;

    QJsonArray mates;
    std::uint64_t nextMateId = 1;
    if (const auto* mg = reg.ctx().find<MateGraphComponent>()) {
        nextMateId = mg->nextMateId;
        for (const auto& m : mg->mates) {
            int bA = -1, sA = -1, bB = -1, sB = -1;
            if (!entityToRef(g, m.bodyA, bA, sA) || !entityToRef(g, m.bodyB, bB, sB)) continue;
            QJsonObject o;
            o["id"] = u64ToJson(m.id); o["type"] = int(m.type);
            o["bodyA"] = bA; o["slotA"] = sA; o["connA"] = int(m.connA);
            o["bodyB"] = bB; o["slotB"] = sB; o["connB"] = int(m.connB);
            o["offset"] = m.offset; o["angle"] = m.angle;
            mates.push_back(o);
        }
    }
    root["mates"] = mates;
    root["nextMateId"] = u64ToJson(nextMateId);

    QFile f(QString::fromStdString(filePath));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool loadAuthoringOverlay(entt::registry& reg, krs::rbuild::RobotGraph& g,
                          const std::string& source, const std::string& filePath)
{
    QFile f(QString::fromStdString(filePath));
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const QJsonObject root = doc.object();

    // STRICT MATCH: never overlay onto a graph the document was not built for -- a wrong overlay
    // would silently bind joints/connectors to the wrong bodies, which is worse than fresh inference.
    if (root["version"].toInt() != kVersion) return false;
    if (root["source"].toString() != QString::fromStdString(source)) return false;
    if (root["bodyCount"].toInt() != int(g.bodies.size())) return false;

    const QJsonArray joints = root["joints"].toArray();
    std::vector<krs::rbuild::RBJoint> parsed;
    parsed.reserve(std::size_t(joints.size()));
    for (const QJsonValue& jv : joints) {
        const QJsonObject o = jv.toObject();
        krs::rbuild::RBJoint j;
        j.id = u64FromJson(o["id"]); j.nodeId = o["nodeId"].toInt(-1); j.name = o["name"].toString().toStdString();
        const int t = o["type"].toInt(0);
        j.type = (t == 1) ? krs::rbuild::JType::Prismatic : (t == 2) ? krs::rbuild::JType::Fixed
                                                                     : krs::rbuild::JType::Revolute;
        j.parent = o["parent"].toInt(-1); j.child = o["child"].toInt(-1);
        if (j.parent < 0 || j.parent >= int(g.bodies.size()) ||
            j.child  < 0 || j.child  >= int(g.bodies.size()) || j.parent == j.child)
            return false;                                  // malformed document -> refuse whole overlay
        j.prov = (o["prov"].toInt(0) == 1) ? krs::rbuild::Prov::Manual : krs::rbuild::Prov::Inferred;
        j.ambiguous = o["ambiguous"].toBool(false);
        j.residual  = o["residual"].toDouble(0.0);
        j.axisPos = vec3FromJson(o["axisPos"]); j.axisDir = vec3FromJson(o["axisDir"], { 0, 0, 1 });
        j.refDir  = vec3FromJson(o["refDir"],  { 1, 0, 0 });
        const QJsonObject lim = o["limits"].toObject();
        j.limits.lower = lim["lower"].toDouble(-3.14159265); j.limits.upper = lim["upper"].toDouble(3.14159265);
        j.limits.effort = lim["effort"].toDouble(0.0); j.limits.velocity = lim["velocity"].toDouble(0.0);
        j.limits.enabled = lim["enabled"].toBool(true);
        parsed.push_back(std::move(j));
    }

    // Document validated -> apply. Joint list is REPLACED (the persisted list IS the user's edit
    // history applied to this same source); id/nodeId counters advance past every restored id.
    g.joints = std::move(parsed);
    g.base = std::clamp(root["base"].toInt(0), 0, int(g.bodies.size()) - 1);
    g.nextJointId = std::max<std::uint64_t>(u64FromJson(root["nextJointId"]), 1);
    g.nextNodeId  = std::max(root["nextNodeId"].toInt(0), 0);
    for (const auto& j : g.joints) {
        g.nextJointId = std::max(g.nextJointId, j.id + 1);
        g.nextNodeId  = std::max(g.nextNodeId,  j.nodeId + 1);
    }

    for (const QJsonValue& sv : root["connectorSets"].toArray()) {
        const QJsonObject o = sv.toObject();
        const entt::entity e = refToEntity(g, o["body"].toInt(-1), o["slot"].toInt(-1));
        if (e == entt::null || !reg.valid(e)) continue;    // body has no live entity -> skip set
        auto& mc = reg.get_or_emplace<MateConnectorComponent>(e);
        mc.connectors.clear();
        std::uint32_t maxId = 0;
        for (const QJsonValue& cv : o["connectors"].toArray()) {
            const QJsonObject co = cv.toObject();
            MateConnector c;
            c.id = std::uint32_t(co["id"].toInt(0)); c.name = co["name"].toString().toStdString();
            c.localPos = vec3FromJson(co["pos"]); c.localZ = vec3FromJson(co["z"], { 0, 0, 1 });
            c.localX = vec3FromJson(co["x"], { 1, 0, 0 });
            c.sourceFaceKey = u64FromJson(co["key"]); c.sourceFaceType = co["ftype"].toInt(1);
            c.radius = float(co["radius"].toDouble(0.0));
            maxId = std::max(maxId, c.id);
            mc.connectors.push_back(std::move(c));
        }
        mc.nextConnectorId = std::max(std::uint32_t(o["nextId"].toInt(1)), maxId + 1);
    }

    auto* mgp = reg.ctx().find<MateGraphComponent>();
    if (!mgp) mgp = &reg.ctx().emplace<MateGraphComponent>();
    mgp->mates.clear();
    std::uint64_t maxMate = 0;
    for (const QJsonValue& mv : root["mates"].toArray()) {
        const QJsonObject o = mv.toObject();
        MateConstraint m;
        m.id = u64FromJson(o["id"]);
        const int t = o["type"].toInt(0);
        m.type = (t >= 0 && t <= 4) ? MateConstraint::Type(t) : MateConstraint::Type::Revolute;
        m.bodyA = refToEntity(g, o["bodyA"].toInt(-1), o["slotA"].toInt(-1)); m.connA = std::uint32_t(o["connA"].toInt(0));
        m.bodyB = refToEntity(g, o["bodyB"].toInt(-1), o["slotB"].toInt(-1)); m.connB = std::uint32_t(o["connB"].toInt(0));
        m.offset = o["offset"].toDouble(0.0); m.angle = o["angle"].toDouble(0.0);
        if (m.bodyA == entt::null || m.bodyB == entt::null) continue;
        maxMate = std::max(maxMate, m.id);
        mgp->mates.push_back(m);
    }
    mgp->nextMateId = std::max<std::uint64_t>(u64FromJson(root["nextMateId"]), maxMate + 1);
    return true;
}

// ===========================================================================
// GATE PERSIST -- edit -> save -> fresh scene -> overlay: joints (identity/type/limits/frames/
// provenance), connectors (per body+slot), and mates (entity-resolved) all round-trip.
// NEG-CTRLs: wrong source / wrong body count / corrupt file refuse with the graph UNTOUCHED.
// ===========================================================================
bool runPersistGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[persist] GATE PERSIST -- authoring survives restart: save -> fresh scene -> overlay round-trip\n");
    const std::string path = (QDir::temp().filePath("krs_persist_gate.json")).toStdString();

    // ---- author a session: demo graph + edits + a minted connector pair/mate ----
    Scene s1;
    auto& r1 = s1.getRegistry();
    krs::rbuild::RobotGraph g1 = krs::rbuild::buildDemoGraph();
    krs::rbuild::spawnGraphBodies(s1, g1, 0);
    g1.joints[0].name = "turntable"; g1.joints[0].limits.lower = -1.25; g1.joints[0].limits.upper = 0.75;
    g1.joints[0].prov = krs::rbuild::Prov::Manual;
    g1.joints[1].type = krs::rbuild::JType::Prismatic;
    krs::rbuild::EditController ctrl{ &g1 };
    ctrl.setJointAxis(1, glm::vec3(0.6f, 0, 0.8f));
    {   // connector pair + mate on bodies 2/3 (the demo bores), exactly as Define mints them
        auto* mg = &r1.ctx().emplace<MateGraphComponent>();
        const entt::entity eA = entt::entity(std::uint32_t(g1.bodies[2].entity));
        const entt::entity eB = entt::entity(std::uint32_t(g1.bodies[3].entity));
        auto& mcA = r1.get_or_emplace<MateConnectorComponent>(eA);
        auto& mcB = r1.get_or_emplace<MateConnectorComponent>(eB);
        BRepFace wa; wa.type = 1; wa.axisPos = { 0, 0.62f, 0.40f }; wa.axisDir = { 1, 0, 0 };
        wa.radius = 0.05f; wa.faceKey = computeFaceKey(wa);
        krs::rbuild::authorConcentricMate(*mg, eA, mcA, g1.bodies[2].placement, wa,
                                               eB, mcB, g1.bodies[3].placement, wa);
    }
    const bool saved = saveAuthoring(r1, g1, "demo.step", path);

    // ---- fresh scene (a "restart"): rebuild the same source, overlay ----
    Scene s2;
    auto& r2 = s2.getRegistry();
    krs::rbuild::RobotGraph g2 = krs::rbuild::buildDemoGraph();
    krs::rbuild::spawnGraphBodies(s2, g2, 0);
    const bool loaded = loadAuthoringOverlay(r2, g2, "demo.step", path);

    bool jointsOk = loaded && g2.joints.size() == g1.joints.size();
    if (jointsOk) for (size_t i = 0; i < g1.joints.size(); ++i) {
        const auto& a = g1.joints[i]; const auto& b = g2.joints[i];
        jointsOk = jointsOk && a.id == b.id && a.nodeId == b.nodeId && a.name == b.name
                && a.type == b.type && a.parent == b.parent && a.child == b.child && a.prov == b.prov
                && std::abs(a.limits.lower - b.limits.lower) < 1e-12
                && std::abs(a.limits.upper - b.limits.upper) < 1e-12
                && glm::length(a.axisDir - b.axisDir) < 1e-6f
                && glm::length(a.axisPos - b.axisPos) < 1e-6f;
    }
    const bool countersOk = loaded && g2.nextJointId >= g1.nextJointId && g2.nextNodeId >= g1.nextNodeId;

    // connectors landed on scene-2's OWN entities for bodies 2/3 (entity ids differ across scenes;
    // the (body, slot) ref is what carries them).
    const entt::entity e2A = entt::entity(std::uint32_t(g2.bodies[2].entity));
    const entt::entity e2B = entt::entity(std::uint32_t(g2.bodies[3].entity));
    const auto* c2A = r2.try_get<MateConnectorComponent>(e2A);
    const auto* c2B = r2.try_get<MateConnectorComponent>(e2B);
    const auto* mg2 = r2.ctx().find<MateGraphComponent>();
    const bool connOk = c2A && c2A->connectors.size() == 1 && c2B && c2B->connectors.size() == 1
                     && c2A->connectors[0].sourceFaceKey != 0;
    const bool mateOk = mg2 && mg2->mates.size() == 1
                     && mg2->mates[0].bodyA == e2A && mg2->mates[0].connA == c2A->connectors[0].id
                     && mg2->mates[0].bodyB == e2B && mg2->mates[0].connB == c2B->connectors[0].id;

    // ---- NEG-CTRLs: refuse cleanly, graph untouched ----
    Scene s3; auto& r3 = s3.getRegistry();
    krs::rbuild::RobotGraph g3 = krs::rbuild::buildDemoGraph();
    krs::rbuild::spawnGraphBodies(s3, g3, 0);
    const size_t before3 = g3.joints.size();
    const bool wrongSrc = !loadAuthoringOverlay(r3, g3, "other.step", path) && g3.joints.size() == before3
                       && g3.joints[0].name != "turntable";
    krs::rbuild::RobotGraph g4 = g3; g4.bodies.pop_back();
    const bool wrongCount = !loadAuthoringOverlay(r3, g4, "demo.step", path);
    bool corruptSafe = false;
    {   // truncate the document -> parse fails -> refused, no crash
        QFile f(QString::fromStdString(path));
        f.open(QIODevice::ReadOnly); const QByteArray all = f.readAll(); f.close();
        const std::string tpath = (QDir::temp().filePath("krs_persist_gate_trunc.json")).toStdString();
        QFile tf(QString::fromStdString(tpath));
        tf.open(QIODevice::WriteOnly | QIODevice::Truncate); tf.write(all.left(all.size() / 3)); tf.close();
        corruptSafe = !loadAuthoringOverlay(r3, g3, "demo.step", tpath) && g3.joints.size() == before3;
    }

    const bool pass = saved && loaded && jointsOk && countersOk && connOk && mateOk
                   && wrongSrc && wrongCount && corruptSafe;
    printf("[persist]   save=%s load=%s joints-round-trip=%s counters-advance=%s connectors=%s mate-rebound=%s\n",
           saved?"yes":"NO", loaded?"yes":"NO", jointsOk?"yes":"NO", countersOk?"yes":"NO",
           connOk?"yes":"NO", mateOk?"yes":"NO");
    printf("[persist]   NEG-CTRLs: wrong-source refused=%s wrong-body-count refused=%s corrupt refused=%s  %s\n",
           wrongSrc?"yes":"NO", wrongCount?"yes":"NO", corruptSafe?"yes":"NO",
           (wrongSrc && wrongCount && corruptSafe) ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[persist] %s\n", pass ? "ALL PASS (authoring survives restart: joints/limits/names/frames + connectors + mates round-trip; mismatches refused)"
                                  : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::persist
