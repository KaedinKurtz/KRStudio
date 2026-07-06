// KSave.cpp -- see KSave.hpp. The .kscene/.krobot/.kjoint/.kstate family: composition by
// reference (relative path + id + content hash), deterministic geometry rebuild + kjoint overlay,
// best-effort session state. All JSON, all versioned, nothing silently rebinds.
//
// v1.2 PERSISTENCE UNIFICATION (E1.1): constraints, groups, face roles, loose-body mate
// connectors and effector attachments survive a save/load. Cross-entity relations are routed
// through PersistentIdComponent pids minted at save (stable across re-saves); the loader spawns
// everything first (PASS 1, building pid -> new entity), then resolves every relation (PASS 2).
// A relation whose endpoint cannot resolve is DROPPED with a Report warning naming it -- never
// a crash, never a guess. Old 1.x scenes (no new sections) load with zero warnings.
#include "KSave.hpp"
#include "RobotBuilder.hpp"
#include "RobotBuilderScene.hpp"   // buildDemoGraph / spawnGraphBodies
#include "RobotModel.hpp"          // RobotRegistry / LiveRobot / instantiateFromGraph / transformRobot
#include "CadImporter.hpp"         // importStepAssembly (STEP-sourced robots)
#include "SceneBuilder.hpp"        // spawnPrimitive / spawnLightEmitter (loose-object + light rebuild)
#include "WorldState.hpp"          // krs::world knowledge (learned params/traits/tags persist per object)
#include "Scene.hpp"
#include "components.hpp"
#include "Constraint.hpp"          // krs::constraint -- REUSED Constraint/graph JSON codec; pids wrap the entity fields
#include "FaceRole.hpp"            // FaceRoleComponent -- per-faceKey semantic roles on loose bodies
#include "GroupOps.hpp"            // krs::group -- membership walk (save) + leaf expansion (visibility) + gate checks
#include "EffectorStudioPanel.hpp" // krs::ee::AttachedEffectors / Attachment -- the ctx attachment registry

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
#include <unordered_map>
#include <unordered_set>

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

