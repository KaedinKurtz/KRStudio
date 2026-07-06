// KEE.cpp -- see KEE.hpp. The .kee end-effector document (krs::kee): a small
// RobotGraph + interface mate + TCPs + face semantic roles + actuation contract,
// one versioned JSON file with the ksave lockfile discipline (embedded content
// hash: detected, reported, never silently trusted).
//
// CODEC PROVENANCE: the graph path MIRRORS src/Utility/KSave.cpp field-for-field
// (the .krobot/.kjoint codec) -- jointToJson/jointFromJson are verbatim copies of
// KSave.cpp:103-137 (embedded joints are literal kjoint/1 objects), the body list
// mirrors the .krobot bodies array (KSave.cpp:387-392) extended with per-body
// placements (a .kee has no CAD-rebuild source, so its geometry frames must be
// self-contained), the counter re-bump mirrors KSave.cpp:694-699, and the
// connector codec mirrors the .krobot connectorSets path (KSave.cpp:400-411 /
// 709-720). One graph codec, two containers -- NOT a second invention. (The
// helpers live in KSave.cpp's anonymous namespace, so they are mirrored here
// rather than linked; any change there must be reflected here.)
#include "KEE.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace krs::kee {
namespace {

// ---- JSON idioms (mirror KSave.cpp:31-71, which mirror krs::persist) ---------------------------
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

bool formatOk(const QJsonObject& o, const char* family) {   // "kee/1" -> family match + major 1
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
bool readJsonFile(const QString& path, QJsonObject& out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) return false;
    out = doc.object();
    return true;
}

// CONTENT HASH (the ksave lockfile discipline, adapted to a SELF-CONTAINED file):
// ksave stores each referenced file's SHA1 in the REFERENCING document
// (KSave.cpp:371/422); a .kee has no referencing document, so the hash is
// EMBEDDED -- SHA1 over the canonical (Compact) serialization with the hash
// field removed. QJsonObject is key-sorted, so the bytes are deterministic.
QString canonicalHash(QJsonObject o) {          // by value: strips the hash field locally
    o.remove(QStringLiteral("contentHash"));
    return QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(o).toJson(QJsonDocument::Compact), QCryptographicHash::Sha1).toHex());
}

// ---- joint codec: VERBATIM mirror of KSave.cpp:103-137 (the .kjoint codec) ---------------------
// Embedded joints carry "format":"kjoint/1" exactly like a standalone .kjoint file,
// so a .kee's joints ARE kjoint objects inline -- the same schema, one code path.
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

// ---- graph codec: mirrors the .krobot master-dict path -----------------------------------------
// Scalars + bodies mirror KSave.cpp:383-392; the joints array embeds kjoint/1
// objects instead of {ref,id,contentHash} triples (self-contained file); bodies
// additionally persist their placement (mat4, KSave.cpp:37 idiom) -- the one
// .kee extension, justified in the header block.
QJsonObject graphToJson(const krs::rbuild::RobotGraph& g) {
    QJsonObject ro;
    ro["bodyCount"] = int(g.bodies.size());
    ro["base"] = g.base;
    ro["nextJointId"] = u64ToJson(g.nextJointId);
    ro["nextNodeId"]  = g.nextNodeId;
    QJsonArray bodies;
    for (const auto& b : g.bodies) {
        QJsonObject bo; bo["name"] = QString::fromStdString(b.name); bo["visSize"] = vec3ToJson(b.visSize);
        bo["placement"] = mat4ToJson(b.placement);
        bodies.push_back(bo);
    }
    ro["bodies"] = bodies;
    QJsonArray joints;
    for (const auto& j : g.joints) joints.push_back(jointToJson(j));
    ro["joints"] = joints;
    return ro;
}
bool graphFromJson(const QJsonObject& ro, krs::rbuild::RobotGraph& g, QString& why) {
    g = krs::rbuild::RobotGraph{};
    const int bodyCount = ro["bodyCount"].toInt(0);
    const QJsonArray bodies = ro["bodies"].toArray();
    if (bodyCount <= 0 || bodies.size() != bodyCount) {                 // strict-match-or-refuse
        why = QStringLiteral("graph body list (%1) does not match bodyCount (%2)")
                  .arg(bodies.size()).arg(bodyCount);
        return false;
    }
    for (const QJsonValue& bv : bodies) {
        const QJsonObject bo = bv.toObject();
        krs::rbuild::RBBody b;
        b.name = bo["name"].toString().toStdString();
        b.visSize = vec3FromJson(bo["visSize"], { 0.12f, 0.12f, 0.12f });
        b.placement = mat4FromJson(bo["placement"]);
        g.bodies.push_back(std::move(b));
    }
    g.base = std::clamp(ro["base"].toInt(0), 0, int(g.bodies.size()) - 1);   // KSave.cpp:692
    for (const QJsonValue& jv : ro["joints"].toArray()) {                    // the WHOLE overlay or nothing
        const QJsonObject jo = jv.toObject();
        krs::rbuild::RBJoint j;
        if (!jointFromJson(jo, bodyCount, j)) { why = QStringLiteral("an embedded kjoint object is malformed"); return false; }
        g.joints.push_back(std::move(j));                                   // preserve ids exactly (no re-mint)
    }
    // counter re-bump: KSave.cpp:694-699 verbatim discipline.
    g.nextJointId = std::max<std::uint64_t>(u64FromJson(ro["nextJointId"]), 1);
    g.nextNodeId  = std::max(ro["nextNodeId"].toInt(0), 0);
    for (const auto& j : g.joints) {
        g.nextJointId = std::max(g.nextJointId, j.id + 1);
        g.nextNodeId  = std::max(g.nextNodeId,  j.nodeId + 1);
    }
    return true;
}

