// KSave.cpp -- see KSave.hpp. The .kscene/.krobot/.kjoint/.kstate family: composition by
// reference (relative path + id + content hash), deterministic geometry rebuild + kjoint overlay,
// best-effort session state. All JSON, all versioned, nothing silently rebinds.
#include "KSave.hpp"
#include "RobotBuilder.hpp"
#include "RobotBuilderScene.hpp"   // buildDemoGraph / spawnGraphBodies
#include "RobotModel.hpp"          // RobotRegistry / LiveRobot / instantiateFromGraph / transformRobot
#include "CadImporter.hpp"         // importStepAssembly (STEP-sourced robots)
#include "Scene.hpp"
#include "components.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QUuid>
#include <QSettings>

#include <cstdio>
#include <algorithm>

namespace krs::ksave {
namespace {

// ---- JSON idioms (match krs::persist) --------------------------------------------------------
QJsonArray vec3ToJson(const glm::vec3& v) { return QJsonArray{ double(v.x), double(v.y), double(v.z) }; }
glm::vec3 vec3FromJson(const QJsonValue& v, const glm::vec3& fb = glm::vec3(0)) {
    const QJsonArray a = v.toArray();
    if (a.size() != 3) return fb;
    return { float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()) };
}
QJsonArray mat4ToJson(const Eigen::Matrix4d& m) {
    QJsonArray a; for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) a.push_back(m(r, c)); return a;
}
Eigen::Matrix4d mat4FromJson(const QJsonValue& v) {
    Eigen::Matrix4d m = Eigen::Matrix4d::Identity();
    const QJsonArray a = v.toArray();
    if (a.size() == 16) for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) m(r, c) = a[r * 4 + c].toDouble();
    return m;
}
QString u64ToJson(std::uint64_t v) { return QString::number(qulonglong(v)); }
std::uint64_t u64FromJson(const QJsonValue& v) { return std::uint64_t(v.toString().toULongLong()); }

bool formatOk(const QJsonObject& o, const char* family) {   // "kjoint/1" -> family match + major 1
    const QString f = o["format"].toString();
    return f.startsWith(QLatin1String(family)) && f.section('/', 1, 1).toInt() == 1;
}
bool writeJsonFile(const QString& path, const QJsonObject& o) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}
bool readJsonFile(const QString& path, QJsonObject& out, QString* hashOut = nullptr) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = f.readAll();
    if (hashOut) *hashOut = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) return false;
    out = doc.object();
    return true;
}
QString fileSha1(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha1).toHex());
}
QString sanitize(const std::string& s) {
    QString q = QString::fromStdString(s);
    for (QChar& c : q) if (!c.isLetterOrNumber() && c != '-' && c != '_') c = '_';
    return q.isEmpty() ? QStringLiteral("robot") : q;
}

// (body, slot) <-> entity, same convention as krs::persist (slot 0 = primary, k>0 = extras[k-1]).
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

// ---- .kjoint ---------------------------------------------------------------------------------
QJsonObject jointToJson(const krs::rbuild::RBJoint& j) {
    QJsonObject o;
    o["format"] = QStringLiteral("kjoint/1");
    o["id"] = u64ToJson(j.id); o["nodeId"] = j.nodeId; o["name"] = QString::fromStdString(j.name);
    o["type"] = int(j.type); o["parent"] = j.parent; o["child"] = j.child;
    o["prov"] = int(j.prov); o["ambiguous"] = j.ambiguous; o["residual"] = j.residual;
    o["axisPos"] = vec3ToJson(j.axisPos); o["axisDir"] = vec3ToJson(j.axisDir); o["refDir"] = vec3ToJson(j.refDir);
    QJsonObject lim;
    lim["lower"] = j.limits.lower; lim["upper"] = j.limits.upper;
    lim["effort"] = j.limits.effort; lim["velocity"] = j.limits.velocity; lim["enabled"] = j.limits.enabled;
    o["limits"] = lim;
    return o;
}
bool jointFromJson(const QJsonObject& o, int bodyCount, krs::rbuild::RBJoint& j) {
    if (!formatOk(o, "kjoint")) return false;
    j.id = u64FromJson(o["id"]); j.nodeId = o["nodeId"].toInt(-1); j.name = o["name"].toString().toStdString();
    const int t = o["type"].toInt(0);
    j.type = (t == 1) ? krs::rbuild::JType::Prismatic : (t == 2) ? krs::rbuild::JType::Fixed
                                                                 : krs::rbuild::JType::Revolute;
    j.parent = o["parent"].toInt(-1); j.child = o["child"].toInt(-1);
    if (j.parent < 0 || j.parent >= bodyCount || j.child < 0 || j.child >= bodyCount || j.parent == j.child)
        return false;
    j.prov = (o["prov"].toInt(0) == 1) ? krs::rbuild::Prov::Manual : krs::rbuild::Prov::Inferred;
    j.ambiguous = o["ambiguous"].toBool(false);
    j.residual  = o["residual"].toDouble(0.0);
    j.axisPos = vec3FromJson(o["axisPos"]); j.axisDir = vec3FromJson(o["axisDir"], { 0, 0, 1 });
    j.refDir  = vec3FromJson(o["refDir"],  { 1, 0, 0 });
    const QJsonObject lim = o["limits"].toObject();
    j.limits.lower = lim["lower"].toDouble(-3.14159265); j.limits.upper = lim["upper"].toDouble(3.14159265);
    j.limits.effort = lim["effort"].toDouble(0.0); j.limits.velocity = lim["velocity"].toDouble(0.0);
    j.limits.enabled = lim["enabled"].toBool(true);
    return true;
}

