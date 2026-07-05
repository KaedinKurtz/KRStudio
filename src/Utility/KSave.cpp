// KSave.cpp -- see KSave.hpp. The .kscene/.krobot/.kjoint/.kstate family: composition by
// reference (relative path + id + content hash), deterministic geometry rebuild + kjoint overlay,
// best-effort session state. All JSON, all versioned, nothing silently rebinds.
#include "KSave.hpp"
#include "RobotBuilder.hpp"
#include "RobotBuilderScene.hpp"   // buildDemoGraph / spawnGraphBodies
#include "RobotModel.hpp"          // RobotRegistry / LiveRobot / instantiateFromGraph / transformRobot
#include "CadImporter.hpp"         // importStepAssembly (STEP-sourced robots)
#include "SceneBuilder.hpp"        // spawnPrimitive / spawnLightEmitter (loose-object + light rebuild)
#include "WorldState.hpp"          // krs::world knowledge (learned params/traits/tags persist per object)
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
    if (!j.actuatorRef.empty()) o["actuator"] = QString::fromStdString(j.actuatorRef);  // drive-train provenance
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
    j.actuatorRef = o["actuator"].toString().toStdString();
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

// ---- non-robot scene content (save architecture v1.1: environment + lights + loose objects) ----
QJsonArray vec2ToJson(const glm::vec2& v) { return QJsonArray{ double(v.x), double(v.y) }; }
glm::vec2 vec2FromJson(const QJsonValue& v, const glm::vec2& fb = glm::vec2(0)) {
    const QJsonArray a = v.toArray();
    if (a.size() != 2) return fb;
    return { float(a[0].toDouble()), float(a[1].toDouble()) };
}
QJsonArray quatToJson(const glm::quat& q) { return QJsonArray{ double(q.w), double(q.x), double(q.y), double(q.z) }; }
glm::quat quatFromJson(const QJsonValue& v, const glm::quat& fb = glm::quat(1, 0, 0, 0)) {
    const QJsonArray a = v.toArray();
    if (a.size() != 4) return fb;
    return glm::quat(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()), float(a[3].toDouble()));
}

// TransformComponent <-> JSON.
QJsonObject transformToJson(const TransformComponent& t) {
    QJsonObject o; o["t"] = vec3ToJson(t.translation); o["r"] = quatToJson(t.rotation); o["s"] = vec3ToJson(t.scale);
    return o;
}
void transformFromJson(const QJsonObject& o, TransformComponent& t) {
    t.translation = vec3FromJson(o["t"]); t.rotation = quatFromJson(o["r"]); t.scale = vec3FromJson(o["s"], glm::vec3(1));
}