// ---- connector codec: mirrors the .krobot connectorSets path (KSave.cpp:400-411 / 709-720) -----
// Keyed by graph BODY INDEX directly (a .kee has no live entities to (body,slot)-ref).
QJsonArray connectorsToJson(const std::map<int, std::vector<MateConnector>>& byBody) {
    QJsonArray sets;
    for (const auto& [body, list] : byBody) {
        QJsonObject o; o["body"] = body;
        QJsonArray arr;
        for (const auto& c : list) {
            QJsonObject co;
            co["id"] = int(c.id); co["name"] = QString::fromStdString(c.name);
            co["pos"] = vec3ToJson(c.localPos); co["z"] = vec3ToJson(c.localZ); co["x"] = vec3ToJson(c.localX);
            co["key"] = u64ToJson(c.sourceFaceKey); co["ftype"] = c.sourceFaceType; co["radius"] = double(c.radius);
            arr.push_back(co);
        }
        o["connectors"] = arr;
        sets.push_back(o);
    }
    return sets;
}
void connectorsFromJson(const QJsonArray& sets, std::map<int, std::vector<MateConnector>>& byBody) {
    byBody.clear();
    for (const QJsonValue& sv : sets) {
        const QJsonObject o = sv.toObject();
        const int body = o["body"].toInt(-1);
        if (body < 0) continue;
        auto& list = byBody[body];
        for (const QJsonValue& cv : o["connectors"].toArray()) {
            const QJsonObject co = cv.toObject();
            MateConnector c;
            c.id = std::uint32_t(co["id"].toInt(0)); c.name = co["name"].toString().toStdString();
            c.localPos = vec3FromJson(co["pos"]); c.localZ = vec3FromJson(co["z"], { 0, 0, 1 });
            c.localX = vec3FromJson(co["x"], { 1, 0, 0 });
            c.sourceFaceKey = u64FromJson(co["key"]); c.sourceFaceType = co["ftype"].toInt(1);
            c.radius = float(co["radius"].toDouble(0.0));
            list.push_back(std::move(c));
        }
    }
}

// ---- face-role codec (FaceRole.hpp; durable faceKey keys, u64-as-string like every faceKey) ----
QJsonArray rolesToJson(const std::map<int, std::unordered_map<std::uint64_t, FaceRoleEntry>>& byBody) {
    QJsonArray sets;
    for (const auto& [body, faces] : byBody) {
        QJsonObject o; o["body"] = body;
        QJsonArray arr;
        for (const auto& [key, e] : faces) {
            QJsonObject fo;
            fo["key"] = u64ToJson(key);
            fo["role"] = QString::fromLatin1(faceRoleName(e.role));       // canonical token
            fo["tag"] = QString::fromStdString(e.tag);
            fo["friction"] = double(e.friction);
            fo["maxNormalForceN"] = double(e.maxNormalForceN);
            arr.push_back(fo);
        }
        o["faces"] = arr;
        sets.push_back(o);
    }
    return sets;
}
void rolesFromJson(const QJsonArray& sets, std::map<int, std::unordered_map<std::uint64_t, FaceRoleEntry>>& byBody) {
    byBody.clear();
    for (const QJsonValue& sv : sets) {
        const QJsonObject o = sv.toObject();
        const int body = o["body"].toInt(-1);
        if (body < 0) continue;
        auto& faces = byBody[body];
        for (const QJsonValue& fv : o["faces"].toArray()) {
            const QJsonObject fo = fv.toObject();
            FaceRoleEntry e;
            e.role = faceRoleFromName(fo["role"].toString().toUtf8().constData());
            e.tag = fo["tag"].toString().toStdString();
            e.friction = float(fo["friction"].toDouble(0.8));
            e.maxNormalForceN = float(fo["maxNormalForceN"].toDouble(50.0));
            faces[u64FromJson(fo["key"])] = std::move(e);
        }
    }
}

// ---- TCP + actuation codecs ---------------------------------------------------------------------
QJsonArray tcpsToJson(const std::vector<TcpFrame>& tcps) {
    QJsonArray arr;
    for (const auto& t : tcps) {
        QJsonObject o;
        o["name"] = QString::fromStdString(t.name);
        o["pos"] = vec3ToJson(t.pos); o["z"] = vec3ToJson(t.z); o["x"] = vec3ToJson(t.x);
        arr.push_back(o);
    }
    return arr;
}
void tcpsFromJson(const QJsonArray& arr, std::vector<TcpFrame>& tcps) {
    tcps.clear();
    for (const QJsonValue& tv : arr) {
        const QJsonObject o = tv.toObject();
        TcpFrame t;
        t.name = o["name"].toString().toStdString();
        t.pos = vec3FromJson(o["pos"]); t.z = vec3FromJson(o["z"], { 0, 0, 1 }); t.x = vec3FromJson(o["x"], { 1, 0, 0 });
        tcps.push_back(std::move(t));
    }
}
QJsonObject actuationToJson(const Actuation& a) {
    QJsonObject o;
    o["driveJoint"] = QString::fromStdString(a.driveJointName);
    o["openQ"] = a.openQ; o["closedQ"] = a.closedQ;
    o["mimicRatio"] = a.mimicRatio; o["mimicJoint"] = QString::fromStdString(a.mimicJointName);
    o["maxGripForceN"] = a.maxGripForceN;
    return o;
}
void actuationFromJson(const QJsonObject& o, Actuation& a) {
    a.driveJointName = o["driveJoint"].toString().toStdString();
    a.openQ = o["openQ"].toDouble(0.0); a.closedQ = o["closedQ"].toDouble(0.0);
    a.mimicRatio = o["mimicRatio"].toDouble(-1.0);
    a.mimicJointName = o["mimicJoint"].toString().toStdString();
    a.maxGripForceN = o["maxGripForceN"].toDouble(40.0);
}

} // namespace