bool formatOk(const QJsonObject& o, const char* family) {   // "kjoint/1", "kscene/1.2" -> family match +
    const QString f = o["format"].toString();               // MAJOR 1 (any 1.x minor is accepted; unknown
    return f.startsWith(QLatin1String(family))              //  majors are refused -- the ksave contract)
        && f.section('/', 1, 1).section('.', 0, 0).toInt() == 1;
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

// ==================== v1.2 PERSISTENT IDENTITY + RELATIONS (E1.1) ====================
// Document-level pid allocator (registry ctx). Monotonic and NEVER reused within a session; the
// .kscene stores "nextPid" so a later session continues where the document left off. A pid whose
// entity was deleted between saves is simply RETIRED (relations that referenced it drop with a
// warning at the next load).
struct PidAllocator { std::uint64_t next = 1; };

PidAllocator& pidAllocator(entt::registry& reg) {
    auto* a = reg.ctx().find<PidAllocator>();
    if (!a) a = &reg.ctx().emplace<PidAllocator>();
    for (auto e : reg.view<PersistentIdComponent>())            // never mint below an existing pid
        a->next = std::max(a->next, reg.get<PersistentIdComponent>(e).pid + 1);
    return *a;
}

// Reuse an existing pid (repeated saves are stable) or mint a fresh one onto the entity.
std::uint64_t mintPid(entt::registry& reg, PidAllocator& alloc, entt::entity e) {
    if (const auto* p = reg.try_get<PersistentIdComponent>(e); p && p->pid != 0) return p->pid;
    const std::uint64_t pid = alloc.next++;
    reg.emplace_or_replace<PersistentIdComponent>(e, PersistentIdComponent{ pid });
    return pid;
}

// The authoring graph for a robot id: the ACTIVE ctx graph when it is that robot's, else the
// parked one in the AuthoringGraphStore. Null when the robot has no graph in this session.
const krs::rbuild::RobotGraph* graphForRobot(entt::registry& reg, int robotId) {
    if (const auto* g = reg.ctx().find<krs::rbuild::RobotGraph>())
        if (g->robotId == robotId && !g->bodies.empty()) return g;
    if (const auto* store = reg.ctx().find<krs::rbuild::AuthoringGraphStore>()) {
        const auto it = store->byRobot.find(robotId);
        if (it != store->byRobot.end()) return &it->second;
    }
    return nullptr;
}

// ROBOT MEMBERS never get pids: a relation endpoint that lives on a robot serializes as
// { robot, body, slot, link } -- graph body INDEX as the fast hint, body NAME as the durable
// fallback (the loader re-finds by name when the index drifted). false = not a resolvable
// robot member (caller warns + drops the relation).
bool robotMemberRefToJson(entt::registry& reg, entt::entity e, QJsonObject& out) {
    const auto* sub = reg.try_get<RobotSubcomponentComponent>(e);
    if (!sub) return false;
    const auto* g = graphForRobot(reg, sub->robotId);
    if (!g) return false;
    int body = -1, slot = -1;
    if (!entityToRef(*g, e, body, slot)) return false;
    out["robot"] = sub->robotId;
    out["body"]  = body;
    out["slot"]  = slot;
    out["link"]  = QString::fromStdString(g->bodies[std::size_t(body)].name);
    return true;
}
entt::entity robotMemberRefFromJson(entt::registry& reg, const QJsonObject& o, QString* why) {
    const int robotId = o["robot"].toInt(-1);
    auto* rr = reg.ctx().find<krs::robot::RobotRegistry>();
    if (!rr || !rr->get(robotId)) {
        if (why) *why = QStringLiteral("robot %1 is not in the scene").arg(robotId);
        return entt::null;
    }
    const auto* g = graphForRobot(reg, robotId);
    if (!g) { if (why) *why = QStringLiteral("robot %1 has no authoring graph").arg(robotId); return entt::null; }
    int body = o["body"].toInt(-1);
    const int slot = o["slot"].toInt(0);
    const std::string link = o["link"].toString().toStdString();
    // The index is a HINT validated against the name; on drift the name re-finds the body.
    if (body < 0 || body >= int(g->bodies.size())
        || (!link.empty() && g->bodies[std::size_t(body)].name != link)) {
        body = -1;
        for (int i = 0; i < int(g->bodies.size()); ++i)
            if (g->bodies[std::size_t(i)].name == link) { body = i; break; }
    }
    if (body < 0) {
        if (why) *why = QStringLiteral("link \"%1\" not found on robot %2")
                            .arg(QString::fromStdString(link)).arg(robotId);
        return entt::null;
    }
    const entt::entity e = refToEntity(*g, body, slot);
    if (e == entt::null || !reg.valid(e)) {
        if (why) *why = QStringLiteral("link \"%1\" on robot %2 resolved to a dead entity")
                            .arg(QString::fromStdString(link)).arg(robotId);
        return entt::null;
    }
    return e;
}

// BRepFace <-> JSON: the durable identity layer of a LOOSE body. Persisted with the object
// because keyed anchors (constraint anchors, connector sourceFaceKeys, role faceKeys) can only
// re-find their geometry through the body's BRepFaceComponent (anchorWorldFrame contract) --
// a loaded loose body without its faces would strand every keyed relation on it.
QJsonObject brepFaceToJson(const BRepFace& f) {
    QJsonObject o;
    o["type"] = f.type;
    o["axisPos"] = vec3ToJson(f.axisPos); o["axisDir"] = vec3ToJson(f.axisDir);
    o["normal"] = vec3ToJson(f.normal);   o["radius"] = double(f.radius);
    o["end0"] = vec3ToJson(f.axisEnd0);   o["end1"] = vec3ToJson(f.axisEnd1);
    o["key"] = u64ToJson(f.faceKey);
    return o;
}
BRepFace brepFaceFromJson(const QJsonObject& o) {
    BRepFace f;
    f.type = o["type"].toInt(0);
    f.axisPos = vec3FromJson(o["axisPos"]); f.axisDir = vec3FromJson(o["axisDir"], { 0, 0, 1 });
    f.normal = vec3FromJson(o["normal"], { 0, 0, 1 }); f.radius = float(o["radius"].toDouble(0.0));
    f.axisEnd0 = vec3FromJson(o["end0"]); f.axisEnd1 = vec3FromJson(o["end1"]);
    f.faceKey = u64FromJson(o["key"]);
    return f;
}

// MateConnectorComponent <-> JSON -- THE connector codec (the .krobot connectorSets entries and
// the v1.2 loose-body "connectors" section share it, so the two paths cannot drift).
QJsonObject connectorSetToJson(const MateConnectorComponent& mc) {
    QJsonObject o;
    o["nextId"] = int(mc.nextConnectorId);
    QJsonArray list;
    for (const auto& c : mc.connectors) {
        QJsonObject co;
        co["id"] = int(c.id); co["name"] = QString::fromStdString(c.name);
        co["pos"] = vec3ToJson(c.localPos); co["z"] = vec3ToJson(c.localZ); co["x"] = vec3ToJson(c.localX);
        co["key"] = u64ToJson(c.sourceFaceKey); co["ftype"] = c.sourceFaceType; co["radius"] = double(c.radius);
        list.push_back(co);
    }
    o["connectors"] = list;
    return o;
}
void connectorSetFromJson(const QJsonObject& o, MateConnectorComponent& mc) {
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
            QJsonObject o = connectorSetToJson(reg.get<MateConnectorComponent>(e));
            o["body"] = body; o["slot"] = slot;
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
    sc["format"] = QStringLiteral("kscene/1.2");    // v1.2: relations (pids); loaders accept any 1.x
    sc["name"] = sceneInfo.completeBaseName();
    sc["robots"] = instances;

    // ---- v1.2: persistent-id allocator; pids ride on the object/light/group entries below, and
    //      savedPids is the loader's RESOLVABLE UNIVERSE -- a relation referencing anything else
    //      is dropped NOW with a warning (never write a relation that cannot load). ----
    PidAllocator& pidAlloc = pidAllocator(reg);
    std::unordered_set<std::uint64_t> savedPids;

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
        const std::uint64_t lpid = mintPid(reg, pidAlloc, e);            // v1.2 identity
        lo["pid"] = u64ToJson(lpid);
        savedPids.insert(lpid);
        if (reg.all_of<HiddenComponent>(e)) lo["hidden"] = true;         // outliner-eye state
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
        const std::uint64_t opid = mintPid(reg, pidAlloc, e);            // v1.2 identity
        oo["pid"] = u64ToJson(opid);
        savedPids.insert(opid);
        if (reg.all_of<HiddenComponent>(e)) oo["hidden"] = true;         // outliner-eye state
        // v1.2: the body's durable-identity faces (keyed anchors re-find through these on load).
        if (const auto* fc = reg.try_get<BRepFaceComponent>(e); fc && !fc->faces.empty()) {
            QJsonArray fa;
            for (const auto& f : fc->faces) fa.push_back(brepFaceToJson(f));
            oo["brepFaces"] = fa;
        }
        objects.push_back(oo);
        ++rep.objects;
    }
    sc["objects"] = objects;

    // ================================ v1.2 RELATIONS ================================
    // All sections are OPTIONAL on load (old 1.x scenes simply have none). Every relation whose
    // endpoint is not in this document is dropped HERE with a warning naming it.

    // ---- groups: each ROOT is a meshless first-class node -> its own entry. Flat membership;
    //      NESTING RECONSTRUCTS FROM memberPids (a member pid that is itself a group root's pid
    //      re-nests the subtree; parentGroupPid is written as a redundant readability channel,
    //      the loader does not need it). ----
    {
        for (auto e : reg.view<GroupComponent>())                        // mint roots FIRST so
            savedPids.insert(mintPid(reg, pidAlloc, e));                 // nested refs resolve
        QJsonArray groups;
        for (auto e : reg.view<GroupComponent>()) {
            const auto& gc = reg.get<GroupComponent>(e);
            QJsonObject go;
            go["pid"] = u64ToJson(reg.get<PersistentIdComponent>(e).pid);
            go["name"] = QString::fromStdString(gc.name);
            go["visible"] = gc.visible;
            if (reg.all_of<TransformComponent>(e))
                go["transform"] = transformToJson(reg.get<TransformComponent>(e));
            QJsonArray memberPids, memberRoots;
            for (entt::entity m : krs::group::groupMembers(reg, e)) {
                if (const auto* rrc = reg.try_get<RobotRootComponent>(m)) {   // robot roots: by robotId
                    memberRoots.push_back(rrc->robotId);
                    continue;
                }
                const auto* p = reg.try_get<PersistentIdComponent>(m);
                if (p && p->pid != 0 && savedPids.count(p->pid)) {
                    memberPids.push_back(u64ToJson(p->pid));
                } else {
                    QString nm = reg.all_of<TagComponent>(m)
                        ? QString::fromStdString(reg.get<TagComponent>(m).tag) : QStringLiteral("<unnamed>");
                    rep.warnings << QStringLiteral("Group \"%1\": member \"%2\" is not persisted in this "
                                                   "document (no object recipe) -- membership dropped.")
                                        .arg(QString::fromStdString(gc.name), nm);
                }
            }
            go["memberPids"] = memberPids;
            if (!memberRoots.isEmpty()) go["memberRobotRoots"] = memberRoots;
            std::uint64_t parentPid = 0;
            if (const auto* gm = reg.try_get<GroupMemberComponent>(e))
                if (reg.valid(gm->group) && reg.all_of<GroupComponent>(gm->group)
                    && reg.all_of<PersistentIdComponent>(gm->group))
                    parentPid = reg.get<PersistentIdComponent>(gm->group).pid;
            go["parentGroupPid"] = u64ToJson(parentPid);
            groups.push_back(go);
        }
        if (!groups.isEmpty()) sc["groups"] = groups;
    }

    // ---- faceRoles: per-pid role maps for LOOSE bodies (robot-member roles are .kee territory). ----
    {
        QJsonArray faceRoles;
        for (auto e : reg.view<FaceRoleComponent>()) {
            const auto& fr = reg.get<FaceRoleComponent>(e);
            if (fr.byFaceKey.empty()) continue;
            if (reg.any_of<RobotSubcomponentComponent, RobotRootComponent>(e)) continue;  // rides with the .kee
            const auto* p = reg.try_get<PersistentIdComponent>(e);
            if (!p || p->pid == 0 || !savedPids.count(p->pid)) {
                QString nm = reg.all_of<TagComponent>(e)
                    ? QString::fromStdString(reg.get<TagComponent>(e).tag) : QStringLiteral("<unnamed>");
                rep.warnings << QStringLiteral("Face roles on \"%1\" (%2 face(s)): body is not persisted in "
                                               "this document -- roles dropped.").arg(nm).arg(int(fr.byFaceKey.size()));
                continue;
            }
            QJsonObject o; o["pid"] = u64ToJson(p->pid);
            QJsonArray roles;
            for (const auto& [key, en] : fr.byFaceKey) {
                QJsonObject ro;
                ro["key"] = u64ToJson(key);
                ro["role"] = int(en.role);
                ro["tag"] = QString::fromStdString(en.tag);
                ro["friction"] = double(en.friction);
                ro["maxNormalForceN"] = double(en.maxNormalForceN);
                roles.push_back(ro);
            }
            o["roles"] = roles;
            faceRoles.push_back(o);
        }
        if (!faceRoles.isEmpty()) sc["faceRoles"] = faceRoles;
    }

    // ---- connectors: LOOSE-body mate-connector sets, per pid (robot sets ride in the .krobot). ----
    {
        QJsonArray looseConns;
        for (auto e : reg.view<MateConnectorComponent>()) {
            if (reg.any_of<RobotSubcomponentComponent, RobotRootComponent>(e)) continue;
            const auto& mc = reg.get<MateConnectorComponent>(e);
            if (mc.connectors.empty()) continue;
            const auto* p = reg.try_get<PersistentIdComponent>(e);
            if (!p || p->pid == 0 || !savedPids.count(p->pid)) {
                QString nm = reg.all_of<TagComponent>(e)
                    ? QString::fromStdString(reg.get<TagComponent>(e).tag) : QStringLiteral("<unnamed>");
                rep.warnings << QStringLiteral("Mate connectors on \"%1\" (%2): body is not persisted in "
                                               "this document -- connectors dropped.").arg(nm).arg(int(mc.connectors.size()));
                continue;
            }
            QJsonObject o = connectorSetToJson(mc);
            o["pid"] = u64ToJson(p->pid);
            looseConns.push_back(o);
        }
        if (!looseConns.isEmpty()) sc["connectors"] = looseConns;
    }

    // ---- constraints: the ctx graph, REUSING krs::constraint::toJson per item; the two anchor
    //      entity fields are meaningless across the save boundary, so they are zeroed and the
    //      endpoints travel as bodyPidA/bodyPidB instead (unwrapped through pidMap on load). ----
    if (const auto* cg = reg.ctx().find<krs::constraint::ConstraintGraphComponent>();
        cg && (!cg->constraints.empty() || cg->nextId > 1)) {
        QJsonObject co;
        co["nextId"] = u64ToJson(cg->nextId);
        co["showIcons"] = cg->showIcons;
        QJsonArray items;
        for (const auto& c : cg->constraints) {
            const auto endpointPid = [&](entt::entity body, const char* side, std::uint64_t& out) {
                const QString ident = QStringLiteral("Constraint #%1 (%2)")
                    .arg(qulonglong(c.id)).arg(QLatin1String(krs::constraint::cTypeName(c.type)));
                if (body == entt::null || !reg.valid(body)) {
                    rep.warnings << QStringLiteral("%1: anchor %2 body is dead -- constraint not saved.")
                                        .arg(ident, QLatin1String(side));
                    return false;
                }
                if (reg.any_of<RobotSubcomponentComponent, RobotRootComponent>(body)) {
                    rep.warnings << QStringLiteral("%1: anchor %2 is a robot member (constraints refuse robot "
                                                   "members) -- constraint not saved.").arg(ident, QLatin1String(side));
                    return false;
                }
                const auto* p = reg.try_get<PersistentIdComponent>(body);
                if (!p || p->pid == 0 || !savedPids.count(p->pid)) {
                    rep.warnings << QStringLiteral("%1: anchor %2 body is not persisted in this document -- "
                                                   "constraint not saved.").arg(ident, QLatin1String(side));
                    return false;
                }
                out = p->pid;
                return true;
            };
            std::uint64_t pa = 0, pb = 0;
            if (!endpointPid(c.a.body, "A", pa) || !endpointPid(c.b.body, "B", pb)) continue;
            QJsonObject o = krs::constraint::toJson(c);
            QJsonObject ja = o["a"].toObject(); ja["body"] = 0.0; o["a"] = ja;
            QJsonObject jb = o["b"].toObject(); jb["body"] = 0.0; o["b"] = jb;
            o["bodyPidA"] = u64ToJson(pa);
            o["bodyPidB"] = u64ToJson(pb);
            items.push_back(o);
        }
        co["items"] = items;
        sc["constraints"] = co;
    }

    // ---- attachments: robotId + flange (robot-member ref) + connectorId + effector body pids. ----
    if (const auto* ae = reg.ctx().find<krs::ee::AttachedEffectors>(); ae && !ae->list.empty()) {
        QJsonArray atts;
        for (const auto& at : ae->list) {
            const QString ident = QStringLiteral("Attachment \"%1\" (robot %2, connector %3)")
                .arg(at.effectorName).arg(at.robotId).arg(at.connectorId);
            QJsonObject flange;
            if (!reg.valid(at.flangeBody) || !robotMemberRefToJson(reg, at.flangeBody, flange)) {
                rep.warnings << QStringLiteral("%1: flange body is not a resolvable robot member -- "
                                               "attachment not saved.").arg(ident);
                continue;
            }
            QJsonArray bodyPids;
            bool bodiesOk = true;
            for (entt::entity be : at.effectorBodies) {
                const auto* p = reg.valid(be) ? reg.try_get<PersistentIdComponent>(be) : nullptr;
                if (!p || p->pid == 0 || !savedPids.count(p->pid)) { bodiesOk = false; break; }
                bodyPids.push_back(u64ToJson(p->pid));
            }
            if (!bodiesOk) {
                rep.warnings << QStringLiteral("%1: an effector body is not persisted in this document "
                                               "(mesh-asset bodies are a follow-up) -- attachment not saved.").arg(ident);
                continue;
            }
            QJsonObject o;
            o["robotId"] = at.robotId;
            o["flange"] = flange;
            o["connectorId"] = int(at.connectorId);
            o["effectorName"] = at.effectorName;
            o["effectorBodyPids"] = bodyPids;
            atts.push_back(o);
        }
        if (!atts.isEmpty()) sc["attachments"] = atts;
    }

    sc["nextPid"] = u64ToJson(pidAlloc.next);       // the document-level allocator (never reused)

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
    // recipe-tagged objects + group roots + the relation registries). Clear all of them first.
    // (Procedural/gizmo entities with no SceneObjectComponent and no LightComponent are left
    // untouched, as in v1.)
    {
        std::vector<entt::entity> kill;
        for (auto e : reg.view<RobotSubcomponentComponent>()) kill.push_back(e);
        for (auto e : reg.view<RobotRootComponent>()) kill.push_back(e);
        for (auto e : reg.view<LightComponent>()) kill.push_back(e);
        for (auto e : reg.view<SceneObjectComponent>()) kill.push_back(e);
        for (auto e : reg.view<GroupComponent>()) kill.push_back(e);          // v1.2: group roots
        std::sort(kill.begin(), kill.end());
        kill.erase(std::unique(kill.begin(), kill.end()), kill.end());
        for (auto e : kill) if (reg.valid(e)) reg.destroy(e);
        if (auto* rr = reg.ctx().find<krs::robot::RobotRegistry>()) rr->robots.clear();
        if (auto* g = reg.ctx().find<krs::rbuild::RobotGraph>()) *g = krs::rbuild::RobotGraph{};
        if (auto* mg = reg.ctx().find<MateGraphComponent>()) { mg->mates.clear(); }
        if (auto* store = reg.ctx().find<krs::rbuild::AuthoringGraphStore>()) store->byRobot.clear();
        // v1.2: the document owns the relation registries too (absent sections load as EMPTY).
        if (auto* cg = reg.ctx().find<krs::constraint::ConstraintGraphComponent>())
            *cg = krs::constraint::ConstraintGraphComponent{};
        if (auto* ae = reg.ctx().find<krs::ee::AttachedEffectors>()) ae->list.clear();
    }

    // v1.2 PASS-1 identity table: pid -> freshly spawned entity. Filled by the light/object/group
    // spawn loops below; every relation resolves through it in PASS 2 (after everything exists).
    std::unordered_map<std::uint64_t, entt::entity> pidMap;
    const auto adoptPid = [&](entt::entity e, const QJsonObject& src) {
        const std::uint64_t pid = u64FromJson(src["pid"]);
        if (pid == 0) return;                                  // old 1.x entry: no identity carried
        reg.emplace_or_replace<PersistentIdComponent>(e, PersistentIdComponent{ pid });
        pidMap[pid] = e;
    };
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
            connectorSetFromJson(o, reg.get_or_emplace<MateConnectorComponent>(e));
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
        adoptPid(e, lo);                                             // v1.2 identity
        if (lo["hidden"].toBool(false)) reg.emplace_or_replace<HiddenComponent>(e);
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
        adoptPid(e, oo);                                             // v1.2 identity
        if (oo["hidden"].toBool(false)) reg.emplace_or_replace<HiddenComponent>(e);
        if (oo.contains("brepFaces")) {                              // durable-identity faces back on
            auto& fc = reg.emplace_or_replace<BRepFaceComponent>(e); // the body (keyed anchors re-find)
            for (const QJsonValue& fv : oo["brepFaces"].toArray())
                fc.faces.push_back(brepFaceFromJson(fv.toObject()));
        }
        ++rep.objects;
    }

    // ================================ v1.2 RELATIONS: PASS 1 tail + PASS 2 ================================
    // Group ROOTS are meshless entities the document owns -- spawn them (finishing PASS 1) ...
    const QJsonArray groupsArr = sc["groups"].toArray();
    for (const QJsonValue& gv : groupsArr) {
        const QJsonObject go = gv.toObject();
        const entt::entity root = reg.create();
        TransformComponent xf; transformFromJson(go["transform"].toObject(), xf);
        reg.emplace<TransformComponent>(root, xf);
        auto& gc = reg.emplace<GroupComponent>(root);
        gc.name = go["name"].toString().toStdString();
        gc.visible = go["visible"].toBool(true);
        gc.lastXf = xf.getTransform();                    // fan-out baseline = the loaded pose (no motion)
        reg.emplace<TagComponent>(root, gc.name);
        adoptPid(root, go);
    }

    // ... then resolve EVERY relation now that everything exists (PASS 2). Anything that cannot
    // resolve drops with ONE warning naming it; nothing crashes, nothing guesses, nothing moves.

    // ---- groups: membership via pidMap (nesting falls out of member pids that are group roots). ----
    for (const QJsonValue& gv : groupsArr) {
        const QJsonObject go = gv.toObject();
        const auto itR = pidMap.find(u64FromJson(go["pid"]));
        if (itR == pidMap.end()) continue;                            // guard (roots were just spawned)
        const entt::entity root = itR->second;
        const QString gname = go["name"].toString();
        for (const QJsonValue& mv : go["memberPids"].toArray()) {
            const std::uint64_t mp = u64FromJson(mv);
            const auto itM = pidMap.find(mp);
            if (itM == pidMap.end()) {
                rep.warnings << QStringLiteral("Group \"%1\": member pid %2 did not resolve -- membership "
                                               "dropped.").arg(gname).arg(qulonglong(mp));
                continue;
            }
            reg.emplace_or_replace<GroupMemberComponent>(itM->second, GroupMemberComponent{ root });
        }
        for (const QJsonValue& rv : go["memberRobotRoots"].toArray()) {
            const int rid = rv.toInt(-1);
            auto* rr = reg.ctx().find<krs::robot::RobotRegistry>();
            krs::robot::LiveRobot* lrb = rr ? rr->get(rid) : nullptr;
            if (!lrb || !reg.valid(lrb->root)) {
                rep.warnings << QStringLiteral("Group \"%1\": robot root %2 is not in the scene -- membership "
                                               "dropped.").arg(gname).arg(rid);
                continue;
            }
            reg.emplace_or_replace<GroupMemberComponent>(lrb->root, GroupMemberComponent{ root });
        }
    }
    // Visibility AFTER all memberships exist (leaf expansion walks the nested tree): an invisible
    // group re-hides its leaves -- the outliner-eye contract.
    for (const QJsonValue& gv : groupsArr) {
        const auto itR = pidMap.find(u64FromJson(gv.toObject()["pid"]));
        if (itR == pidMap.end()) continue;
        const auto* gc = reg.try_get<GroupComponent>(itR->second);
        if (gc && !gc->visible)
            for (entt::entity m : krs::group::leafTargets(reg, itR->second))
                reg.emplace_or_replace<HiddenComponent>(m);
    }

    // ---- faceRoles onto pidMap targets ----
    for (const QJsonValue& v : sc["faceRoles"].toArray()) {
        const QJsonObject o = v.toObject();
        const std::uint64_t pid = u64FromJson(o["pid"]);
        const auto it = pidMap.find(pid);
        if (it == pidMap.end()) {
            rep.warnings << QStringLiteral("Face roles for pid %1 did not resolve to a body -- roles "
                                           "dropped.").arg(qulonglong(pid));
            continue;
        }
        auto& fr = reg.get_or_emplace<FaceRoleComponent>(it->second);
        fr.byFaceKey.clear();
        for (const QJsonValue& rv : o["roles"].toArray()) {
            const QJsonObject ro = rv.toObject();
            FaceRoleEntry en;
            en.role = FaceRole(ro["role"].toInt(0));
            en.tag = ro["tag"].toString().toStdString();
            en.friction = float(ro["friction"].toDouble(0.8));
            en.maxNormalForceN = float(ro["maxNormalForceN"].toDouble(50.0));
            fr.byFaceKey[u64FromJson(ro["key"])] = en;
        }
    }

    // ---- loose-body connector sets onto pidMap targets ----
    for (const QJsonValue& v : sc["connectors"].toArray()) {
        const QJsonObject o = v.toObject();
        const std::uint64_t pid = u64FromJson(o["pid"]);
        const auto it = pidMap.find(pid);
        if (it == pidMap.end()) {
            rep.warnings << QStringLiteral("Mate connectors for pid %1 did not resolve to a body -- "
                                           "connectors dropped.").arg(qulonglong(pid));
            continue;
        }
        connectorSetFromJson(o, reg.get_or_emplace<MateConnectorComponent>(it->second));
    }

    // ---- constraints: unwrap bodyPidA/bodyPidB -> entities; VALIDATE, never re-snap (the saved
    //      poses already satisfy them -- zero motion on load). A missing pid drops the constraint;
    //      an anchor whose key cannot re-find its face is KEPT but warned (the durable-key
    //      contract: stale today may re-anchor after a re-import, and dropping would lose data). ----
    if (sc.contains("constraints")) {
        const QJsonObject co = sc["constraints"].toObject();
        auto& cg = krs::constraint::constraintGraph(reg);
        cg.constraints.clear();
        cg.showIcons = co["showIcons"].toBool(true);
        cg.nextId = std::max<std::uint64_t>(u64FromJson(co["nextId"]), 1);
        for (const QJsonValue& iv : co["items"].toArray()) {
            const QJsonObject o = iv.toObject();
            krs::constraint::Constraint c;
            if (!krs::constraint::fromJson(o, c)) {
                rep.warnings << QStringLiteral("A constraint entry is malformed -- dropped.");
                continue;
            }
            const QString ident = QStringLiteral("Constraint #%1 (%2)")
                .arg(qulonglong(c.id)).arg(QLatin1String(krs::constraint::cTypeName(c.type)));
            const std::uint64_t pa = u64FromJson(o["bodyPidA"]), pb = u64FromJson(o["bodyPidB"]);
            const auto ia = pidMap.find(pa), ib = pidMap.find(pb);
            if (ia == pidMap.end() || ib == pidMap.end()) {
                rep.warnings << QStringLiteral("%1: body pid %2 did not resolve -- constraint dropped.")
                                    .arg(ident).arg(qulonglong(ia == pidMap.end() ? pa : pb));
                continue;
            }
            c.a.body = ia->second;
            c.b.body = ib->second;
            const auto A = krs::constraint::anchorWorldFrame(reg, c.a);
            const auto B = krs::constraint::anchorWorldFrame(reg, c.b);
            if (!A.valid || !B.valid)
                rep.warnings << QStringLiteral("%1: anchor %2 does not resolve on the loaded body (stale "
                                               "key?) -- kept, but it cannot solve until re-anchored.")
                                    .arg(ident, QLatin1String(!A.valid ? "A" : "B"));
            cg.nextId = std::max(cg.nextId, c.id + 1);
            cg.constraints.push_back(std::move(c));
        }
    }

    // ---- attachments: flange = robot-member ref (registry + graph, name fallback); effector
    //      bodies via pidMap. A missing robot / body drops the attachment with a warning. ----
    for (const QJsonValue& av : sc["attachments"].toArray()) {
        const QJsonObject o = av.toObject();
        krs::ee::Attachment at;
        at.robotId = o["robotId"].toInt(-1);
        at.connectorId = std::uint32_t(o["connectorId"].toInt(0));
        at.effectorName = o["effectorName"].toString();
        const QString ident = QStringLiteral("Attachment \"%1\" (robot %2, connector %3)")
            .arg(at.effectorName).arg(at.robotId).arg(at.connectorId);
        QString why;
        at.flangeBody = robotMemberRefFromJson(reg, o["flange"].toObject(), &why);
        if (at.flangeBody == entt::null) {
            rep.warnings << QStringLiteral("%1: %2 -- attachment dropped.").arg(ident, why);
            continue;
        }
        bool bodiesOk = true;
        for (const QJsonValue& pv : o["effectorBodyPids"].toArray()) {
            const std::uint64_t bp = u64FromJson(pv);
            const auto it = pidMap.find(bp);
            if (it == pidMap.end()) {
                rep.warnings << QStringLiteral("%1: effector body pid %2 did not resolve -- attachment "
                                               "dropped.").arg(ident).arg(qulonglong(bp));
                bodiesOk = false;
                break;
            }
            at.effectorBodies.push_back(it->second);
        }
        if (!bodiesOk) continue;
        auto* ae = reg.ctx().find<krs::ee::AttachedEffectors>();
        if (!ae) ae = &reg.ctx().emplace<krs::ee::AttachedEffectors>();
        ae->list.push_back(std::move(at));
    }

    // ---- the document-level pid allocator continues where the file left off (never reuse). ----
    {
        auto* alloc = reg.ctx().find<PidAllocator>();
        if (!alloc) alloc = &reg.ctx().emplace<PidAllocator>();
        alloc->next = std::max(alloc->next, u64FromJson(sc["nextPid"]));
        for (const auto& [pid, e] : pidMap) alloc->next = std::max(alloc->next, pid + 1);
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

    // ================================================================================
    // v1.2 RELATIONS chapter (E1.1): pids + constraints + nested groups + face roles +
    // loose-body connectors + effector attachments survive save -> WIPE -> load.
    // ================================================================================
    printf("[scenesave]   -- v1.2 RELATIONS: constraints/groups/roles/connectors/attachments through pid remap --\n");
    bool relSaveOk = false, relLoadOk = false, relPidOk = false, relConOk = false, relMotionOk = false,
         relGroupOk = false, relRoleOk = false, relConnOk = false, relAttOk = false,
         negDelOk = false, negPidOk = false, negOldOk = false;
    {
        const std::string relPath = QDir(dir).filePath("relations.kscene").toStdString();

        // Synthetic body-LOCAL identity faces (identical on each box -- keys are per-body).
        BRepFace cylF; cylF.type = 1; cylF.axisPos = { 0, 0, 0 }; cylF.axisDir = { 0, 0, 1 };
        cylF.radius = 0.05f; cylF.axisEnd0 = { 0, 0, -0.2f }; cylF.axisEnd1 = { 0, 0, 0.2f };
        cylF.faceKey = computeFaceKey(cylF);
        BRepFace plF; plF.type = 0; plF.normal = { 0, 0, 1 }; plF.axisPos = { 0, 0, 0.2f };
        plF.faceKey = computeFaceKey(plF);
        const std::uint64_t keyCyl = cylF.faceKey, keyPl = plF.faceKey;
        const auto cylAnchor = [&](entt::entity body) {
            krs::constraint::Anchor a;
            a.body = body; a.key = keyCyl; a.faceId = 0;
            a.pos = { 0, 0, 0 }; a.axisZ = { 0, 0, 1 }; a.axisX = { 1, 0, 0 };
            a.radius = 0.05f; a.from = krs::constraint::AnchorSource::CylFace;
            return a;
        };

        std::uint64_t pidA0 = 0, pidB0 = 0, pidC0 = 0, pidIn0 = 0, pidOut0 = 0, nextPid1 = 0, nextPid2 = 0;
        TransformComponent xfA0, xfB0, xfC0;

        // ---- author: demo robot + 3 keyed boxes + 2 constraints (1 kinematic) + nested groups
        //      + roles + a loose connector + an attachment; save. ----
        {
            Scene sA;
            auto& rA = sA.getRegistry();
            rA.ctx().emplace<RobotSourceRegistry>().set(0, "", /*builder demo*/ 0);
            krs::rbuild::RobotGraph rg = krs::rbuild::buildDemoGraph();
            rg.robotId = 0;
            krs::rbuild::spawnGraphBodies(sA, rg, 0);
            rA.ctx().emplace<krs::rbuild::RobotGraph>(rg);
            krs::robot::LiveRobot* rl = krs::robot::instantiateFromGraph(sA, rg, 0);
            if (rl) { rl->name = rl->model.name = "RelBot"; rl->useRobotFkViz = true; }

            const auto mkBox = [&](const char* nm, const glm::vec3& pos) {
                entt::entity e = SceneBuilder::spawnPrimitive(sA, int(Primitive::Cube), pos, { 0.4f, 0.4f, 0.4f }, nm);
                auto& fc = rA.emplace<BRepFaceComponent>(e);
                fc.faces = { cylF, plF };
                return e;
            };
            const entt::entity A = mkBox("BoxA", { 0, 0.5f, 0 });
            const entt::entity B = mkBox("BoxB", { 0.9f, 0.7f, 0.3f });
            const entt::entity C = mkBox("BoxC", { -0.8f, 0.4f, 0.6f });

            auto& cg = krs::constraint::constraintGraph(rA);
            krs::constraint::Constraint c1;
            c1.id = cg.nextId++; c1.type = krs::constraint::CType::Concentric;
            c1.a = cylAnchor(A); c1.b = cylAnchor(B);
            const bool snap1 = krs::constraint::applyConstraintSnap(rA, c1);   // satisfied at save
            cg.constraints.push_back(c1);
            krs::constraint::Constraint c2;
            c2.id = cg.nextId++; c2.type = krs::constraint::CType::Revolute;   // KINEMATIC type
            c2.a = cylAnchor(B); c2.b = cylAnchor(C);
            const bool snap2 = krs::constraint::applyConstraintSnap(rA, c2);
            cg.constraints.push_back(c2);

            auto& fr = rA.emplace<FaceRoleComponent>(A);                       // roles on 2 keys of A
            fr.byFaceKey[keyCyl] = { FaceRole::GripSurface, "vacuum-pad", 0.42f, 77.5f };
            fr.byFaceKey[keyPl]  = { FaceRole::KeepOut, "camera-window", 0.42f, 50.0f };

            auto& mcC = rA.emplace<MateConnectorComponent>(C);                 // connector id 7 on C
            MateConnector k;
            k.id = 7; k.name = "toolseat"; k.localPos = { 0.1f, 0.2f, 0.3f };
            k.localZ = { 0, 0, 1 }; k.localX = { 1, 0, 0 };
            k.sourceFaceKey = keyCyl; k.sourceFaceType = 1; k.radius = 0.05f;
            mcC.connectors.push_back(k);
            mcC.nextConnectorId = 8;

            const entt::entity inner = krs::group::makeGroup(rA, "inner", { A, B });   // {A,B}
            const entt::entity outer = krs::group::makeGroup(rA, "outer", { inner, C }); // {{A,B},C}

            auto& ae = rA.ctx().emplace<krs::ee::AttachedEffectors>();
            krs::ee::Attachment at;
            at.robotId = 0;
            at.flangeBody = entt::entity(std::uint32_t(rg.bodies[2].entity));  // a REAL robot member
            at.connectorId = 7;
            at.effectorName = "GateGripper";
            at.effectorBodies = { C };
            ae.list.push_back(at);

            xfA0 = rA.get<TransformComponent>(A);                              // pre-save poses
            xfB0 = rA.get<TransformComponent>(B);
            xfC0 = rA.get<TransformComponent>(C);

            const Report rs = saveScene(sA, relPath);
            const auto pidOfA = [&](entt::entity e) {
                return rA.all_of<PersistentIdComponent>(e) ? rA.get<PersistentIdComponent>(e).pid : 0ull;
            };
            pidA0 = pidOfA(A); pidB0 = pidOfA(B); pidC0 = pidOfA(C);
            pidIn0 = pidOfA(inner); pidOut0 = pidOfA(outer);
            {
                QJsonObject sj; readJsonFile(QString::fromStdString(relPath), sj);
                nextPid1 = u64FromJson(sj["nextPid"]);
            }
            relSaveOk = rs.ok && rs.robots == 1 && rs.objects == 3 && rs.warnings.isEmpty()
                     && snap1 && snap2
                     && pidA0 && pidB0 && pidC0 && pidIn0 && pidOut0
                     && pidA0 != pidB0 && pidA0 != pidC0 && pidB0 != pidC0
                     && nextPid1 == 6;
            printf("[scenesave]   v1.2 save: ok=%s robots=%d objects=%d snaps=%s/%s warnings=%d pids A/B/C/in/out=%llu/%llu/%llu/%llu/%llu nextPid=%llu (want 6)  %s\n",
                   rs.ok ? "yes" : "NO", rs.robots, rs.objects, snap1 ? "yes" : "NO", snap2 ? "yes" : "NO",
                   int(rs.warnings.size()),
                   (unsigned long long)pidA0, (unsigned long long)pidB0, (unsigned long long)pidC0,
                   (unsigned long long)pidIn0, (unsigned long long)pidOut0, (unsigned long long)nextPid1,
                   relSaveOk ? "PASS" : "FAIL");
        }

        // ---- WIPE (fresh Scene) -> load -> every relation intact, ZERO motion. ----
        {
            Scene sB;
            auto& rB = sB.getRegistry();
            const Report rl2 = loadScene(sB, relPath);

            const auto findObj = [&](const char* nm) -> entt::entity {
                for (auto e : rB.view<SceneObjectComponent, TagComponent>())
                    if (rB.get<TagComponent>(e).tag == nm) return e;
                return entt::null;
            };
            const auto findGrp = [&](const char* nm) -> entt::entity {
                for (auto e : rB.view<GroupComponent>())
                    if (rB.get<GroupComponent>(e).name == nm) return e;
                return entt::null;
            };
            const entt::entity A2 = findObj("BoxA"), B2 = findObj("BoxB"), C2 = findObj("BoxC");
            const entt::entity in2 = findGrp("inner"), out2 = findGrp("outer");
            relLoadOk = rl2.ok && rl2.warnings.isEmpty()
                     && A2 != entt::null && B2 != entt::null && C2 != entt::null
                     && in2 != entt::null && out2 != entt::null;

            // constraints: count 2, types + ids intact, anchors resolve on BOTH sides.
            int anchorsValid = 0;
            auto* cg2 = rB.ctx().find<krs::constraint::ConstraintGraphComponent>();
            if (relLoadOk && cg2 && cg2->constraints.size() == 2) {
                for (auto& c : cg2->constraints) {
                    if (krs::constraint::anchorWorldFrame(rB, c.a).valid) ++anchorsValid;
                    if (krs::constraint::anchorWorldFrame(rB, c.b).valid) ++anchorsValid;
                }
                relConOk = cg2->constraints[0].type == krs::constraint::CType::Concentric
                        && cg2->constraints[1].type == krs::constraint::CType::Revolute
                        && cg2->constraints[0].id == 1 && cg2->constraints[1].id == 2
                        && cg2->constraints[0].a.body == A2 && cg2->constraints[0].b.body == B2
                        && cg2->constraints[1].a.body == B2 && cg2->constraints[1].b.body == C2
                        && cg2->nextId == 3 && anchorsValid == 4;
            }

            // ZERO body motion during load (the saved poses already satisfy the constraints).
            double maxMove = 1e9; bool rotOk = false;
            if (relLoadOk) {
                const auto d = [&](entt::entity e, const TransformComponent& x0) {
                    const auto& x = rB.get<TransformComponent>(e);
                    return double(glm::length(x.translation - x0.translation))
                         + double(glm::length(x.scale - x0.scale));
                };
                maxMove = std::max({ d(A2, xfA0), d(B2, xfB0), d(C2, xfC0) });
                const auto rq = [&](entt::entity e, const TransformComponent& x0) {
                    return std::abs(glm::dot(rB.get<TransformComponent>(e).rotation, x0.rotation)) > 1.0f - 1e-6f;
                };
                rotOk = rq(A2, xfA0) && rq(B2, xfB0) && rq(C2, xfC0);
            }
            relMotionOk = relLoadOk && maxMove < 1e-6 && rotOk;
            printf("[scenesave]   v1.2 load: ok=%s warnings=%d constraints=%d/2 anchors valid=%d/4 nextId=%llu (want 3) maxMotion=%.3e (<1e-6) rot-exact=%s  %s\n",
                   rl2.ok ? "yes" : "NO", int(rl2.warnings.size()),
                   cg2 ? int(cg2->constraints.size()) : -1, anchorsValid,
                   cg2 ? (unsigned long long)cg2->nextId : 0ull,
                   maxMove, rotOk ? "yes" : "NO",
                   (relLoadOk && relConOk && relMotionOk) ? "PASS" : "FAIL");

            // roles exact (friction 0.42 on both painted keys of A).
            const auto* fr2 = (A2 != entt::null) ? rB.try_get<FaceRoleComponent>(A2) : nullptr;
            relRoleOk = fr2 && fr2->byFaceKey.size() == 2
                     && fr2->byFaceKey.count(keyCyl) && fr2->byFaceKey.count(keyPl)
                     && fr2->byFaceKey.at(keyCyl).role == FaceRole::GripSurface
                     && fr2->byFaceKey.at(keyCyl).tag == "vacuum-pad"
                     && std::abs(fr2->byFaceKey.at(keyCyl).friction - 0.42f) < 1e-7f
                     && std::abs(fr2->byFaceKey.at(keyCyl).maxNormalForceN - 77.5f) < 1e-4f
                     && fr2->byFaceKey.at(keyPl).role == FaceRole::KeepOut
                     && std::abs(fr2->byFaceKey.at(keyPl).friction - 0.42f) < 1e-7f;
            printf("[scenesave]   v1.2 roles: n=%d/2 friction=%.6f (want 0.420000) grip-tag=%s  %s\n",
                   fr2 ? int(fr2->byFaceKey.size()) : -1,
                   (fr2 && fr2->byFaceKey.count(keyCyl)) ? fr2->byFaceKey.at(keyCyl).friction : -1.0,
                   (fr2 && fr2->byFaceKey.count(keyCyl)) ? fr2->byFaceKey.at(keyCyl).tag.c_str() : "<none>",
                   relRoleOk ? "PASS" : "FAIL");

            // connector id 7 on C, frame exact.
            const auto* mc2 = (C2 != entt::null) ? rB.try_get<MateConnectorComponent>(C2) : nullptr;
            relConnOk = mc2 && mc2->connectors.size() == 1 && mc2->connectors[0].id == 7
                     && glm::length(mc2->connectors[0].localPos - glm::vec3(0.1f, 0.2f, 0.3f)) < 1e-7f
                     && glm::length(mc2->connectors[0].localZ - glm::vec3(0, 0, 1)) < 1e-7f
                     && glm::length(mc2->connectors[0].localX - glm::vec3(1, 0, 0)) < 1e-7f
                     && mc2->connectors[0].sourceFaceKey == keyCyl
                     && std::abs(mc2->connectors[0].radius - 0.05f) < 1e-7f
                     && mc2->nextConnectorId == 8;
            printf("[scenesave]   v1.2 connector: id=%u (want 7) pos=(%.3f,%.3f,%.3f) (want 0.1,0.2,0.3) nextId=%u (want 8)  %s\n",
                   (mc2 && !mc2->connectors.empty()) ? mc2->connectors[0].id : 0u,
                   (mc2 && !mc2->connectors.empty()) ? mc2->connectors[0].localPos.x : 0.0f,
                   (mc2 && !mc2->connectors.empty()) ? mc2->connectors[0].localPos.y : 0.0f,
                   (mc2 && !mc2->connectors.empty()) ? mc2->connectors[0].localPos.z : 0.0f,
                   mc2 ? mc2->nextConnectorId : 0u, relConnOk ? "PASS" : "FAIL");

            // attachment: robot-member flange resolved, connectorId intact, effector body == C.
            auto* ae2 = rB.ctx().find<krs::ee::AttachedEffectors>();
            const auto* g2 = rB.ctx().find<krs::rbuild::RobotGraph>();
            if (ae2 && ae2->list.size() == 1 && g2 && g2->bodies.size() > 2) {
                const auto& at2 = ae2->list[0];
                relAttOk = at2.robotId == 0 && at2.connectorId == 7 && at2.effectorName == "GateGripper"
                        && rB.valid(at2.flangeBody)
                        && at2.flangeBody == entt::entity(std::uint32_t(g2->bodies[2].entity))
                        && rB.all_of<RobotSubcomponentComponent>(at2.flangeBody)
                        && at2.effectorBodies.size() == 1 && at2.effectorBodies[0] == C2;
            }
            printf("[scenesave]   v1.2 attachment: entries=%d/1 robot=%d connectorId=%u (want 7) flange==graph-body2=%s effectorBody==BoxC=%s  %s\n",
                   ae2 ? int(ae2->list.size()) : -1,
                   (ae2 && !ae2->list.empty()) ? ae2->list[0].robotId : -1,
                   (ae2 && !ae2->list.empty()) ? ae2->list[0].connectorId : 0u,
                   (ae2 && !ae2->list.empty() && g2 && g2->bodies.size() > 2
                    && ae2->list[0].flangeBody == entt::entity(std::uint32_t(g2->bodies[2].entity))) ? "yes" : "NO",
                   (ae2 && !ae2->list.empty() && !ae2->list[0].effectorBodies.empty()
                    && ae2->list[0].effectorBodies[0] == C2) ? "yes" : "NO",
                   relAttOk ? "PASS" : "FAIL");

            // pid round-trip: a RE-SAVE reuses every pid (stable identity across saves).
            const Report rs2 = saveScene(sB, QDir(dir).filePath("relations2.kscene").toStdString());
            {
                QJsonObject sj; readJsonFile(QDir(dir).filePath("relations2.kscene"), sj);
                nextPid2 = u64FromJson(sj["nextPid"]);
            }
            const auto pidOfB = [&](entt::entity e) {
                return (e != entt::null && rB.all_of<PersistentIdComponent>(e))
                     ? rB.get<PersistentIdComponent>(e).pid : 0ull;
            };
            relPidOk = rs2.ok
                    && pidOfB(A2) == pidA0 && pidOfB(B2) == pidB0 && pidOfB(C2) == pidC0
                    && pidOfB(in2) == pidIn0 && pidOfB(out2) == pidOut0
                    && nextPid2 == nextPid1;
            printf("[scenesave]   v1.2 pids: stable across re-save=%s (A %llu->%llu B %llu->%llu C %llu->%llu) nextPid %llu->%llu  %s\n",
                   relPidOk ? "yes" : "NO",
                   (unsigned long long)pidA0, (unsigned long long)pidOfB(A2),
                   (unsigned long long)pidB0, (unsigned long long)pidOfB(B2),
                   (unsigned long long)pidC0, (unsigned long long)pidOfB(C2),
                   (unsigned long long)nextPid1, (unsigned long long)nextPid2,
                   relPidOk ? "PASS" : "FAIL");

            // nested topology identical + fan-out still works (move outer root, A follows).
            bool nestOk = false; float fanDx = 0.0f;
            if (relLoadOk) {
                nestOk = krs::group::topGroupOf(rB, A2) == out2
                      && rB.all_of<GroupMemberComponent>(A2) && rB.get<GroupMemberComponent>(A2).group == in2
                      && rB.all_of<GroupMemberComponent>(in2) && rB.get<GroupMemberComponent>(in2).group == out2
                      && rB.all_of<GroupMemberComponent>(C2) && rB.get<GroupMemberComponent>(C2).group == out2;
                const float ax0 = rB.get<TransformComponent>(A2).translation.x;
                rB.get<TransformComponent>(out2).translation.x += 0.5f;
                krs::group::applyRootDelta(rB, out2);
                fanDx = rB.get<TransformComponent>(A2).translation.x - ax0;
            }
            relGroupOk = nestOk && std::abs(fanDx - 0.5f) < 1e-5f;
            printf("[scenesave]   v1.2 groups: topGroupOf(A)==outer + A-in-inner + inner-in-outer + C-in-outer=%s fan-out dx=%.6f (want 0.500000)  %s\n",
                   nestOk ? "yes" : "NO", fanDx, relGroupOk ? "PASS" : "FAIL");
        }

        // ---- NEG-CTRL: hand-delete BoxB's object entry -> BOTH constraints drop, warned BY NAME,
        //      load stays ok, A/C land intact. ----
        {
            QJsonObject sj; readJsonFile(QString::fromStdString(relPath), sj);
            QJsonArray objs = sj["objects"].toArray();
            for (int i = 0; i < objs.size(); ++i)
                if (objs[i].toObject()["name"].toString() == QLatin1String("BoxB")) { objs.removeAt(i); break; }
            sj["objects"] = objs;
            const std::string p = QDir(dir).filePath("relations_noB.kscene").toStdString();
            writeJsonFile(QString::fromStdString(p), sj);
            Scene sC;
            auto& rC = sC.getRegistry();
            const Report rn = loadScene(sC, p);
            int dropped = 0; bool namedCon = false, namedRev = false;
            for (const QString& w : rn.warnings)
                if (w.contains(QLatin1String("constraint dropped"))) {
                    ++dropped;
                    if (w.contains(QLatin1String("Concentric"))) namedCon = true;
                    if (w.contains(QLatin1String("Revolute")))   namedRev = true;
                }
            auto* cgC = rC.ctx().find<krs::constraint::ConstraintGraphComponent>();
            entt::entity A3 = entt::null, C3 = entt::null;
            for (auto e : rC.view<SceneObjectComponent, TagComponent>()) {
                if (rC.get<TagComponent>(e).tag == "BoxA") A3 = e;
                if (rC.get<TagComponent>(e).tag == "BoxC") C3 = e;
            }
            const bool intact = A3 != entt::null && C3 != entt::null
                && glm::length(rC.get<TransformComponent>(A3).translation - xfA0.translation) < 1e-6f
                && glm::length(rC.get<TransformComponent>(C3).translation - xfC0.translation) < 1e-6f;
            negDelOk = rn.ok && dropped == 2 && namedCon && namedRev
                    && cgC && cgC->constraints.empty() && intact;
            printf("[scenesave]   NEG-CTRL deleted body: ok=%s dropped=%d/2 named(Concentric/Revolute)=%s/%s remaining=%d/0 A+C intact=%s  %s\n",
                   rn.ok ? "yes" : "NO", dropped, namedCon ? "yes" : "NO", namedRev ? "yes" : "NO",
                   cgC ? int(cgC->constraints.size()) : -1, intact ? "yes" : "NO",
                   negDelOk ? "REJECTS(non-vacuous)" : "VACUOUS!");
        }

        // ---- NEG-CTRL: corrupt one bodyPid to an unknown value -> ONLY that constraint drops. ----
        {
            QJsonObject sj; readJsonFile(QString::fromStdString(relPath), sj);
            QJsonObject cons = sj["constraints"].toObject();
            QJsonArray items = cons["items"].toArray();
            QJsonObject i0 = items[0].toObject();
            i0["bodyPidA"] = QStringLiteral("999999");
            items.replace(0, i0);
            cons["items"] = items; sj["constraints"] = cons;
            const std::string p = QDir(dir).filePath("relations_badpid.kscene").toStdString();
            writeJsonFile(QString::fromStdString(p), sj);
            Scene sD;
            auto& rD = sD.getRegistry();
            const Report rn = loadScene(sD, p);
            int dropped = 0; bool named999 = false;
            for (const QString& w : rn.warnings)
                if (w.contains(QLatin1String("constraint dropped"))) {
                    ++dropped;
                    if (w.contains(QLatin1String("999999"))) named999 = true;
                }
            auto* cgD = rD.ctx().find<krs::constraint::ConstraintGraphComponent>();
            negPidOk = rn.ok && dropped == 1 && named999
                    && cgD && cgD->constraints.size() == 1
                    && cgD->constraints[0].type == krs::constraint::CType::Revolute;
            printf("[scenesave]   NEG-CTRL corrupt pid: ok=%s dropped=%d/1 names-999999=%s survivor=%s (want Revolute)  %s\n",
                   rn.ok ? "yes" : "NO", dropped, named999 ? "yes" : "NO",
                   (cgD && cgD->constraints.size() == 1)
                       ? krs::constraint::cTypeName(cgD->constraints[0].type) : "<none>",
                   negPidOk ? "REJECTS(non-vacuous)" : "VACUOUS!");
        }

        // ---- NEG-CTRL: an OLD v1.1 scene (no new sections, no pids) loads with ZERO warnings and
        //      ZERO relations -- backwards compatibility is not allowed to invent anything. ----
        {
            QJsonObject old;
            old["format"] = QStringLiteral("kscene/1");
            old["name"] = QStringLiteral("v11");
            old["robots"] = QJsonArray{};
            QJsonObject obj;
            obj["primitive"] = int(Primitive::Cube); obj["mesh"] = QString();
            obj["name"] = QStringLiteral("OldBox");
            obj["transform"] = QJsonObject{ { "t", QJsonArray{ 1, 2, 3 } }, { "r", QJsonArray{ 1, 0, 0, 0 } },
                                            { "s", QJsonArray{ 1, 1, 1 } } };
            old["objects"] = QJsonArray{ obj };
            old["lights"] = QJsonArray{};
            const std::string p = QDir(dir).filePath("v11.kscene").toStdString();
            writeJsonFile(QString::fromStdString(p), old);
            Scene sE;
            auto& rE = sE.getRegistry();
            const Report rn = loadScene(sE, p);
            int loose = 0; entt::entity oldBox = entt::null;
            for (auto e : rE.view<SceneObjectComponent>()) { ++loose; oldBox = e; }
            int groupsN = 0; for (auto e : rE.view<GroupComponent>()) { (void)e; ++groupsN; }
            auto* cgE = rE.ctx().find<krs::constraint::ConstraintGraphComponent>();
            auto* aeE = rE.ctx().find<krs::ee::AttachedEffectors>();
            negOldOk = rn.ok && rn.warnings.isEmpty() && loose == 1 && groupsN == 0
                    && oldBox != entt::null && !rE.all_of<PersistentIdComponent>(oldBox)
                    && (!cgE || cgE->constraints.empty()) && (!aeE || aeE->list.empty());
            printf("[scenesave]   NEG-CTRL v1.1 scene: ok=%s warnings=%d (want 0) objects=%d/1 groups=%d/0 constraints=%d/0 attachments=%d/0 pid-free=%s  %s\n",
                   rn.ok ? "yes" : "NO", int(rn.warnings.size()), loose, groupsN,
                   cgE ? int(cgE->constraints.size()) : 0, aeE ? int(aeE->list.size()) : 0,
                   (oldBox != entt::null && !rE.all_of<PersistentIdComponent>(oldBox)) ? "yes" : "NO",
                   negOldOk ? "PASS" : "FAIL");
        }
    }

    const bool relPass = relSaveOk && relLoadOk && relPidOk && relConOk && relMotionOk
                      && relGroupOk && relRoleOk && relConnOk && relAttOk
                      && negDelOk && negPidOk && negOldOk;
    const bool pass = savedOk && envOk && propsOk && lightsOk && objectsOk && negOk && relPass;
    printf("[scenesave] %s\n", pass ? "ALL PASS (environment/lights/objects round-trip; v1.2 relations: pids stable across re-save, 2 constraints (1 kinematic) anchor-resolve with zero load motion, nested groups fan out, roles/connectors exact, attachment intact; dropped relations warn BY NAME; old 1.x scenes load clean)"
                                    : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::ksave