// ---- graph transform (mirror of transformRobot's ctx-graph block) ------------------------------
void applyWorldTransformToGraph(krs::rbuild::RobotGraph& g, const Eigen::Matrix4d& T) {
    const Eigen::Matrix3d R = T.block<3, 3>(0, 0);
    auto xp = [&](const glm::vec3& p) {
        const Eigen::Vector4d r = T * Eigen::Vector4d(p.x, p.y, p.z, 1.0);
        return glm::vec3(float(r.x()), float(r.y()), float(r.z()));
    };
    auto xd = [&](const glm::vec3& d) {
        const Eigen::Vector3d r = R * Eigen::Vector3d(d.x, d.y, d.z);
        const double L = r.norm();
        return (L > 1e-12) ? glm::vec3(float(r.x() / L), float(r.y() / L), float(r.z() / L)) : d;
    };
    for (auto& b : g.bodies) b.placement = T * b.placement;
    for (auto& j : g.joints) {
        if (glm::length(j.axisPos) > 1e-9f) j.axisPos = xp(j.axisPos);   // (0,0,0) = unset sentinel
        j.axisDir = xd(j.axisDir);
        j.refDir  = xd(j.refDir);
    }
}

} // namespace

// ================================================================================================
// SAVE
// ================================================================================================
Report saveScene(Scene& scene, const std::string& kscenePath)
{
    Report rep;
    auto& reg = scene.getRegistry();
    auto* rr = reg.ctx().find<krs::robot::RobotRegistry>();
    if (!rr || rr->robots.empty()) { rep.error = QStringLiteral("No robots to save."); return rep; }
    auto* srcReg = reg.ctx().find<RobotSourceRegistry>();
    auto* store  = reg.ctx().find<krs::rbuild::AuthoringGraphStore>();
    auto* ctxG   = reg.ctx().find<krs::rbuild::RobotGraph>();

    const QFileInfo sceneInfo(QString::fromStdString(kscenePath));
    const QDir sceneDir = sceneInfo.absoluteDir();
    QDir().mkpath(sceneDir.absolutePath());

    QJsonArray instances;
    for (auto& rp : rr->robots) {
        if (!rp) continue;
        krs::robot::LiveRobot& lr = *rp;
        const RobotSource* src = srcReg ? srcReg->find(lr.robotId) : nullptr;
        const int builder = src ? src->builder : -1;
        const std::string step = src ? src->sourceStep : std::string();
        if (builder < 0 || (builder != 0 && step.empty())) {
            rep.warnings << QStringLiteral("Robot \"%1\" (id %2) has no rebuildable source (e.g. a split-off "
                                           "branch) -- skipped. Re-merge before saving.")
                                .arg(QString::fromStdString(lr.name)).arg(lr.robotId);
            continue;
        }

        // The authoring graph: active ctx if it is this robot's, else the parked one, else a mirror.
        krs::rbuild::RobotGraph g;
        if (ctxG && ctxG->robotId == lr.robotId) g = *ctxG;
        else if (store && store->byRobot.count(lr.robotId)) g = store->byRobot.at(lr.robotId);
        else {
            g = krs::robot::buildGraphFromLiveRobot(lr);
            rep.warnings << QStringLiteral("Robot \"%1\": saved from a live mirror (no authoring graph in "
                                           "session -- demo body sizes may reset to defaults).")
                                .arg(QString::fromStdString(lr.name));
        }

        // ---- .kjoint files: robots/<name>/<joint>.kjoint ----
        const QString rname = sanitize(lr.name);
        const QString jointDirRel = QStringLiteral("robots/%1").arg(rname);
        QJsonArray jointRefs;
        for (int ji = 0; ji < int(g.joints.size()); ++ji) {
            const auto& j = g.joints[ji];
            const QString jname = sanitize(j.name.empty() ? ("J" + std::to_string(ji)) : j.name);
            const QString rel = QStringLiteral("%1/%2.kjoint").arg(jointDirRel, jname);
            const QString abs = sceneDir.filePath(rel);
            if (!writeJsonFile(abs, jointToJson(j))) { rep.error = QStringLiteral("Cannot write %1").arg(abs); return rep; }
            QJsonObject ref;
            ref["ref"] = rel; ref["id"] = u64ToJson(j.id); ref["contentHash"] = fileSha1(abs);
            jointRefs.push_back(ref);
            ++rep.joints;
        }

        // ---- .krobot master dict ----
        QJsonObject ro;
        ro["format"] = QStringLiteral("krobot/1");
        ro["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        ro["name"] = QString::fromStdString(lr.name);
        ro["builder"] = builder;
        ro["sourceStep"] = QString::fromStdString(step);
        ro["bodyCount"] = int(g.bodies.size());
        ro["base"] = g.base;
        ro["nextJointId"] = u64ToJson(g.nextJointId);
        ro["nextNodeId"]  = g.nextNodeId;
        QJsonArray bodies;
        for (const auto& b : g.bodies) {
            QJsonObject bo; bo["name"] = QString::fromStdString(b.name); bo["visSize"] = vec3ToJson(b.visSize);
            bodies.push_back(bo);
        }
        ro["bodies"] = bodies;
        ro["joints"] = jointRefs;
        // Mate connectors, by (body, slot) ref -- the same durable addressing krs::persist proved.
        QJsonArray conns;
        for (auto e : reg.view<MateConnectorComponent>()) {
            int body = -1, slot = -1;
            if (!entityToRef(g, e, body, slot)) continue;
            const auto& mc = reg.get<MateConnectorComponent>(e);
            QJsonObject o; o["body"] = body; o["slot"] = slot; o["nextId"] = int(mc.nextConnectorId);
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
        ro["connectorSets"] = conns;

        const QString robotRel = QStringLiteral("robots/%1.krobot").arg(rname);
        const QString robotAbs = sceneDir.filePath(robotRel);
        if (!writeJsonFile(robotAbs, ro)) { rep.error = QStringLiteral("Cannot write %1").arg(robotAbs); return rep; }

        // ---- instance entry in the .kscene ----
        QJsonObject inst;
        inst["ref"] = robotRel;
        inst["id"] = ro["id"];
        inst["contentHash"] = fileSha1(robotAbs);
        inst["robotId"] = lr.robotId;
        inst["name"] = QString::fromStdString(lr.name);
        inst["ownsDrive"] = lr.ownsDrive;
        inst["basePlacement"] = mat4ToJson(lr.model.basePlacement);
        instances.push_back(inst);
        ++rep.robots;
    }

    QJsonObject sc;
    sc["format"] = QStringLiteral("kscene/1");
    sc["name"] = sceneInfo.completeBaseName();
    sc["robots"] = instances;
    if (!writeJsonFile(sceneInfo.absoluteFilePath(), sc)) {
        rep.error = QStringLiteral("Cannot write %1").arg(sceneInfo.absoluteFilePath());
        return rep;
    }

    saveSessionState(scene, kscenePath);            // the sidecar rides along with every save
    auto* info = reg.ctx().find<OpenSceneInfo>();
    if (!info) info = &reg.ctx().emplace<OpenSceneInfo>();
    info->kscenePath = kscenePath;
    QSettings().setValue(QStringLiteral("ksave/lastScene"), QString::fromStdString(kscenePath));
    rep.ok = true;
    return rep;
}

// ================================================================================================
// SESSION STATE (.kstate sidecar)
// ================================================================================================
static QString kstatePathFor(const std::string& kscenePath) {
    const QFileInfo fi(QString::fromStdString(kscenePath));
    return fi.absoluteDir().filePath(fi.completeBaseName() + QStringLiteral(".kstate"));
}

bool saveSessionState(Scene& scene, const std::string& kscenePath)
{
    auto& reg = scene.getRegistry();
    QJsonObject st;
    st["format"] = QStringLiteral("kstate/1");
    QJsonObject domains;

    // robots: per-robot joint pose q (deterministic, sim-agnostic -- NO PhysX/fluid buffers).
    QJsonObject robots;
    if (auto* rr = reg.ctx().find<krs::robot::RobotRegistry>()) {
        for (auto& rp : rr->robots) {
            if (!rp) continue;
            QJsonArray q;
            for (int i = 0; i < rp->ndof(); ++i) q.push_back(rp->q[i]);
            QJsonObject r; r["q"] = q;
            robots[QString::number(rp->robotId)] = r;
        }
    }
    domains["robots"] = robots;

    // camera: primary camera pose.
    const entt::entity camE = scene.getPrimaryCamera();
    if (camE != entt::null && reg.valid(camE) && reg.all_of<CameraComponent>(camE)) {
        const Camera& cam = reg.get<CameraComponent>(camE).camera;
        QJsonObject c;
        c["pos"] = vec3ToJson(cam.getPosition());
        c["target"] = vec3ToJson(cam.getFocalPoint());
        domains["camera"] = c;
    }
    st["domains"] = domains;
    return writeJsonFile(kstatePathFor(kscenePath), st);
}

static void applySessionState(Scene& scene, const std::string& kscenePath, Report& rep)
{
    QJsonObject st;
    if (!readJsonFile(kstatePathFor(kscenePath), st)) return;         // no sidecar -> authored state
    if (!formatOk(st, "kstate")) { rep.warnings << QStringLiteral(".kstate has an unknown format -- ignored."); return; }
    auto& reg = scene.getRegistry();
    const QJsonObject domains = st["domains"].toObject();

    const QJsonObject robots = domains["robots"].toObject();
    if (auto* rr = reg.ctx().find<krs::robot::RobotRegistry>()) {
        for (auto it = robots.begin(); it != robots.end(); ++it) {
            krs::robot::LiveRobot* lr = rr->get(it.key().toInt());
            if (!lr) { rep.warnings << QStringLiteral(".kstate: robot id %1 not in the scene -- pose skipped.").arg(it.key()); continue; }
            const QJsonArray q = it.value().toObject()["q"].toArray();
            if (int(q.size()) != lr->ndof()) {                        // stale (definition changed) -> best-effort skip
                rep.warnings << QStringLiteral(".kstate: robot \"%1\" pose has %2 DOF, robot has %3 -- pose skipped.")
                                    .arg(QString::fromStdString(lr->name)).arg(int(q.size())).arg(lr->ndof());
                continue;
            }
            Eigen::VectorXd qv(lr->ndof());
            for (int i = 0; i < lr->ndof(); ++i) qv[i] = q[i].toDouble();
            lr->setCommandedQ(qv);                                    // clamped by the (possibly new) limits
            krs::robot::writeBackRobotViz(scene, *lr);
        }
    }

    if (domains.contains("camera")) {
        const QJsonObject c = domains["camera"].toObject();
        const entt::entity camE = scene.getPrimaryCamera();
        if (camE != entt::null && reg.valid(camE) && reg.all_of<CameraComponent>(camE)) {
            Camera& cam = reg.get<CameraComponent>(camE).camera;
            const glm::vec3 pos = vec3FromJson(c["pos"]), tgt = vec3FromJson(c["target"]);
            cam.forceRecalculateView(pos, tgt, glm::length(pos - tgt));
        }
    }
}

// ================================================================================================
// LOAD
// ================================================================================================
Report loadScene(Scene& scene, const std::string& kscenePath)
{
    Report rep;
    auto& reg = scene.getRegistry();
    QJsonObject sc;
    if (!readJsonFile(QString::fromStdString(kscenePath), sc)) {
        rep.error = QStringLiteral("Cannot read/parse %1").arg(QString::fromStdString(kscenePath));
        return rep;
    }
    if (!formatOk(sc, "kscene")) { rep.error = QStringLiteral("Unknown .kscene format."); return rep; }
    const QDir sceneDir = QFileInfo(QString::fromStdString(kscenePath)).absoluteDir();

    // REPLACE semantics: the document owns the robots. Clear every robot + member entity first.
    {
        std::vector<entt::entity> kill;
        for (auto e : reg.view<RobotSubcomponentComponent>()) kill.push_back(e);
        for (auto e : reg.view<RobotRootComponent>()) kill.push_back(e);
        std::sort(kill.begin(), kill.end());
        kill.erase(std::unique(kill.begin(), kill.end()), kill.end());
        for (auto e : kill) if (reg.valid(e)) reg.destroy(e);
        if (auto* rr = reg.ctx().find<krs::robot::RobotRegistry>()) rr->robots.clear();
        if (auto* g = reg.ctx().find<krs::rbuild::RobotGraph>()) *g = krs::rbuild::RobotGraph{};
        if (auto* mg = reg.ctx().find<MateGraphComponent>()) { mg->mates.clear(); }
        if (auto* store = reg.ctx().find<krs::rbuild::AuthoringGraphStore>()) store->byRobot.clear();
    }
    auto* srcReg = reg.ctx().find<RobotSourceRegistry>();
    if (!srcReg) srcReg = &reg.ctx().emplace<RobotSourceRegistry>();
    srcReg->entries.clear();

    bool firstInstalled = false;
    for (const QJsonValue& iv : sc["robots"].toArray()) {
        const QJsonObject inst = iv.toObject();
        const QString rel = inst["ref"].toString();
        const QString abs = sceneDir.filePath(rel);
        QString robotHash;
        QJsonObject ro;
        if (!readJsonFile(abs, ro, &robotHash)) {
            rep.warnings << QStringLiteral("Missing/corrupt robot definition %1 -- instance skipped.").arg(rel);
            continue;
        }
        if (!formatOk(ro, "krobot")) { rep.warnings << QStringLiteral("%1: unknown .krobot format -- skipped.").arg(rel); continue; }
        if (robotHash != inst["contentHash"].toString())
            rep.warnings << QStringLiteral("%1 changed on disk since the scene was saved -- using the NEW definition.").arg(rel);

        const int robotId = inst["robotId"].toInt(-1);
        const int builder = ro["builder"].toInt(-1);
        const std::string step = ro["sourceStep"].toString().toStdString();
        const int bodyCount = ro["bodyCount"].toInt(0);

        // ---- deterministic geometry rebuild ----
        krs::rbuild::RobotGraph g;
        std::vector<int> spawnedEntities;                    // cleanup on a mid-robot refusal
        if (builder == 0) {
            g = krs::rbuild::buildDemoGraph();
            krs::rbuild::spawnGraphBodies(scene, g, robotId);
        } else if (builder == 1 || builder == 2) {
            std::vector<krs::rbuild::ParsedPart> parts = krs::cad::importStepAssembly(scene, step);
            for (const auto& p : parts) if (p.entity >= 0) spawnedEntities.push_back(p.entity);
            if (parts.empty()) {
                rep.warnings << QStringLiteral("%1: CAD source \"%2\" failed to import -- robot skipped.")
                                    .arg(rel, QString::fromStdString(step));
                continue;
            }
            g = (builder == 1) ? krs::rbuild::buildNamedSerialChain(parts)
                               : krs::rbuild::buildGraphFromParts(parts, std::max(0, ro["base"].toInt(0)));
        } else {
            rep.warnings << QStringLiteral("%1: unknown builder %2 -- skipped.").arg(rel).arg(builder);
            continue;
        }
        auto refuseRobot = [&](const QString& why) {
            rep.warnings << QStringLiteral("%1: %2 -- robot skipped.").arg(rel, why);
            for (int eid : spawnedEntities) {
                const entt::entity e = entt::entity(std::uint32_t(eid));
                if (reg.valid(e)) reg.destroy(e);
            }
        };
        if (int(g.bodies.size()) != bodyCount) {
            refuseRobot(QStringLiteral("rebuilt %1 bodies, definition expects %2 (source changed?)")
                            .arg(int(g.bodies.size())).arg(bodyCount));
            continue;
        }

        // ---- overlay the .kjoint files (definition truth; the WHOLE overlay or nothing) ----
        std::vector<krs::rbuild::RBJoint> joints;
        bool jointsOk = true;
        for (const QJsonValue& jv : ro["joints"].toArray()) {
            const QJsonObject jref = jv.toObject();
            const QString jrel = jref["ref"].toString();
            QString jhash;
            QJsonObject jo;
            if (!readJsonFile(sceneDir.filePath(jrel), jo, &jhash)) { jointsOk = false; break; }
            if (jhash != jref["contentHash"].toString())
                rep.warnings << QStringLiteral("%1 changed on disk -- using the NEW joint definition.").arg(jrel);
            krs::rbuild::RBJoint j;
            if (!jointFromJson(jo, bodyCount, j)) { jointsOk = false; break; }
            joints.push_back(std::move(j));
        }
        if (!jointsOk) { refuseRobot(QStringLiteral("a .kjoint file is missing or malformed")); continue; }
        g.joints = std::move(joints);
        g.base = std::clamp(ro["base"].toInt(0), 0, int(g.bodies.size()) - 1);
        g.robotId = robotId;
        g.nextJointId = std::max<std::uint64_t>(u64FromJson(ro["nextJointId"]), 1);
        g.nextNodeId  = std::max(ro["nextNodeId"].toInt(0), 0);
        for (const auto& j : g.joints) {
            g.nextJointId = std::max(g.nextJointId, j.id + 1);
            g.nextNodeId  = std::max(g.nextNodeId,  j.nodeId + 1);
        }

        // ---- connectors onto the freshly spawned entities ----
        for (const QJsonValue& sv : ro["connectorSets"].toArray()) {
            const QJsonObject o = sv.toObject();
            const entt::entity e = refToEntity(g, o["body"].toInt(-1), o["slot"].toInt(-1));
            if (e == entt::null || !reg.valid(e)) continue;
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

        // ---- go live ----
        krs::robot::LiveRobot* lr = krs::robot::instantiateFromGraph(scene, g, robotId);
        if (!lr) { refuseRobot(QStringLiteral("instantiation failed")); continue; }
        lr->name = lr->model.name = inst["name"].toString().toStdString();
        lr->useRobotFkViz = true;
        lr->ownsDrive = inst["ownsDrive"].toBool(false);
        if (reg.valid(lr->root)) {
            reg.emplace_or_replace<RobotRootComponent>(lr->root, RobotRootComponent{ lr->name, robotId });
            reg.emplace_or_replace<TagComponent>(lr->root, lr->name);
        }

        // Placement: the instance's saved base vs the freshly rebuilt base -> rigid delta.
        const Eigen::Matrix4d savedBase = mat4FromJson(inst["basePlacement"]);
        const Eigen::Matrix4d freshBase = lr->model.basePlacement;
        const Eigen::Matrix4d T = savedBase * freshBase.inverse();
        if ((T - Eigen::Matrix4d::Identity()).cwiseAbs().maxCoeff() > 1e-9) {
            krs::robot::transformRobot(scene, robotId, T);
            applyWorldTransformToGraph(g, T);
        }

        srcReg->set(robotId, step, builder);
        rep.joints += int(g.joints.size());
        // First loaded robot's graph becomes the ACTIVE authoring graph; the rest park.
        auto* store = reg.ctx().find<krs::rbuild::AuthoringGraphStore>();
        if (!store) store = &reg.ctx().emplace<krs::rbuild::AuthoringGraphStore>();
        if (!firstInstalled) {
            auto* gp = reg.ctx().find<krs::rbuild::RobotGraph>();
            if (!gp) gp = &reg.ctx().emplace<krs::rbuild::RobotGraph>();
            *gp = std::move(g);
            firstInstalled = true;
        } else {
            store->byRobot[robotId] = std::move(g);
        }
        ++rep.robots;
    }

    krs::robot::rebuildJointNameRegistry(reg);
    applySessionState(scene, kscenePath, rep);      // q + camera, best-effort (stale entries skip)

    auto* info = reg.ctx().find<OpenSceneInfo>();
    if (!info) info = &reg.ctx().emplace<OpenSceneInfo>();
    info->kscenePath = kscenePath;
    QSettings().setValue(QStringLiteral("ksave/lastScene"), QString::fromStdString(kscenePath));
    rep.ok = rep.robots > 0 || sc["robots"].toArray().isEmpty();
    if (!rep.ok && rep.error.isEmpty()) rep.error = QStringLiteral("No robot instance could be loaded.");
    return rep;
}

// ================================================================================================
// GATE KSAVE -- save -> fresh scene -> load round-trip + tamper detection + refusal NEG-CTRLs.
// ================================================================================================
bool runKSaveGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[ksave] GATE KSAVE -- .kscene/.krobot/.kjoint/.kstate round-trip; tamper detected+honored; refusals clean\n");
    const QString dir = QDir::temp().filePath("krs_ksave_gate");
    QDir(dir).removeRecursively();
    const std::string scenePath = QDir(dir).filePath("cell.kscene").toStdString();
    // QSettings hygiene: save/load record ksave/lastScene for the boot reopen; a GATE run must not
    // repoint the user's session at the gate's temp scene. Capture + restore on every exit path.
    const QVariant priorLastScene = QSettings().value(QStringLiteral("ksave/lastScene"));
    struct SettingsRestore {
        QVariant v;
        ~SettingsRestore() {
            if (v.isValid()) QSettings().setValue(QStringLiteral("ksave/lastScene"), v);
            else             QSettings().remove(QStringLiteral("ksave/lastScene"));
        }
    } restoreGuard{ priorLastScene };

    // ---- author a session: demo robot + edits + pose ----
    Scene s1;
    auto& r1 = s1.getRegistry();
    r1.ctx().emplace<RobotSourceRegistry>().set(0, "", /*builder demo*/ 0);
    krs::rbuild::RobotGraph g1 = krs::rbuild::buildDemoGraph();
    g1.robotId = 0;
    krs::rbuild::spawnGraphBodies(s1, g1, 0);
    g1.joints[0].name = "turntable"; g1.joints[0].limits.lower = -1.25; g1.joints[0].limits.upper = 0.75;
    g1.joints[1].type = krs::rbuild::JType::Prismatic;
    r1.ctx().emplace<krs::rbuild::RobotGraph>(g1);
    krs::robot::LiveRobot* lr1 = krs::robot::instantiateFromGraph(s1, g1, 0);
    if (!lr1) { printf("[ksave] FAIL: no live robot\n"); return false; }
    lr1->name = lr1->model.name = "GateBot"; lr1->useRobotFkViz = true;
    {   // a connector on body 2 (persists through the .krobot)
        auto& mc = r1.get_or_emplace<MateConnectorComponent>(entt::entity(std::uint32_t(g1.bodies[2].entity)));
        BRepFace f; f.type = 1; f.axisPos = { 0, 0.62f, 0.40f }; f.axisDir = { 1, 0, 0 }; f.radius = 0.05f;
        f.faceKey = computeFaceKey(f);
        mc.connectors.push_back(krs::rbuild::makeConnectorLocal(f, g1.bodies[2].placement, mc.nextConnectorId++, "gate"));
    }
    Eigen::VectorXd q0(lr1->ndof()); for (int i = 0; i < lr1->ndof(); ++i) q0[i] = 0.15 * (i + 1);
    lr1->setCommandedQ(q0);
    const Eigen::VectorXd qSaved = lr1->q;

    const Report sr = saveScene(s1, scenePath);
    const bool savedOk = sr.ok && sr.robots == 1 && sr.joints == int(g1.joints.size())
        && QFile::exists(QString::fromStdString(scenePath))
        && QFile::exists(QDir(dir).filePath("robots/GateBot.krobot"))
        && QFile::exists(QDir(dir).filePath("robots/GateBot/turntable.kjoint"))
        && QFile::exists(QDir(dir).filePath("cell.kstate"));
    printf("[ksave]   save: ok=%s robots=%d joints=%d files(kscene/krobot/kjoint/kstate) present=%s\n",
           sr.ok ? "yes" : "NO", sr.robots, sr.joints, savedOk ? "yes" : "NO");

    // ---- fresh scene ("restart"): load ----
    bool loadOk = false, stateOk = false, connOk = false;
    {
        Scene s2;
        const Report lrp = loadScene(s2, scenePath);
        auto& r2 = s2.getRegistry();
        auto* rr2 = r2.ctx().find<krs::robot::RobotRegistry>();
        krs::robot::LiveRobot* lb = rr2 ? rr2->get(0) : nullptr;
        const auto* g2 = r2.ctx().find<krs::rbuild::RobotGraph>();
        loadOk = lrp.ok && lb && g2 && g2->robotId == 0
              && g2->joints.size() == g1.joints.size()
              && g2->joints[0].name == "turntable"
              && std::abs(g2->joints[0].limits.lower + 1.25) < 1e-9
              && g2->joints[1].type == krs::rbuild::JType::Prismatic
              && lb->name == "GateBot" && g2->dof() == g1.dof();
        stateOk = lb && lb->ndof() == int(qSaved.size())
               && (lb->q - qSaved).cwiseAbs().maxCoeff() < 1e-9;
        if (g2 && lb) {
            const entt::entity e2 = entt::entity(std::uint32_t(g2->bodies[2].entity));
            const auto* mc2 = r2.try_get<MateConnectorComponent>(e2);
            connOk = mc2 && mc2->connectors.size() == 1 && mc2->connectors[0].sourceFaceKey != 0;
        }
        printf("[ksave]   load: ok=%s joints+edits round-trip=%s q restored=%s connector restored=%s\n",
               lrp.ok ? "yes" : "NO", loadOk ? "yes" : "NO", stateOk ? "yes" : "NO", connOk ? "yes" : "NO");
    }

    // ---- TAMPER: edit a .kjoint on disk -> reload detects (warning) AND honors the new value ----
    bool tamperOk = false;
    {
        const QString jpath = QDir(dir).filePath("robots/GateBot/turntable.kjoint");
        QJsonObject jo; readJsonFile(jpath, jo);
        QJsonObject lim = jo["limits"].toObject(); lim["upper"] = 2.5; jo["limits"] = lim;
        writeJsonFile(jpath, jo);
        Scene s3;
        const Report lrp = loadScene(s3, scenePath);
        const auto* g3 = s3.getRegistry().ctx().find<krs::rbuild::RobotGraph>();
        bool warned = false;
        for (const QString& w : lrp.warnings) if (w.contains(QStringLiteral("changed on disk"))) warned = true;
        tamperOk = lrp.ok && g3 && std::abs(g3->joints[0].limits.upper - 2.5) < 1e-9 && warned;
        printf("[ksave]   tamper: swapped .kjoint detected(warning)=%s new value live=%s  %s\n",
               warned ? "yes" : "NO", (g3 && std::abs(g3->joints[0].limits.upper - 2.5) < 1e-9) ? "yes" : "NO",
               tamperOk ? "PASS" : "FAIL");
    }

    // ---- STALE STATE: wrong-length q is skipped, load still succeeds ----
    bool staleOk = false;
    {
        QJsonObject st; readJsonFile(QDir(dir).filePath("cell.kstate"), st);
        QJsonObject domains = st["domains"].toObject();
        QJsonObject robots = domains["robots"].toObject();
        QJsonObject r0 = robots["0"].toObject();
        r0["q"] = QJsonArray{ 9.9 };                                   // wrong DOF count
        robots["0"] = r0; domains["robots"] = robots; st["domains"] = domains;
        writeJsonFile(QDir(dir).filePath("cell.kstate"), st);
        Scene s4;
        const Report lrp = loadScene(s4, scenePath);
        auto* rr4 = s4.getRegistry().ctx().find<krs::robot::RobotRegistry>();
        krs::robot::LiveRobot* lb = rr4 ? rr4->get(0) : nullptr;
        bool warned = false;
        for (const QString& w : lrp.warnings) if (w.contains(QStringLiteral("pose skipped"))) warned = true;
        staleOk = lrp.ok && lb && lb->q.cwiseAbs().maxCoeff() < 1e-12 && warned;   // authored home, not 9.9
        printf("[ksave]   stale .kstate: wrong-DOF pose skipped(warning)=%s robot at home=%s  %s\n",
               warned ? "yes" : "NO", (lb && lb->q.cwiseAbs().maxCoeff() < 1e-12) ? "yes" : "NO",
               staleOk ? "PASS" : "FAIL");
    }

    // ---- NEG-CTRLs: missing .krobot / corrupt .kscene refuse cleanly ----
    bool negMissing = false, negCorrupt = false;
    {
        QFile::remove(QDir(dir).filePath("robots/GateBot.krobot"));
        Scene s5;
        const Report lrp = loadScene(s5, scenePath);
        auto* rr5 = s5.getRegistry().ctx().find<krs::robot::RobotRegistry>();
        negMissing = !lrp.ok && (!rr5 || rr5->robots.empty());
        QFile f(QString::fromStdString(scenePath));
        f.open(QIODevice::WriteOnly | QIODevice::Truncate); f.write("{ not json", 10); f.close();
        Scene s6;
        const Report lrp2 = loadScene(s6, scenePath);
        negCorrupt = !lrp2.ok;
        printf("[ksave]   NEG-CTRLs: missing .krobot refused=%s corrupt .kscene refused=%s  %s\n",
               negMissing ? "yes" : "NO", negCorrupt ? "yes" : "NO",
               (negMissing && negCorrupt) ? "REJECTS(non-vacuous)" : "VACUOUS!");
    }

    const bool pass = savedOk && loadOk && stateOk && connOk && tamperOk && staleOk && negMissing && negCorrupt;
    printf("[ksave] %s\n", pass ? "ALL PASS (nested kscene/krobot/kjoint round-trip; kstate best-effort; tamper detected+honored; refusals clean)"
                                : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::ksave