// ================================================================================================
// SAVE / LOAD
// ================================================================================================
Report saveKee(const EffectorDoc& doc, const std::string& path)
{
    Report rep;
    QJsonObject o;
    o["format"] = QStringLiteral("kee/1");
    o["name"] = doc.name;
    o["notes"] = doc.notes;
    o["interfaceMateId"] = int(doc.interfaceMateId);
    o["graph"] = graphToJson(doc.graph);
    o["tcps"] = tcpsToJson(doc.tcps);
    o["faceRoles"] = rolesToJson(doc.faceRolesByBody);
    o["connectors"] = connectorsToJson(doc.connectorsByBody);
    o["actuation"] = actuationToJson(doc.actuation);
    o["contentHash"] = canonicalHash(o);                       // over everything but the hash itself

    const QString qpath = QString::fromStdString(path);
    if (!writeJsonFile(qpath, o)) { rep.error = QStringLiteral("Cannot write %1").arg(qpath); return rep; }

    // Authoring an incomplete doc is legitimate -- validation failures are surfaced
    // as warnings at save, never as a refusal.
    for (const ValidationItem& it : validate(doc))
        if (!it.ok) rep.warnings << QStringLiteral("(validation) %1").arg(it.what);
    rep.ok = true;
    return rep;
}

Report loadKee(const std::string& path, EffectorDoc& out)
{
    Report rep;
    const QString qpath = QString::fromStdString(path);
    QJsonObject o;
    if (!readJsonFile(qpath, o)) {
        rep.error = QStringLiteral("Cannot read/parse %1").arg(qpath);
        return rep;
    }
    if (!formatOk(o, "kee")) {                                  // unknown major -> refuse the FILE
        rep.error = QStringLiteral("Unknown .kee format \"%1\" (this build reads kee/1).")
                        .arg(o["format"].toString());
        return rep;
    }
    // Lockfile discipline: a hash mismatch is DETECTED + REPORTED and the new
    // content is honored -- exactly ksave's "changed on disk" contract, never a
    // silent trust and never a silent refusal.
    const QString stored = o["contentHash"].toString();
    if (!stored.isEmpty() && stored != canonicalHash(o))
        rep.warnings << QStringLiteral("%1 changed on disk since it was authored (content hash mismatch) "
                                       "-- using the NEW content.").arg(qpath);
    else if (stored.isEmpty())
        rep.warnings << QStringLiteral("%1 carries no content hash (hand-authored?).").arg(qpath);

    out = EffectorDoc{};
    out.name = o["name"].toString();
    out.notes = o["notes"].toString();
    out.interfaceMateId = std::uint32_t(std::max(0, o["interfaceMateId"].toInt(0)));
    QString why;
    if (!graphFromJson(o["graph"].toObject(), out.graph, why)) {
        rep.error = QStringLiteral("%1: %2").arg(qpath, why);
        return rep;
    }
    tcpsFromJson(o["tcps"].toArray(), out.tcps);
    rolesFromJson(o["faceRoles"].toArray(), out.faceRolesByBody);
    connectorsFromJson(o["connectors"].toArray(), out.connectorsByBody);
    actuationFromJson(o["actuation"].toObject(), out.actuation);
    rep.ok = true;
    return rep;
}