// MaterialComponent <-> JSON. First pass: all scalar/vector appearance + engineering fields.
// Texture *maps* (shared_ptr<Texture2D/Cubemap>) are NOT persisted here (no asset path on the
// component yet) -- a follow-up will add texture-asset refs; loaded objects keep their maps null.
QJsonObject materialToJson(const MaterialComponent& m) {
    QJsonObject o;
    o["albedo"] = vec3ToJson(m.albedoColor); o["albedoTiling"] = vec2ToJson(m.albedoTiling);
    o["albedoOffset"] = vec2ToJson(m.albedoOffset); o["albedoBrightness"] = m.albedoBrightness;
    o["opacity"] = m.opacity; o["metallic"] = m.metallic; o["roughness"] = m.roughness;
    o["specular"] = vec3ToJson(m.specularColor); o["glossiness"] = m.glossiness; o["ao"] = m.ao;
    o["clearcoat"] = m.clearcoat; o["clearcoatRoughness"] = m.clearcoatRoughness;
    o["sheen"] = m.sheen; o["sheenColor"] = vec3ToJson(m.sheenColor); o["sheenRoughness"] = m.sheenRoughness;
    o["transmission"] = m.transmission; o["ior"] = m.ior; o["thickness"] = m.thickness;
    o["attenuationColor"] = vec3ToJson(m.attenuationColor); o["attenuationDistance"] = double(m.attenuationDistance);
    o["sssEnabled"] = m.sssEnabled; o["sssColor"] = vec3ToJson(m.sssColor); o["sssRadius"] = vec3ToJson(m.sssRadius);
    o["anisotropy"] = m.anisotropy; o["anisotropyRotation"] = m.anisotropyRotation;
    o["emissive"] = vec3ToJson(m.emissiveColor); o["emissiveStrength"] = m.emissiveStrength;
    // engineering / physical
    o["physicalName"] = QString::fromStdString(m.physicalName);
    o["density"] = double(m.density); o["bulkModulus"] = double(m.bulkModulus); o["shearModulus"] = double(m.shearModulus);
    o["youngsModulus"] = double(m.youngsModulus); o["poissonRatio"] = double(m.poissonRatio);
    o["volume_m3"] = double(m.volume_m3); o["massKg"] = double(m.massKg);
    o["specificHeat"] = double(m.specificHeat); o["thermalConductivity"] = double(m.thermalConductivity);
    return o;
}
void materialFromJson(const QJsonObject& o, MaterialComponent& m) {
    m.albedoColor = vec3FromJson(o["albedo"], m.albedoColor);
    m.albedoTiling = vec2FromJson(o["albedoTiling"], m.albedoTiling);
    m.albedoOffset = vec2FromJson(o["albedoOffset"], m.albedoOffset);
    m.albedoBrightness = float(o["albedoBrightness"].toDouble(m.albedoBrightness));
    m.opacity = float(o["opacity"].toDouble(m.opacity));
    m.metallic = float(o["metallic"].toDouble(m.metallic));
    m.roughness = float(o["roughness"].toDouble(m.roughness));
    m.specularColor = vec3FromJson(o["specular"], m.specularColor);
    m.glossiness = float(o["glossiness"].toDouble(m.glossiness));
    m.ao = float(o["ao"].toDouble(m.ao));
    m.clearcoat = float(o["clearcoat"].toDouble(m.clearcoat));
    m.clearcoatRoughness = float(o["clearcoatRoughness"].toDouble(m.clearcoatRoughness));
    m.sheen = float(o["sheen"].toDouble(m.sheen));
    m.sheenColor = vec3FromJson(o["sheenColor"], m.sheenColor);
    m.sheenRoughness = float(o["sheenRoughness"].toDouble(m.sheenRoughness));
    m.transmission = float(o["transmission"].toDouble(m.transmission));
    m.ior = float(o["ior"].toDouble(m.ior));
    m.thickness = float(o["thickness"].toDouble(m.thickness));
    m.attenuationColor = vec3FromJson(o["attenuationColor"], m.attenuationColor);
    m.attenuationDistance = float(o["attenuationDistance"].toDouble(m.attenuationDistance));
    m.sssEnabled = o["sssEnabled"].toBool(m.sssEnabled);
    m.sssColor = vec3FromJson(o["sssColor"], m.sssColor);
    m.sssRadius = vec3FromJson(o["sssRadius"], m.sssRadius);
    m.anisotropy = float(o["anisotropy"].toDouble(m.anisotropy));
    m.anisotropyRotation = float(o["anisotropyRotation"].toDouble(m.anisotropyRotation));
    m.emissiveColor = vec3FromJson(o["emissive"], m.emissiveColor);
    m.emissiveStrength = float(o["emissiveStrength"].toDouble(m.emissiveStrength));
    m.physicalName = o["physicalName"].toString(QString::fromStdString(m.physicalName)).toStdString();
    m.density = float(o["density"].toDouble(m.density));
    m.bulkModulus = float(o["bulkModulus"].toDouble(m.bulkModulus));
    m.shearModulus = float(o["shearModulus"].toDouble(m.shearModulus));
    m.youngsModulus = float(o["youngsModulus"].toDouble(m.youngsModulus));
    m.poissonRatio = float(o["poissonRatio"].toDouble(m.poissonRatio));
    m.volume_m3 = float(o["volume_m3"].toDouble(m.volume_m3));
    m.massKg = float(o["massKg"].toDouble(m.massKg));
    m.specificHeat = float(o["specificHeat"].toDouble(m.specificHeat));
    m.thermalConductivity = float(o["thermalConductivity"].toDouble(m.thermalConductivity));
}

// LightComponent <-> JSON.
QJsonObject lightToJson(const LightComponent& l) {
    QJsonObject o;
    o["type"] = int(l.type); o["color"] = vec3ToJson(l.color); o["intensity"] = l.intensity; o["range"] = l.range;
    o["innerConeDeg"] = l.innerConeDeg; o["outerConeDeg"] = l.outerConeDeg;
    o["size"] = vec2ToJson(l.size); o["twoSided"] = l.twoSided; o["enabled"] = l.enabled;
    return o;
}
void lightFromJson(const QJsonObject& o, LightComponent& l) {
    l.type = LightComponent::Type(o["type"].toInt(int(l.type)));
    l.color = vec3FromJson(o["color"], l.color);
    l.intensity = float(o["intensity"].toDouble(l.intensity));
    l.range = float(o["range"].toDouble(l.range));
    l.innerConeDeg = float(o["innerConeDeg"].toDouble(l.innerConeDeg));
    l.outerConeDeg = float(o["outerConeDeg"].toDouble(l.outerConeDeg));
    l.size = vec2FromJson(o["size"], l.size);
    l.twoSided = o["twoSided"].toBool(l.twoSided);
    l.enabled = o["enabled"].toBool(l.enabled);
}

// EnvironmentSettings ctx <-> JSON (the renderer's persisted lighting/skybox knobs).
QJsonObject environmentToJson(const EnvironmentSettings& e) {
    QJsonObject o;
    o["iblIntensity"] = e.iblIntensity; o["drawSkybox"] = e.drawSkybox; o["roomColor"] = vec3ToJson(e.roomColor);
    o["sunIntensity"] = e.sunIntensity; o["sunColor"] = vec3ToJson(e.sunColor); o["sunDirection"] = vec3ToJson(e.sunDirection);
    o["exposureEV"] = e.exposureEV; o["tonemapExposure"] = e.tonemapExposure; o["hdrEnabled"] = e.hdrEnabled;
    o["hdrPath"] = QString::fromStdString(e.hdrPath);
    return o;
}
void environmentFromJson(const QJsonObject& o, EnvironmentSettings& e) {
    e.iblIntensity = float(o["iblIntensity"].toDouble(e.iblIntensity));
    e.drawSkybox = o["drawSkybox"].toBool(e.drawSkybox);
    e.roomColor = vec3FromJson(o["roomColor"], e.roomColor);
    e.sunIntensity = float(o["sunIntensity"].toDouble(e.sunIntensity));
    e.sunColor = vec3FromJson(o["sunColor"], e.sunColor);
    e.sunDirection = vec3FromJson(o["sunDirection"], e.sunDirection);
    e.exposureEV = float(o["exposureEV"].toDouble(e.exposureEV));
    e.tonemapExposure = float(o["tonemapExposure"].toDouble(e.tonemapExposure));
    e.hdrEnabled = o["hdrEnabled"].toBool(e.hdrEnabled);
    e.hdrPath = o["hdrPath"].toString().toStdString();
}