// ================================================================================================
// VALIDATION -- is this document USABLE as an end-effector? Per-item pass/fail
// lines for UI display. Load parses; THIS judges (a structurally sound file with
// a dangling drive-joint name loads fine and fails here, for the right reason).
// ================================================================================================
std::vector<ValidationItem> validate(const EffectorDoc& d)
{
    std::vector<ValidationItem> items;
    auto add = [&](bool ok, const QString& what) { items.push_back({ ok, what }); };
    const krs::rbuild::RobotGraph& g = d.graph;
    const int nb = int(g.bodies.size());

    // 1) graph sanity
    const bool graphOk = nb > 0 && g.base >= 0 && g.base < nb;
    add(graphOk, graphOk
        ? QStringLiteral("graph: %1 bodies / %2 joints, base=%3").arg(nb).arg(int(g.joints.size())).arg(g.base)
        : QStringLiteral("graph: no bodies / invalid base -- not an effector"));

    // 2) interface mate: resolves to a connector on a BASE-COMPONENT body (the
    //    flange side must be rigid to the base, or mating it would tear the doc).
    {
        const std::set<int> baseComp = graphOk ? g.members() : std::set<int>{};
        int foundBody = -1; bool onBase = false;
        for (const auto& [body, list] : d.connectorsByBody)
            for (const auto& c : list)
                if (d.interfaceMateId != 0 && c.id == d.interfaceMateId) {
                    if (foundBody < 0 || baseComp.count(body)) { foundBody = body; onBase = baseComp.count(body) != 0; }
                }
        if (d.interfaceMateId == 0)
            add(false, QStringLiteral("interface mate: UNASSIGNED (id 0) -- cannot mate to a robot flange"));
        else if (foundBody < 0)
            add(false, QStringLiteral("interface mate: connector id %1 not found on any body").arg(d.interfaceMateId));
        else if (!onBase)
            add(false, QStringLiteral("interface mate: connector id %1 is on body %2, which is NOT in the base component")
                           .arg(d.interfaceMateId).arg(foundBody));
        else
            add(true, QStringLiteral("interface mate: connector id %1 on body %2 (base component)")
                          .arg(d.interfaceMateId).arg(foundBody));
    }

    // 3) drive joint resolves BY NAME to a drivable (non-Fixed) graph joint.
    auto findJoint = [&](const std::string& nm) {
        for (int i = 0; i < int(g.joints.size()); ++i) if (g.joints[i].name == nm) return i;
        return -1;
    };
    const bool passive = d.actuation.driveJointName.empty();
    const int di = passive ? -1 : findJoint(d.actuation.driveJointName);
    if (passive)
        add(true, QStringLiteral("drive joint: none (passive tool -- no actuation contract)"));
    else if (di < 0)
        add(false, QStringLiteral("drive joint \"%1\" does not resolve to any graph joint")
                       .arg(QString::fromStdString(d.actuation.driveJointName)));
    else if (g.joints[di].type == krs::rbuild::JType::Fixed)
        add(false, QStringLiteral("drive joint \"%1\" resolves but is Fixed -- not drivable")
                       .arg(QString::fromStdString(d.actuation.driveJointName)));
    else
        add(true, QStringLiteral("drive joint \"%1\" -> graph joint %2 (%3)")
                      .arg(QString::fromStdString(d.actuation.driveJointName)).arg(di)
                      .arg(g.joints[di].type == krs::rbuild::JType::Prismatic
                               ? QStringLiteral("prismatic") : QStringLiteral("revolute")));

    // 4) aperture endpoints within the drive joint's limits.
    if (passive)
        add(true, QStringLiteral("aperture: n/a (passive tool)"));
    else if (di < 0 || g.joints[di].type == krs::rbuild::JType::Fixed)
        add(false, QStringLiteral("aperture: cannot check -- drive joint unresolved"));
    else {
        const krs::rbuild::JointLimits& L = g.joints[di].limits;
        if (!L.enabled)
            add(true, QStringLiteral("aperture: drive joint is continuous (no position limits) -- "
                                     "openQ=%1 closedQ=%2 accepted").arg(d.actuation.openQ).arg(d.actuation.closedQ));
        else {
            const bool inRange = L.lower <= d.actuation.openQ && d.actuation.openQ <= L.upper
                              && L.lower <= d.actuation.closedQ && d.actuation.closedQ <= L.upper;
            add(inRange, inRange
                ? QStringLiteral("aperture: openQ=%1 closedQ=%2 within limits [%3, %4]")
                      .arg(d.actuation.openQ).arg(d.actuation.closedQ).arg(L.lower).arg(L.upper)
                : QStringLiteral("aperture: openQ=%1 / closedQ=%2 OUTSIDE drive limits [%3, %4]")
                      .arg(d.actuation.openQ).arg(d.actuation.closedQ).arg(L.lower).arg(L.upper));
        }
    }

    // 5) TCPs: >=1, each frame well-formed (tcps[0] is the default TCP).
    if (d.tcps.empty())
        add(false, QStringLiteral("TCPs: none -- at least one required (tcps[0] = default)"));
    else {
        bool frames = true;
        for (const TcpFrame& t : d.tcps) {
            const float lz = glm::length(t.z), lx = glm::length(t.x);
            if (lz < 1e-9f || lx < 1e-9f ||
                std::abs(glm::dot(t.z / lz, t.x / lx)) > 0.999f) { frames = false; break; }
        }
        add(frames, frames
            ? QStringLiteral("TCPs: %1 (default \"%2\")").arg(int(d.tcps.size()))
                  .arg(QString::fromStdString(d.tcps.front().name))
            : QStringLiteral("TCPs: a frame is degenerate (zero or near-parallel z/x)"));
    }

    // 6) grip surfaces -- WARNING ONLY: a doc with none is usable (a probe, a
    //    camera tool), but grasp planning will have no contact faces.
    {
        int nRoles = 0, nGrip = 0;
        for (const auto& [body, faces] : d.faceRolesByBody)
            for (const auto& [key, e] : faces) { ++nRoles; if (e.role == FaceRole::GripSurface) ++nGrip; }
        add(true, nGrip > 0
            ? QStringLiteral("face roles: %1 total, %2 GripSurface").arg(nRoles).arg(nGrip)
            : QStringLiteral("(warning) face roles: no GripSurface designated -- grasp planning has no contact faces"));
    }

    // 7) mimic joint resolves when named (and is a distinct, drivable joint).
    if (d.actuation.mimicJointName.empty())
        add(true, QStringLiteral("mimic: none (single-acting)"));
    else {
        const int mi = findJoint(d.actuation.mimicJointName);
        if (mi < 0)
            add(false, QStringLiteral("mimic joint \"%1\" does not resolve to any graph joint")
                           .arg(QString::fromStdString(d.actuation.mimicJointName)));
        else if (mi == di)
            add(false, QStringLiteral("mimic joint \"%1\" is the drive joint itself")
                           .arg(QString::fromStdString(d.actuation.mimicJointName)));
        else if (g.joints[mi].type == krs::rbuild::JType::Fixed)
            add(false, QStringLiteral("mimic joint \"%1\" resolves but is Fixed -- cannot follow")
                           .arg(QString::fromStdString(d.actuation.mimicJointName)));
        else
            add(true, QStringLiteral("mimic joint \"%1\" -> graph joint %2 (ratio %3)")
                          .arg(QString::fromStdString(d.actuation.mimicJointName)).arg(mi)
                          .arg(d.actuation.mimicRatio));
    }

    return items;
}