// SceneProperties (fog + background) <-> JSON. Lives in the ECS ctx.
QJsonObject scenePropsToJson(const SceneProperties& p) {
    QJsonObject o;
    o["fogEnabled"] = p.fogEnabled;
    o["backgroundColor"] = QJsonArray{ double(p.backgroundColor.r), double(p.backgroundColor.g),
                                       double(p.backgroundColor.b), double(p.backgroundColor.a) };
    o["fogColor"] = vec3ToJson(p.fogColor); o["fogStart"] = p.fogStartDistance; o["fogEnd"] = p.fogEndDistance;
    o["showCollisionShapes"] = p.showCollisionShapes;
    return o;
}
void scenePropsFromJson(const QJsonObject& o, SceneProperties& p) {
    p.fogEnabled = o["fogEnabled"].toBool(p.fogEnabled);
    const QJsonArray bg = o["backgroundColor"].toArray();
    if (bg.size() == 4) p.backgroundColor = { float(bg[0].toDouble()), float(bg[1].toDouble()),
                                              float(bg[2].toDouble()), float(bg[3].toDouble()) };
    p.fogColor = vec3FromJson(o["fogColor"], p.fogColor);
    p.fogStartDistance = float(o["fogStart"].toDouble(p.fogStartDistance));
    p.fogEndDistance = float(o["fogEnd"].toDouble(p.fogEndDistance));
    p.showCollisionShapes = o["showCollisionShapes"].toBool(p.showCollisionShapes);
}