// ================================================================================================
// GATE KEE -- synthetic parallel gripper -> save -> load -> FIELD-EXACT round-trip;
// validate honest (all-ok on the good doc, the right item fails on each broken one);
// refusals clean (corrupt / wrong major); tampered content hash detected.
// ================================================================================================
namespace {

// The synthetic PARALLEL GRIPPER: base + 2 jaws, 1 prismatic drive + a mimic
// follower, interface connector on the base, 2 GripSurface pads + 1 KeepOut
// window, 2 TCPs (pinch point between the jaw tips = default).
EffectorDoc makeGateGripper()
{
    EffectorDoc d;
    d.name = QStringLiteral("gate_parallel_gripper");
    d.notes = QStringLiteral("synthetic 2-jaw parallel gripper authored by runKeeGate");

    // ---- bodies: base (0) + jaw_left (1) + jaw_right (2), placements base-relative metres.
    krs::rbuild::RBBody base;
    base.name = "gripper_base"; base.visSize = { 0.08f, 0.08f, 0.06f };
    base.placement = Eigen::Matrix4d::Identity();
    krs::rbuild::RBBody jawL;
    jawL.name = "jaw_left"; jawL.visSize = { 0.02f, 0.03f, 0.06f };
    jawL.placement = Eigen::Matrix4d::Identity(); jawL.placement(0, 3) = -0.03; jawL.placement(2, 3) = 0.08;
    krs::rbuild::RBBody jawR;
    jawR.name = "jaw_right"; jawR.visSize = { 0.02f, 0.03f, 0.06f };
    jawR.placement = Eigen::Matrix4d::Identity(); jawR.placement(0, 3) = 0.03; jawR.placement(2, 3) = 0.08;
    d.graph.bodies = { base, jawL, jawR };
    d.graph.base = 0;

    // ---- joints: prismatic drive base->jawL along +X, mimic follower base->jawR.
    krs::rbuild::RBJoint jd;
    jd.parent = 0; jd.child = 1; jd.type = krs::rbuild::JType::Prismatic;
    jd.axisPos = { -0.03f, 0.0f, 0.08f }; jd.axisDir = { 1, 0, 0 };
    jd.orthonormalizeFrame();
    jd.prov = krs::rbuild::Prov::Manual;
    jd.limits.lower = 0.0; jd.limits.upper = 0.04; jd.limits.effort = 120.0; jd.limits.velocity = 0.15;
    jd.name = "jaw_drive";
    d.graph.addJoint(jd);
    krs::rbuild::RBJoint jm;
    jm.parent = 0; jm.child = 2; jm.type = krs::rbuild::JType::Prismatic;
    jm.axisPos = { 0.03f, 0.0f, 0.08f }; jm.axisDir = { 1, 0, 0 };
    jm.orthonormalizeFrame();
    jm.prov = krs::rbuild::Prov::Manual;
    jm.limits.lower = -0.04; jm.limits.upper = 0.0; jm.limits.effort = 120.0; jm.limits.velocity = 0.15;
    jm.name = "jaw_follow";
    d.graph.addJoint(jm);

    // ---- interface connector ON THE BASE BODY (durable faceKey anchor, body-LOCAL frame).
    {
        BRepFace flange;                                    // the ISO-flange pilot bore
        flange.type = 1; flange.axisPos = { 0, 0, -0.03f }; flange.axisDir = { 0, 0, 1 };
        flange.radius = 0.025f; flange.axisEnd0 = { 0, 0, -0.03f }; flange.axisEnd1 = { 0, 0, -0.01f };
        flange.faceKey = computeFaceKey(flange);
        MateConnector c;
        c.id = 1; c.name = "flange_interface";
        c.localPos = { 0, 0, -0.03f }; c.localZ = { 0, 0, -1 }; c.localX = { 1, 0, 0 };
        c.sourceFaceKey = flange.faceKey; c.sourceFaceType = 1; c.radius = 0.025f;
        d.connectorsByBody[0].push_back(c);
        d.interfaceMateId = 1;
    }

    // ---- face roles: a GripSurface pad on each jaw (durable faceKey), a KeepOut on the base.
    {
        BRepFace padL; padL.type = 0; padL.normal = { 1, 0, 0 };  padL.axisPos = { -0.01f, 0, 0.10f };
        padL.faceKey = computeFaceKey(padL);
        BRepFace padR; padR.type = 0; padR.normal = { -1, 0, 0 }; padR.axisPos = { 0.01f, 0, 0.10f };
        padR.faceKey = computeFaceKey(padR);
        BRepFace cam;  cam.type = 0;  cam.normal = { 0, 1, 0 };   cam.axisPos = { 0, 0.04f, 0 };
        cam.faceKey = computeFaceKey(cam);
        d.faceRolesByBody[1][padL.faceKey] = { FaceRole::GripSurface, "rubber-pad", 0.9f, 60.0f };
        d.faceRolesByBody[2][padR.faceKey] = { FaceRole::GripSurface, "rubber-pad", 0.9f, 60.0f };
        d.faceRolesByBody[0][cam.faceKey]  = { FaceRole::KeepOut, "camera-window", 0.3f, 5.0f };
    }

    // ---- TCPs (BASE-BODY-LOCAL): the pinch point between the jaw tips = default.
    d.tcps.push_back({ "tcp_pinch",       { 0, 0, 0.135f }, { 0, 0, 1 },  { 1, 0, 0 } });
    d.tcps.push_back({ "tcp_tool_change", { 0, 0, -0.03f }, { 0, 0, -1 }, { 1, 0, 0 } });

    // ---- actuation contract: drive-by-name, aperture in joint space, mimic ratio -1.
    d.actuation.driveJointName = "jaw_drive";
    d.actuation.openQ = 0.04; d.actuation.closedQ = 0.002;
    d.actuation.mimicRatio = -1.0; d.actuation.mimicJointName = "jaw_follow";
    d.actuation.maxGripForceN = 45.0;
    return d;
}

// FIELD-EXACT document comparison: every serialized field, exact equality (the
// JSON layer stores doubles losslessly in Qt6; a mismatch names the field).
QStringList diffDocs(const EffectorDoc& A, const EffectorDoc& B)
{
    QStringList d;
    auto chk = [&](bool ok, const QString& what) { if (!ok) d << what; };
    chk(A.name == B.name, "name");
    chk(A.notes == B.notes, "notes");
    chk(A.interfaceMateId == B.interfaceMateId, "interfaceMateId");

    // graph scalars + bodies + joints
    chk(A.graph.base == B.graph.base, "graph.base");
    chk(A.graph.nextJointId == B.graph.nextJointId, "graph.nextJointId");
    chk(A.graph.nextNodeId == B.graph.nextNodeId, "graph.nextNodeId");
    chk(A.graph.bodies.size() == B.graph.bodies.size(), "graph.bodies.size");
    if (A.graph.bodies.size() == B.graph.bodies.size())
        for (size_t i = 0; i < A.graph.bodies.size(); ++i) {
            const auto& a = A.graph.bodies[i]; const auto& b = B.graph.bodies[i];
            chk(a.name == b.name, QStringLiteral("body[%1].name").arg(i));
            chk(a.visSize == b.visSize, QStringLiteral("body[%1].visSize").arg(i));
            chk((a.placement - b.placement).cwiseAbs().maxCoeff() == 0.0,
                QStringLiteral("body[%1].placement").arg(i));
        }
    chk(A.graph.joints.size() == B.graph.joints.size(), "graph.joints.size");
    if (A.graph.joints.size() == B.graph.joints.size())
        for (size_t i = 0; i < A.graph.joints.size(); ++i) {
            const auto& a = A.graph.joints[i]; const auto& b = B.graph.joints[i];
            chk(a.id == b.id && a.nodeId == b.nodeId && a.name == b.name, QStringLiteral("joint[%1].identity").arg(i));
            chk(a.type == b.type && a.parent == b.parent && a.child == b.child, QStringLiteral("joint[%1].topology").arg(i));
            chk(a.prov == b.prov && a.ambiguous == b.ambiguous && a.residual == b.residual, QStringLiteral("joint[%1].prov").arg(i));
            chk(a.axisPos == b.axisPos && a.axisDir == b.axisDir && a.refDir == b.refDir, QStringLiteral("joint[%1].frame").arg(i));
            chk(a.limits.lower == b.limits.lower && a.limits.upper == b.limits.upper
                && a.limits.effort == b.limits.effort && a.limits.velocity == b.limits.velocity
                && a.limits.enabled == b.limits.enabled, QStringLiteral("joint[%1].limits").arg(i));
            chk(a.actuatorRef == b.actuatorRef, QStringLiteral("joint[%1].actuatorRef").arg(i));
        }

    // TCPs
    chk(A.tcps.size() == B.tcps.size(), "tcps.size");
    if (A.tcps.size() == B.tcps.size())
        for (size_t i = 0; i < A.tcps.size(); ++i) {
            const auto& a = A.tcps[i]; const auto& b = B.tcps[i];
            chk(a.name == b.name && a.pos == b.pos && a.z == b.z && a.x == b.x,
                QStringLiteral("tcp[%1]").arg(i));
        }

    // face roles (incl. friction + force budget)
    chk(A.faceRolesByBody.size() == B.faceRolesByBody.size(), "faceRolesByBody.size");
    for (const auto& [body, faces] : A.faceRolesByBody) {
        const auto it = B.faceRolesByBody.find(body);
        if (it == B.faceRolesByBody.end()) { d << QStringLiteral("faceRoles body %1 missing").arg(body); continue; }
        chk(faces.size() == it->second.size(), QStringLiteral("faceRoles[%1].size").arg(body));
        for (const auto& [key, e] : faces) {
            const auto fit = it->second.find(key);
            if (fit == it->second.end()) { d << QStringLiteral("faceRoles[%1] key missing").arg(body); continue; }
            chk(e.role == fit->second.role && e.tag == fit->second.tag
                && e.friction == fit->second.friction && e.maxNormalForceN == fit->second.maxNormalForceN,
                QStringLiteral("faceRoles[%1][%2]").arg(body).arg(u64ToJson(key)));
        }
    }

    // connectors
    chk(A.connectorsByBody.size() == B.connectorsByBody.size(), "connectorsByBody.size");
    for (const auto& [body, list] : A.connectorsByBody) {
        const auto it = B.connectorsByBody.find(body);
        if (it == B.connectorsByBody.end()) { d << QStringLiteral("connectors body %1 missing").arg(body); continue; }
        chk(list.size() == it->second.size(), QStringLiteral("connectors[%1].size").arg(body));
        if (list.size() == it->second.size())
            for (size_t i = 0; i < list.size(); ++i) {
                const auto& a = list[i]; const auto& b = it->second[i];
                chk(a.id == b.id && a.name == b.name && a.localPos == b.localPos && a.localZ == b.localZ
                    && a.localX == b.localX && a.sourceFaceKey == b.sourceFaceKey
                    && a.sourceFaceType == b.sourceFaceType && a.radius == b.radius,
                    QStringLiteral("connectors[%1][%2]").arg(body).arg(i));
            }
    }

    // actuation
    chk(A.actuation.driveJointName == B.actuation.driveJointName
        && A.actuation.openQ == B.actuation.openQ && A.actuation.closedQ == B.actuation.closedQ
        && A.actuation.mimicRatio == B.actuation.mimicRatio
        && A.actuation.mimicJointName == B.actuation.mimicJointName
        && A.actuation.maxGripForceN == B.actuation.maxGripForceN, "actuation");
    return d;
}

// The failing items of a validation run whose text contains `needle` (case-insensitive).
int failingItemsContaining(const std::vector<ValidationItem>& items, const char* needle)
{
    int n = 0;
    for (const auto& it : items)
        if (!it.ok && it.what.contains(QLatin1String(needle), Qt::CaseInsensitive)) ++n;
    return n;
}

} // namespace