// True for a robot member/root or a light (those are saved via their own sections, not as loose objects).
bool isRobotOrLight(entt::registry& reg, entt::entity e) {
    return reg.all_of<RobotSubcomponentComponent>(e) || reg.all_of<RobotRootComponent>(e)
        || reg.all_of<LightEmitterTag>(e) || reg.all_of<LightComponent>(e);
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
    // v1.1: a scene may hold ONLY non-robot content (lights/objects/environment), so an empty
    // robot registry is no longer fatal -- we save whatever is present.
    auto* srcReg = reg.ctx().find<RobotSourceRegistry>();
    auto* store  = reg.ctx().find<krs::rbuild::AuthoringGraphStore>();
    auto* ctxG   = reg.ctx().find<krs::rbuild::RobotGraph>();

    const QFileInfo sceneInfo(QString::fromStdString(kscenePath));
    const QDir sceneDir = sceneInfo.absoluteDir();
    QDir().mkpath(sceneDir.absolutePath());

    QJsonArray instances;
    for (auto& rp : (rr ? rr->robots : std::vector<std::shared_ptr<krs::robot::LiveRobot>>{})) {
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

    // ---- v1.1: environment (renderer knobs mirrored into ctx) + fog/background ----
    if (auto* env = reg.ctx().find<EnvironmentSettings>()) sc["environment"] = environmentToJson(*env);
    if (auto* props = reg.ctx().find<SceneProperties>())  sc["sceneProps"]  = scenePropsToJson(*props);

    // ---- v1.2: KNOWLEDGE -- what the robot has LEARNED about each object (params + traits + tag).
    //      Keyed by object NAME; reloading the same scene means the glass honed yesterday needs no
    //      re-derivation (the whole point of the R2S ledger). ----
    if (auto* wsp = reg.ctx().find<krs::world::WorldState>()) {
        QJsonObject knowledge;
        for (const auto& [objName, k] : wsp->allKnowledge()) {
            QJsonObject ko;
            ko["tag"] = (k.tag == krs::world::LearnTag::R2S) ? QStringLiteral("r2s") : QStringLiteral("simonly");
            QJsonArray traits; for (const auto& t : k.traits) traits.push_back(QString::fromStdString(t));
            ko["traits"] = traits;
            QJsonObject params;
            for (const auto& [pn, p] : k.params) {
                QJsonObject po; po["v"] = p.value; po["sigma"] = p.sigma; po["prov"] = int(p.prov);
                params[QString::fromStdString(pn)] = po;
            }
            ko["params"] = params;
            knowledge[QString::fromStdString(objName)] = ko;
        }
        if (!knowledge.isEmpty()) sc["knowledge"] = knowledge;
    }

    // ---- v1.1: lights (each: transform + full LightComponent + emissive material) ----
    QJsonArray lights;
    for (auto e : reg.view<LightComponent>()) {
        if (reg.all_of<RobotSubcomponentComponent>(e) || reg.all_of<RobotRootComponent>(e)) continue; // robot-mounted lights ride with the robot
        QJsonObject lo;
        lo["light"] = lightToJson(reg.get<LightComponent>(e));
        if (reg.all_of<TransformComponent>(e)) lo["transform"] = transformToJson(reg.get<TransformComponent>(e));
        if (reg.all_of<TagComponent>(e))       lo["name"]      = QString::fromStdString(reg.get<TagComponent>(e).tag);
        if (reg.all_of<MaterialComponent>(e))  lo["material"]  = materialToJson(reg.get<MaterialComponent>(e));
        lights.push_back(lo);
        ++rep.lights;
    }
    sc["lights"] = lights;

    // ---- v1.1: loose objects (primitives + mesh instances that carry a reconstruction recipe) ----
    QJsonArray objects;
    for (auto e : reg.view<SceneObjectComponent, TransformComponent>()) {
        if (isRobotOrLight(reg, e)) continue;
        const auto& so = reg.get<SceneObjectComponent>(e);
        QJsonObject oo;
        oo["primitive"] = so.primitive;
        // Prefer the recipe's mesh path; fall back to the renderable's sourcePath for mesh assets.
        std::string mesh = so.meshPath;
        if (mesh.empty() && reg.all_of<RenderableMeshComponent>(e)) mesh = reg.get<RenderableMeshComponent>(e).sourcePath;
        oo["mesh"] = QString::fromStdString(mesh);
        oo["transform"] = transformToJson(reg.get<TransformComponent>(e));
        if (reg.all_of<TagComponent>(e))      oo["name"]     = QString::fromStdString(reg.get<TagComponent>(e).tag);
        if (reg.all_of<MaterialComponent>(e)) oo["material"] = materialToJson(reg.get<MaterialComponent>(e));
        objects.push_back(oo);
        ++rep.objects;
    }
    sc["objects"] = objects;

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

    // REPLACE semantics: the document owns the robots AND the loose non-robot content (lights +
    // recipe-tagged objects). Clear all of them first. (Procedural/gizmo entities with no
    // SceneObjectComponent and no LightComponent are left untouched, as in v1.)
    {
        std::vector<entt::entity> kill;
        for (auto e : reg.view<RobotSubcomponentComponent>()) kill.push_back(e);
        for (auto e : reg.view<RobotRootComponent>()) kill.push_back(e);
        for (auto e : reg.view<LightComponent>()) kill.push_back(e);
        for (auto e : reg.view<SceneObjectComponent>()) kill.push_back(e);
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

    // ---- v1.2: knowledge ledger (learned params/traits/tags per object name) ----
    if (sc.contains("knowledge")) {
        auto& wsr = krs::world::worldState(reg);
        const QJsonObject knowledge = sc["knowledge"].toObject();
        for (auto it = knowledge.begin(); it != knowledge.end(); ++it) {
            const QJsonObject ko = it.value().toObject();
            krs::world::ObjectKnowledge k;
            k.tag = (ko["tag"].toString() == QLatin1String("r2s")) ? krs::world::LearnTag::R2S
                                                                   : krs::world::LearnTag::SimOnly;
            for (const QJsonValue& tv : ko["traits"].toArray()) k.traits.push_back(tv.toString().toStdString());
            const QJsonObject params = ko["params"].toObject();
            for (auto pit = params.begin(); pit != params.end(); ++pit) {
                const QJsonObject po = pit.value().toObject();
                krs::world::ObjParam p;
                p.value = po["v"].toDouble(); p.sigma = po["sigma"].toDouble(1e9);
                p.prov = krs::world::Prov(po["prov"].toInt(0));
                k.params[pit.key().toStdString()] = p;
            }
            wsr.setKnowledge(it.key().toStdString(), k);
        }
    }

    // ---- v1.1: environment + fog/background (ctx singletons) ----
    if (sc.contains("environment")) {
        auto* env = reg.ctx().find<EnvironmentSettings>();
        if (!env) env = &reg.ctx().emplace<EnvironmentSettings>();
        environmentFromJson(sc["environment"].toObject(), *env);
    }
    if (sc.contains("sceneProps")) {
        auto* props = reg.ctx().find<SceneProperties>();
        if (!props) props = &reg.ctx().emplace<SceneProperties>();
        scenePropsFromJson(sc["sceneProps"].toObject(), *props);
    }

    // ---- v1.1: lights (rebuild the emitter body, then overlay the saved light/transform/material) ----
    for (const QJsonValue& lv : sc["lights"].toArray()) {
        const QJsonObject lo = lv.toObject();
        LightComponent lc; lightFromJson(lo["light"].toObject(), lc);
        const std::string name = lo["name"].toString().toStdString();
        TransformComponent xf; transformFromJson(lo["transform"].toObject(), xf);
        entt::entity e = SceneBuilder::spawnLightEmitter(scene, lc.type, xf.translation, lc.color, lc.intensity, name);
        if (e == entt::null || !reg.valid(e)) { rep.warnings << QStringLiteral("A light failed to spawn -- skipped."); continue; }
        reg.emplace_or_replace<LightComponent>(e, lc);               // full params (cone/size/range/enabled)
        reg.emplace_or_replace<TransformComponent>(e, xf);           // exact saved orientation + scale
        if (lo.contains("material") && reg.all_of<MaterialComponent>(e))
            materialFromJson(lo["material"].toObject(), reg.get<MaterialComponent>(e));
        ++rep.lights;
    }

    // ---- v1.1: loose objects (primitive recipe rebuild; mesh-asset objects deferred to v1.2) ----
    for (const QJsonValue& ov : sc["objects"].toArray()) {
        const QJsonObject oo = ov.toObject();
        const int prim = oo["primitive"].toInt(-1);
        const std::string mesh = oo["mesh"].toString().toStdString();
        const std::string name = oo["name"].toString().toStdString();
        TransformComponent xf; transformFromJson(oo["transform"].toObject(), xf);
        entt::entity e = entt::null;
        if (prim >= 0) {
            e = SceneBuilder::spawnPrimitive(scene, prim, xf.translation, xf.scale, name);
        } else {
            rep.warnings << QStringLiteral("Object \"%1\" is a mesh asset (%2) -- mesh-asset reload is a v1.2 "
                                           "follow-up, skipped for now.")
                                .arg(QString::fromStdString(name), QString::fromStdString(mesh));
            continue;
        }
        if (e == entt::null || !reg.valid(e)) { rep.warnings << QStringLiteral("An object failed to spawn -- skipped."); continue; }
        reg.emplace_or_replace<TransformComponent>(e, xf);           // restore rotation (spawnPrimitive takes only pos+scale)
        if (oo.contains("material")) {
            auto& m = reg.get_or_emplace<MaterialComponent>(e);
            materialFromJson(oo["material"].toObject(), m);
        }
        ++rep.objects;
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
// ACTUATOR CHAIN: .kactuator -> .kmotor -> derived joint-side effort/velocity
// ================================================================================================
ActuatorSpec resolveActuator(const std::string& kactuatorPath)
{
    ActuatorSpec sp;
    const QString apath = QString::fromStdString(kactuatorPath);
    QJsonObject ao;
    if (!readJsonFile(apath, ao)) { sp.error = QStringLiteral("Cannot read .kactuator %1").arg(apath); return sp; }
    if (!formatOk(ao, "kactuator")) { sp.error = QStringLiteral("Unknown .kactuator format."); return sp; }
    sp.actuatorName = ao["name"].toString().toStdString();
    sp.ratio = ao["gearRatio"].toDouble(1.0);
    sp.efficiency = ao["efficiency"].toDouble(1.0);
    if (sp.ratio <= 0.0) { sp.error = QStringLiteral("gearRatio must be > 0."); return sp; }

    const QString mrel = ao["motor"].toObject()["ref"].toString();
    if (mrel.isEmpty()) { sp.error = QStringLiteral(".kactuator has no motor ref."); return sp; }
    const QString mpath = QFileInfo(apath).absoluteDir().filePath(mrel);
    QString mhash;
    QJsonObject mo;
    if (!readJsonFile(mpath, mo, &mhash)) { sp.error = QStringLiteral("Cannot read .kmotor %1").arg(mpath); return sp; }
    if (!formatOk(mo, "kmotor")) { sp.error = QStringLiteral("Unknown .kmotor format."); return sp; }
    const QString wantHash = ao["motor"].toObject()["contentHash"].toString();
    if (!wantHash.isEmpty() && wantHash != mhash)
        sp.error = QStringLiteral("(warning) .kmotor changed on disk since the actuator was authored");  // non-fatal
    sp.motorName = mo["name"].toString().toStdString();
    sp.kt = mo["torqueConstant"].toDouble(0.0);           // N*m / A
    sp.maxCurrent = mo["maxCurrent"].toDouble(0.0);       // A
    sp.motorMaxSpeed = mo["maxSpeed"].toDouble(0.0);      // rad/s (motor shaft)

    // DERIVED joint-side limits: torque multiplies through the gearbox, speed divides.
    sp.jointEffort   = sp.kt * sp.maxCurrent * sp.ratio * sp.efficiency;
    sp.jointVelocity = (sp.ratio > 0.0) ? sp.motorMaxSpeed / sp.ratio : 0.0;
    sp.ok = sp.kt > 0.0 && sp.maxCurrent > 0.0 && sp.motorMaxSpeed > 0.0;
    if (!sp.ok && sp.error.isEmpty()) sp.error = QStringLiteral(".kmotor missing Kt/current/speed.");

    // Optional CHARACTERIZED reference: a .klut of real measured behavior. The actuator-level ref (resolved
    // from the .kactuator's dir) wins; else a motor-level ref (from the .kmotor's dir). Non-fatal on miss.
    auto loadCharacterized = [&](const QJsonObject& obj, const QDir& baseDir) {
        const QJsonObject ch = obj["characterized"].toObject();
        const QString cref = ch["ref"].toString();
        if (cref.isEmpty()) return;
        krs::klut::Lut lut; QString cerr;
        if (krs::klut::loadKLut(baseDir.filePath(cref), lut, &cerr)) {
            sp.hasCharacterized = true; sp.characterizedLut = std::move(lut);
            sp.characterizedQuantity = ch["quantity"].toString().toStdString();
        } else if (sp.error.isEmpty()) {
            sp.error = QStringLiteral("(warning) characterized .klut: %1").arg(cerr);
        }
    };
    loadCharacterized(ao, QFileInfo(apath).absoluteDir());
    if (!sp.hasCharacterized) loadCharacterized(mo, QFileInfo(mpath).absoluteDir());
    return sp;
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

    // ---- ACTUATOR CHAIN: .kactuator -> .kmotor -> derived joint limits match the closed form ----
    bool actOk = false, actNeg = false;
    {
        // A Maxon-style EC-45flat-ish motor + a 100:1 gearbox at 80% efficiency.
        const double kt = 0.036, imax = 6.0, wmax = 800.0, ratio = 100.0, eff = 0.8;
        QJsonObject mo; mo["format"] = QStringLiteral("kmotor/1"); mo["name"] = QStringLiteral("EC-45flat");
        mo["torqueConstant"] = kt; mo["maxCurrent"] = imax; mo["maxSpeed"] = wmax;
        const QString mpath = QDir(dir).filePath("lib/EC45.kmotor");
        writeJsonFile(mpath, mo);
        QJsonObject ao; ao["format"] = QStringLiteral("kactuator/1"); ao["name"] = QStringLiteral("EC45+GP42");
        ao["gearRatio"] = ratio; ao["efficiency"] = eff;
        QJsonObject mref; mref["ref"] = QStringLiteral("EC45.kmotor"); mref["contentHash"] = fileSha1(mpath);
        ao["motor"] = mref;
        const QString apath = QDir(dir).filePath("lib/EC45_GP42.kactuator");
        writeJsonFile(apath, ao);

        const ActuatorSpec sp = resolveActuator(apath.toStdString());
        const double wantEffort = kt * imax * ratio * eff;   // 0.036*6*100*0.8 = 17.28 N*m
        const double wantVel    = wmax / ratio;              // 8.0 rad/s
        actOk = sp.ok && std::abs(sp.jointEffort - wantEffort) < 1e-9
                      && std::abs(sp.jointVelocity - wantVel) < 1e-9;

        // CHARACTERIZED: reference a real torque-speed .klut from the actuator -> resolveActuator loads it.
        {
            krs::klut::Lut curve; curve.name = "torque-speed"; curve.quantity = "torque"; curve.unit = "N*m";
            krs::klut::Axis ax; ax.name = "speed"; ax.unit = "rad/s"; ax.breakpoints = { 0.0, 4.0, 8.0 };
            curve.axes = { ax }; curve.values = { 20.0, 10.0, 0.0 };   // stall 20 N*m -> 0 at no-load 8 rad/s
            krs::klut::saveKLut(curve, QDir(dir).filePath("lib/EC45_torque.klut"));
            QJsonObject aoc = ao; QJsonObject ch;
            ch["ref"] = QStringLiteral("EC45_torque.klut"); ch["quantity"] = QStringLiteral("torqueSpeed");
            aoc["characterized"] = ch; writeJsonFile(apath, aoc);
            const ActuatorSpec spc = resolveActuator(apath.toStdString());
            const bool charOk = spc.hasCharacterized && spc.characterizedLut.axes.size() == 1
                && std::abs(spc.characterizedLut.sample1D(4.0) - 10.0) < 1e-9
                && spc.characterizedQuantity == "torqueSpeed";
            printf("[ksave]   actuator CHARACTERIZED .klut ref: loaded=%s real torque@4rad/s=%.2f (want 10)  %s\n",
                   spc.hasCharacterized ? "yes" : "no",
                   spc.hasCharacterized ? spc.characterizedLut.sample1D(4.0) : -1.0, charOk ? "PASS" : "FAIL");
            actOk = actOk && charOk;
        }
        // NEG-CTRL: a missing .kmotor is refused, not silently zeroed.
        QFile::remove(mpath);
        const ActuatorSpec bad = resolveActuator(apath.toStdString());
        actNeg = !bad.ok;
        printf("[ksave]   actuator chain: derived effort=%.3f N*m (want %.3f) velocity=%.3f rad/s (want %.3f)=%s ; "
               "NEG missing .kmotor refused=%s  %s\n",
               sp.jointEffort, wantEffort, sp.jointVelocity, wantVel, actOk ? "yes" : "NO",
               actNeg ? "yes" : "NO", (actOk && actNeg) ? "PASS" : "FAIL");
    }

    const bool pass = savedOk && loadOk && stateOk && connOk && tamperOk && staleOk
                   && negMissing && negCorrupt && actOk && actNeg;
    printf("[ksave] %s\n", pass ? "ALL PASS (nested kscene/krobot/kjoint round-trip; kstate best-effort; tamper detected+honored; actuator chain derives limits; refusals clean)"
                                : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ================================================================================================
// GATE SCENESAVE -- non-robot content (environment, lights, loose objects) round-trips a save/load.
// ================================================================================================
bool runSceneObjectsGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[scenesave] GATE SCENESAVE -- environment + lights + loose objects survive save -> fresh scene -> load\n");
    const QString dir = QDir::temp().filePath("krs_scenesave_gate");
    QDir(dir).removeRecursively();
    const std::string scenePath = QDir(dir).filePath("room.kscene").toStdString();
    const QVariant priorLastScene = QSettings().value(QStringLiteral("ksave/lastScene"));
    struct SettingsRestore {
        QVariant v;
        ~SettingsRestore() {
            if (v.isValid()) QSettings().setValue(QStringLiteral("ksave/lastScene"), v);
            else             QSettings().remove(QStringLiteral("ksave/lastScene"));
        }
    } restoreGuard{ priorLastScene };

    // ---- author a ROBOT-FREE scene: environment + 2 lights + 2 primitive objects ----
    Scene s1;
    auto& r1 = s1.getRegistry();

    // environment (non-default so a round-trip is meaningful)
    EnvironmentSettings envIn;
    envIn.iblIntensity = 2.3f; envIn.drawSkybox = false; envIn.roomColor = { 0.2f, 0.4f, 0.6f };
    envIn.sunIntensity = 3.1f; envIn.sunColor = { 1.0f, 0.9f, 0.7f }; envIn.sunDirection = { 0.3f, -1.0f, 0.2f };
    envIn.exposureEV = 1.5f; envIn.tonemapExposure = 0.8f; envIn.hdrEnabled = false; envIn.hdrPath = "assets/studio.hdr";
    r1.ctx().emplace<EnvironmentSettings>(envIn);
    SceneProperties propsIn;
    propsIn.fogEnabled = true; propsIn.fogColor = { 0.05f, 0.06f, 0.07f };
    propsIn.fogStartDistance = 4.0f; propsIn.fogEndDistance = 42.0f; propsIn.backgroundColor = { 0.11f, 0.12f, 0.13f, 1.0f };
    r1.ctx().emplace<SceneProperties>(propsIn);

    // light A: a spot with a custom cone + range
    entt::entity la = SceneBuilder::spawnLightEmitter(s1, LightComponent::Type::Spot, { 1, 3, -2 }, { 1.0f, 0.5f, 0.2f }, 5.5f, "KeySpot");
    { auto& lc = r1.get<LightComponent>(la); lc.innerConeDeg = 12.0f; lc.outerConeDeg = 28.0f; lc.range = 9.0f; lc.enabled = true; }
    // light B: a two-sided rect-area
    entt::entity lb = SceneBuilder::spawnLightEmitter(s1, LightComponent::Type::RectArea, { -2, 2.5f, 1 }, { 0.6f, 0.8f, 1.0f }, 3.0f, "FillPanel");
    { auto& lc = r1.get<LightComponent>(lb); lc.size = { 3.0f, 1.5f }; lc.twoSided = true; }

    // object A: a cube with a red-ish metal material + a real rotation
    entt::entity oa = SceneBuilder::spawnPrimitive(s1, int(Primitive::Cube), { 0, 0.5f, 0 }, { 1.2f, 0.8f, 0.6f }, "Anvil");
    { auto& xf = r1.get<TransformComponent>(oa); xf.rotation = glm::angleAxis(glm::radians(37.0f), glm::normalize(glm::vec3(0.2f, 1.0f, 0.1f)));
      auto& m = r1.get_or_emplace<MaterialComponent>(oa); m.albedoColor = { 0.7f, 0.15f, 0.1f }; m.metallic = 0.9f; m.roughness = 0.3f;
      m.emissiveColor = { 0.0f, 0.0f, 0.0f }; m.density = 7850.0f; m.physicalName = "Steel AISI 1045"; }
    // object B: a sphere with a glassy transmission material
    entt::entity ob = SceneBuilder::spawnPrimitive(s1, int(Primitive::IcoSphere), { 2, 1, 0.5f }, { 0.5f, 0.5f, 0.5f }, "Marble");
    { auto& m = r1.get_or_emplace<MaterialComponent>(ob); m.transmission = 0.85f; m.ior = 1.52f; m.roughness = 0.05f; m.clearcoat = 1.0f; }

    // the KNOWLEDGE ledger: an R2S glass the robot honed (mass Measured) + traits -- must persist.
    {
        auto& wsr = krs::world::worldState(r1);
        auto& k = wsr.know("Marble");
        k.tag = krs::world::LearnTag::R2S;
        k.traits = { "liquid-container", "fragile" };
        k.params["mass"] = { 0.372, 0.02, krs::world::Prov::Measured };
        k.params["com_r"] = { 0.043, 0.003, krs::world::Prov::Measured };
    }

    // capture inputs for comparison
    const TransformComponent oaXfIn = r1.get<TransformComponent>(oa);
    const MaterialComponent  oaMatIn = r1.get<MaterialComponent>(oa);
    const LightComponent     laLcIn = r1.get<LightComponent>(la);

    const Report sr = saveScene(s1, scenePath);
    const bool savedOk = sr.ok && sr.robots == 0 && sr.lights == 2 && sr.objects == 2
                      && QFile::exists(QString::fromStdString(scenePath));
    printf("[scenesave]   save: ok=%s robots=%d lights=%d objects=%d file present=%s  %s\n",
           sr.ok ? "yes" : "NO", sr.robots, sr.lights, sr.objects,
           QFile::exists(QString::fromStdString(scenePath)) ? "yes" : "NO", savedOk ? "PASS" : "FAIL");

    // ---- fresh scene ("restart"): load and verify every domain ----
    bool envOk = false, propsOk = false, lightsOk = false, objectsOk = false;
    {
        Scene s2;
        auto& r2 = s2.getRegistry();
        const Report lrp = loadScene(s2, scenePath);

        // environment
        if (auto* env = r2.ctx().find<EnvironmentSettings>()) {
            envOk = std::abs(env->iblIntensity - 2.3f) < 1e-5 && !env->drawSkybox
                 && glm::length(env->roomColor - glm::vec3(0.2f, 0.4f, 0.6f)) < 1e-5
                 && std::abs(env->sunIntensity - 3.1f) < 1e-5 && !env->hdrEnabled
                 && env->hdrPath == "assets/studio.hdr" && std::abs(env->exposureEV - 1.5f) < 1e-5;
        }
        if (auto* props = r2.ctx().find<SceneProperties>()) {
            propsOk = props->fogEnabled && std::abs(props->fogEndDistance - 42.0f) < 1e-5
                   && glm::length(props->fogColor - glm::vec3(0.05f, 0.06f, 0.07f)) < 1e-5;
        }

        // lights: find the spot + the rect-area by their restored params
        int spots = 0, rects = 0; bool spotParamsOk = false, rectParamsOk = false;
        for (auto e : r2.view<LightComponent>()) {
            const auto& lc = r2.get<LightComponent>(e);
            if (lc.type == LightComponent::Type::Spot) {
                ++spots;
                spotParamsOk = std::abs(lc.innerConeDeg - 12.0f) < 1e-4 && std::abs(lc.outerConeDeg - 28.0f) < 1e-4
                            && std::abs(lc.range - 9.0f) < 1e-4 && std::abs(lc.intensity - 5.5f) < 1e-4
                            && glm::length(lc.color - glm::vec3(1.0f, 0.5f, 0.2f)) < 1e-4;
            } else if (lc.type == LightComponent::Type::RectArea) {
                ++rects;
                rectParamsOk = lc.twoSided && glm::length(glm::vec2(lc.size) - glm::vec2(3.0f, 1.5f)) < 1e-4;
            }
        }
        lightsOk = spots == 1 && rects == 1 && spotParamsOk && rectParamsOk;

        // objects: match by name, verify transform (incl. rotation) + material fields
        int found = 0; bool cubeOk = false, sphereOk = false;
        for (auto e : r2.view<SceneObjectComponent, TransformComponent, TagComponent>()) {
            if (isRobotOrLight(r2, e)) continue;
            const std::string nm = r2.get<TagComponent>(e).tag;
            const auto& xf = r2.get<TransformComponent>(e);
            const auto* m = r2.try_get<MaterialComponent>(e);
            if (nm == "Anvil") {
                ++found;
                const bool xfOk = glm::length(xf.translation - oaXfIn.translation) < 1e-5
                               && glm::length(xf.scale - oaXfIn.scale) < 1e-5
                               && (std::abs(glm::dot(xf.rotation, oaXfIn.rotation)) > 0.9999f); // same orientation (sign-agnostic)
                cubeOk = xfOk && m && glm::length(m->albedoColor - glm::vec3(0.7f, 0.15f, 0.1f)) < 1e-5
                      && std::abs(m->metallic - 0.9f) < 1e-5 && std::abs(m->roughness - 0.3f) < 1e-5
                      && std::abs(m->density - 7850.0f) < 1e-2 && m->physicalName == "Steel AISI 1045";
            } else if (nm == "Marble") {
                ++found;
                sphereOk = m && std::abs(m->transmission - 0.85f) < 1e-5 && std::abs(m->ior - 1.52f) < 1e-5
                        && std::abs(m->clearcoat - 1.0f) < 1e-5;
            }
        }
        objectsOk = found == 2 && cubeOk && sphereOk;

        // KNOWLEDGE round-trip: the honed R2S glass reloads with its measured params + traits +
        // tag intact -- the robot must NOT need to re-derive yesterday's glass.
        bool knowOk = false;
        {
            const auto* k = krs::world::worldState(r2).knowledgeOf("Marble");
            knowOk = k && k->tag == krs::world::LearnTag::R2S
                && k->hasTrait("liquid-container") && k->hasTrait("fragile")
                && k->params.count("mass") && k->params.count("com_r")
                && std::abs(k->params.at("mass").value - 0.372) < 1e-12
                && std::abs(k->params.at("mass").sigma - 0.02) < 1e-12
                && k->params.at("mass").prov == krs::world::Prov::Measured
                && krs::world::worldState(r2).needSatisfied("Marble", "mass", 0.05);
        }
        objectsOk = objectsOk && knowOk;

        printf("[scenesave]   load: ok=%s env=%s sceneProps=%s lights(spot+rect params)=%s objects(xf+mat)=%s knowledge(r2s+traits+measured-params)=%s\n",
               lrp.ok ? "yes" : "NO", envOk ? "yes" : "NO", propsOk ? "yes" : "NO",
               lightsOk ? "yes" : "NO", objectsOk ? "yes" : "NO", knowOk ? "yes" : "NO");
        (void)laLcIn; (void)oaMatIn;
    }

    // ---- NEG-CTRL: an object with NO recipe (primitive=-1, mesh asset) is skipped with a warning,
    //      not silently fabricated. Hand-craft a .kscene with one such object and confirm it warns. ----
    bool negOk = false;
    {
        const std::string negPath = QDir(dir).filePath("negctrl.kscene").toStdString();
        QJsonObject sc; sc["format"] = QStringLiteral("kscene/1"); sc["name"] = QStringLiteral("neg");
        sc["robots"] = QJsonArray{};
        QJsonObject bad; bad["primitive"] = -1; bad["mesh"] = QStringLiteral("assets/missing.obj"); bad["name"] = QStringLiteral("Ghost");
        bad["transform"] = QJsonObject{ { "t", QJsonArray{ 0, 0, 0 } }, { "r", QJsonArray{ 1, 0, 0, 0 } }, { "s", QJsonArray{ 1, 1, 1 } } };
        sc["objects"] = QJsonArray{ bad };
        writeJsonFile(QString::fromStdString(negPath), sc);
        Scene s3;
        const Report lrp = loadScene(s3, negPath);
        int loose = 0; for (auto e : s3.getRegistry().view<SceneObjectComponent>()) { (void)e; ++loose; }
        bool warned = false; for (const QString& w : lrp.warnings) if (w.contains("mesh asset")) warned = true;
        negOk = warned && lrp.objects == 0 && loose == 0;
        printf("[scenesave]   NEG-CTRL mesh-asset object without a loader is skipped+warned (not fabricated)=%s  %s\n",
               negOk ? "yes" : "NO", negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");
    }

    const bool pass = savedOk && envOk && propsOk && lightsOk && objectsOk && negOk;
    printf("[scenesave] %s\n", pass ? "ALL PASS (environment/skybox + fog + lights (all params) + loose objects (transform incl. rotation + full material) round-trip a save/load; recipe-less mesh objects skipped honestly)"
                                    : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::ksave