bool runKeeGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[kee] GATE KEE -- .kee end-effector document: field-exact round-trip; validation honest; refusals clean\n");
    const QString dir = QDir::temp().filePath("krs_kee_gate");
    QDir(dir).removeRecursively();
    const std::string keePath = QDir(dir).filePath("gate_gripper.kee").toStdString();

    // ---- author the synthetic parallel gripper + validate it (all items must pass) ----
    const EffectorDoc doc = makeGateGripper();
    const auto vGood = validate(doc);
    const bool authOk = validationOk(vGood) && vGood.size() == 7;
    printf("[kee]   authored: %d bodies / %d joints (prismatic drive + mimic), interface mate id %u, %d TCPs, 3 face roles\n",
           int(doc.graph.bodies.size()), int(doc.graph.joints.size()), doc.interfaceMateId, int(doc.tcps.size()));
    for (const auto& it : vGood)
        printf("[kee]     validate: %s  %s\n", it.ok ? "ok " : "FAIL", it.what.toUtf8().constData());
    printf("[kee]   validate(authored): %d items all-ok=%s  %s\n",
           int(vGood.size()), validationOk(vGood) ? "yes" : "NO", authOk ? "PASS" : "FAIL");

    // ---- save -> load -> FIELD-EXACT round-trip ----
    const Report sr = saveKee(doc, keePath);
    const bool savedOk = sr.ok && sr.warnings.isEmpty() && QFile::exists(QString::fromStdString(keePath));
    printf("[kee]   save: ok=%s warnings=%d file present=%s  %s\n",
           sr.ok ? "yes" : "NO", int(sr.warnings.size()),
           QFile::exists(QString::fromStdString(keePath)) ? "yes" : "NO", savedOk ? "PASS" : "FAIL");

    EffectorDoc back;
    const Report lr = loadKee(keePath, back);
    const QStringList mismatches = diffDocs(doc, back);
    const bool loadOk = lr.ok && lr.warnings.isEmpty() && mismatches.isEmpty() && validationOk(validate(back));
    for (const QString& m : mismatches)
        printf("[kee]     MISMATCH: %s\n", m.toUtf8().constData());
    printf("[kee]   load: ok=%s field-exact mismatches=%d validate(loaded) all-ok=%s  %s\n",
           lr.ok ? "yes" : "NO", int(mismatches.size()),
           validationOk(validate(back)) ? "yes" : "NO", loadOk ? "PASS" : "FAIL");

    // ---- NEG-CTRL 1: missing interface mate FAILS VALIDATION (not load) ----
    bool negMate = false;
    {
        EffectorDoc bad = doc; bad.interfaceMateId = 999;
        const std::string p = QDir(dir).filePath("neg_mate.kee").toStdString();
        saveKee(bad, p);                                    // save succeeds (authoring incomplete docs is legal)
        EffectorDoc rt;
        const Report r = loadKee(p, rt);                    // load succeeds -- validation is the judge
        const auto v = validate(rt);
        negMate = r.ok && !validationOk(v) && failingItemsContaining(v, "interface mate") == 1;
        printf("[kee]   NEG missing interface mate: load ok=%s validation fails(interface item)=%s  %s\n",
               r.ok ? "yes" : "NO", (!validationOk(v) && failingItemsContaining(v, "interface mate") == 1) ? "yes" : "NO",
               negMate ? "REJECTS(non-vacuous)" : "VACUOUS!");
    }

    // ---- NEG-CTRL 2: driveJointName typo FAILS validation ----
    bool negDrive = false;
    {
        EffectorDoc bad = doc; bad.actuation.driveJointName = "jaw_drve";   // typo
        const auto v = validate(bad);
        // "does not resolve" isolates the DRIVE item (the aperture item also names the
        // drive joint in its cannot-check text, so the "drive joint" needle would hit both).
        negDrive = !validationOk(v) && failingItemsContaining(v, "does not resolve") == 1
                && failingItemsContaining(v, "aperture") == 1;              // aperture honestly un-checkable
        printf("[kee]   NEG drive-name typo: drive item fails=%s aperture reports unresolvable=%s  %s\n",
               failingItemsContaining(v, "does not resolve") == 1 ? "yes" : "NO",
               failingItemsContaining(v, "aperture") == 1 ? "yes" : "NO",
               negDrive ? "REJECTS(non-vacuous)" : "VACUOUS!");
    }

    // ---- NEG-CTRL 3: openQ outside the drive joint's limits FAILS validation ----
    bool negAperture = false;
    {
        EffectorDoc bad = doc; bad.actuation.openQ = 0.2;                   // limits are [0, 0.04]
        const auto v = validate(bad);
        negAperture = !validationOk(v) && failingItemsContaining(v, "aperture") == 1;
        printf("[kee]   NEG openQ outside limits: aperture item fails=%s  %s\n",
               failingItemsContaining(v, "aperture") == 1 ? "yes" : "NO",
               negAperture ? "REJECTS(non-vacuous)" : "VACUOUS!");
    }

    // ---- NEG-CTRL 4: corrupt file -> load ok=false, no crash ----
    bool negCorrupt = false;
    {
        const QString p = QDir(dir).filePath("corrupt.kee");
        QFile f(p); f.open(QIODevice::WriteOnly | QIODevice::Truncate); f.write("{ not json", 10); f.close();
        EffectorDoc rt;
        const Report r = loadKee(p.toStdString(), rt);
        negCorrupt = !r.ok && !r.error.isEmpty();
        printf("[kee]   NEG corrupt file: refused=%s (no crash)  %s\n",
               negCorrupt ? "yes" : "NO", negCorrupt ? "REJECTS(non-vacuous)" : "VACUOUS!");
    }

    // ---- NEG-CTRL 5: wrong format major refused ----
    bool negMajor = false;
    {
        QJsonObject o; readJsonFile(QString::fromStdString(keePath), o);
        o["format"] = QStringLiteral("kee/2");
        const QString p = QDir(dir).filePath("major2.kee");
        writeJsonFile(p, o);
        EffectorDoc rt;
        const Report r = loadKee(p.toStdString(), rt);
        negMajor = !r.ok && r.error.contains(QLatin1String("format"));
        printf("[kee]   NEG wrong major (kee/2): refused=%s  %s\n",
               negMajor ? "yes" : "NO", negMajor ? "REJECTS(non-vacuous)" : "VACUOUS!");
    }

    // ---- TAMPER: hand-edit a field on disk, hash left stale -> DETECTED (warning) + honored ----
    bool tamperOk = false;
    {
        QJsonObject o; readJsonFile(QString::fromStdString(keePath), o);
        QJsonObject act = o["actuation"].toObject(); act["maxGripForceN"] = 9999.0; o["actuation"] = act;
        const QString p = QDir(dir).filePath("tampered.kee");
        writeJsonFile(p, o);                                 // contentHash now stale
        EffectorDoc rt;
        const Report r = loadKee(p.toStdString(), rt);
        bool warned = false;
        for (const QString& w : r.warnings) if (w.contains(QLatin1String("content hash"))) warned = true;
        tamperOk = r.ok && warned && rt.actuation.maxGripForceN == 9999.0;
        printf("[kee]   tamper: hand-edited maxGripForceN detected(warning)=%s new value live=%s  %s\n",
               warned ? "yes" : "NO", rt.actuation.maxGripForceN == 9999.0 ? "yes" : "NO",
               tamperOk ? "PASS" : "FAIL");
    }

    const bool pass = authOk && savedOk && loadOk
                   && negMate && negDrive && negAperture && negCorrupt && negMajor && tamperOk;
    printf("[kee] %s\n", pass
        ? "ALL PASS (parallel-gripper .kee round-trips field-exact (graph joints/limits, mate id, TCPs, roles incl. friction, actuation); "
          "validation honest (missing mate / drive typo / out-of-limits aperture fail for the right reason); "
          "corrupt + wrong-major refused; tampered content hash detected)"
        : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::kee
