// ===========================================================================
// KDoc.cpp -- see KDoc.hpp. The Node <-> JSON codec both .kgraph and .knode
// build on: a tagged std::any round-trip over the verified closed type union,
// with the load-bearing restore order (params -> selectNamedOption -> literals
// -> policy).
// ===========================================================================
#include "KDoc.hpp"
#include "Node.hpp"
#include "NodeFactory.hpp"

#include <QJsonArray>
#include <QString>
#include <glm/glm.hpp>

#include <any>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace krs::kdoc {
namespace {

// --- tagged std::any encoder over the VERIFIED closed union ------------------------------------
// {double,float,int,long long,bool,std::string,glm::vec3}. 64-bit ints are STRING-encoded (QJson
// numbers are doubles -> lossy above 2^53). Unknown stored type -> t="?" (decode fails loud).
QJsonObject encodeAny(const std::any& a) {
    QJsonObject o;
    if (auto* p = std::any_cast<double>(&a))            { o["t"] = "d";  o["v"] = *p; }
    else if (auto* p = std::any_cast<float>(&a))        { o["t"] = "f";  o["v"] = double(*p); }
    else if (auto* p = std::any_cast<int>(&a))          { o["t"] = "i";  o["v"] = *p; }
    else if (auto* p = std::any_cast<long long>(&a))    { o["t"] = "l";  o["v"] = QString::number(*p); }
    else if (auto* p = std::any_cast<bool>(&a))         { o["t"] = "b";  o["v"] = *p; }
    else if (auto* p = std::any_cast<std::string>(&a))  { o["t"] = "s";  o["v"] = QString::fromStdString(*p); }
    else if (auto* p = std::any_cast<glm::vec3>(&a))    { o["t"] = "v3"; o["v"] = QJsonArray{ double(p->x), double(p->y), double(p->z) }; }
    else                                                { o["t"] = "?"; }
    return o;
}

// Decode a {t,v} object and drive it onto the node -- either a param or an input-port literal (both
// go through the node's own templated setter so coercion + storage match the live authoring path).
bool decodeInto(Node& n, const QJsonObject& e, bool isLiteral, const std::string& key, QString* err) {
    const QString t = e["t"].toString();
    const QJsonValue v = e["v"];
    auto set = [&](auto typedValue) {
        if (isLiteral) n.setPortLiteral(key, typedValue);
        else           n.setParam(key, typedValue);
    };
    if      (t == "d") set(double(v.toDouble()));
    else if (t == "f") set(float(v.toDouble()));
    else if (t == "i") set(int(v.toInt()));
    else if (t == "l") set(static_cast<long long>(v.toString().toLongLong()));
    else if (t == "b") set(bool(v.toBool()));
    else if (t == "s") set(std::string(v.toString().toStdString()));
    else if (t == "v3") {
        const QJsonArray a = v.toArray();
        if (a.size() != 3) { if (err) *err = QStringLiteral("bad vec3 for '%1'").arg(QString::fromStdString(key)); return false; }
        set(glm::vec3(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble())));
    } else {
        if (err) *err = QStringLiteral("unknown type tag '%1' for '%2'").arg(t, QString::fromStdString(key));
        return false;   // fail loud
    }
    return true;
}

bool hasPort(const Node& n, const char* name) {
    for (const auto& p : n.getPorts()) if (p.name == name) return true;
    return false;
}

} // namespace

QJsonObject nodeToJson(const Node& n, const std::string& typeId) {
    QJsonObject o;
    o["typeId"] = QString::fromStdString(typeId);
    o["id"]     = QString::fromStdString(n.getId());

    QJsonArray params;
    for (const auto& kv : n.params()) {
        QJsonObject e = encodeAny(kv.second);
        e["k"] = QString::fromStdString(kv.first);
        params.push_back(e);
    }
    o["params"] = params;

    QJsonArray literals;
    for (const auto& p : n.getPorts()) {
        if (p.direction != Port::Direction::Input || !p.literalValue.has_value()) continue;
        QJsonObject e = encodeAny(p.literalValue->data);
        e["k"] = QString::fromStdString(p.name);
        literals.push_back(e);
    }
    o["literals"] = literals;

    const std::string named = n.namedOption();
    if (!named.empty()) o["namedOption"] = QString::fromStdString(named);

    o["policy"] = int(n.getUpdatePolicy());
    o["edge"]   = int(n.getTriggerEdge());
    return o;
}

bool applyJsonToNode(Node& n, const QJsonObject& o, QString* err) {
    // 1. params first (a reconfigurable node reads its selector param when we drive selectNamedOption).
    for (const QJsonValue& pv : o["params"].toArray()) {
        const QJsonObject e = pv.toObject();
        if (!decodeInto(n, e, /*isLiteral*/false, e["k"].toString().toStdString(), err)) return false;
    }
    // 2. rebuild the port set from the saved selection BEFORE literals -- so the target ports exist.
    if (o.contains("namedOption")) n.selectNamedOption(o["namedOption"].toString().toStdString());
    // 3. port literals, addressed by name (survive port reorder).
    for (const QJsonValue& lv : o["literals"].toArray()) {
        const QJsonObject e = lv.toObject();
        if (!decodeInto(n, e, /*isLiteral*/true, e["k"].toString().toStdString(), err)) return false;
    }
    // 4. update policy.
    if (o.contains("policy")) n.setUpdatePolicy(Node::UpdatePolicy(o["policy"].toInt()));
    if (o.contains("edge"))   n.setTriggerEdge(Node::TriggerEdge(o["edge"].toInt()));
    return true;
}

std::string typeIdOf(const QJsonObject& o) { return o["typeId"].toString().toStdString(); }

// ================================================================================================
// GATE KDOC
// ================================================================================================
bool runKDocGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[kdoc] GATE KDOC -- Node<->JSON codec: tagged std::any round-trip + reconfigure-order restore\n");
    bool allOk = true;
    auto& F = NodeFactory::instance();

    // ---- (1) gen_sine: double params round-trip bit-equal; non-vacuous (fresh default != restored) ----
    {
        auto a = F.createNode("gen_sine");
        if (a) {
            a->setParam<double>("freq", 3.5);  a->setParam<double>("amp", 2.0);
            a->setParam<double>("phase", 0.7); a->setParam<double>("offset", -1.25);
            const QJsonObject j = nodeToJson(*a, "gen_sine");
            auto b = F.createNode("gen_sine");
            const bool freshDiffers = std::abs(b->getParam<double>("freq", 0.0) - 3.5) > 1e-9;   // fresh default 1.0
            QString err; const bool applied = applyJsonToNode(*b, j, &err);
            const bool eq = applied
                && std::abs(b->getParam<double>("freq", 0.0) - 3.5)   < 1e-12
                && std::abs(b->getParam<double>("amp", 0.0)  - 2.0)   < 1e-12
                && std::abs(b->getParam<double>("phase", 0.0)- 0.7)   < 1e-12
                && std::abs(b->getParam<double>("offset", 0.0)+ 1.25) < 1e-12;
            const bool ok = eq && freshDiffers;
            printf("[kdoc]   gen_sine double params: restored-bit-equal=%d non-vacuous(fresh!=restored)=%d  %s\n",
                   int(eq), int(freshDiffers), ok ? "PASS" : "FAIL");
            allOk = allOk && ok;
        } else { printf("[kdoc]   gen_sine: createNode null -- FAIL\n"); allOk = false; }
    }

    // ---- (2) physics_articulation_drive: int port-literal 'Joint' round-trips ----
    {
        auto a = F.createNode("physics_articulation_drive");
        if (a) {
            a->setPortLiteral<int>("Joint", 4);
            const QJsonObject j = nodeToJson(*a, "physics_articulation_drive");
            auto b = F.createNode("physics_articulation_drive");
            QString err; const bool applied = applyJsonToNode(*b, j, &err);
            const int got = b->getInput<int>("Joint").value_or(-999);
            const bool ok = applied && got == 4;
            printf("[kdoc]   physics_articulation_drive int literal 'Joint': restored=%d (want 4)  %s\n",
                   got, ok ? "PASS" : "FAIL");
            allOk = allOk && ok;
        } else { printf("[kdoc]   physics_articulation_drive: createNode null -- FAIL\n"); allOk = false; }
    }

    // ---- (3) glm::vec3 param + int64 precision (proves string-encoding) ----
    {
        auto a = F.createNode("gen_sine");
        if (a) {
            a->setParam<glm::vec3>("testVec", glm::vec3(1.5f, -2.5f, 3.5f));
            a->setParam<long long>("testBig", 9007199254740993LL);   // 2^53 + 1
            const QJsonObject j = nodeToJson(*a, "gen_sine");
            auto b = F.createNode("gen_sine");
            QString err; const bool applied = applyJsonToNode(*b, j, &err);
            const glm::vec3 gv = b->getParam<glm::vec3>("testVec", glm::vec3(0));
            const long long gl = b->getParam<long long>("testBig", 0LL);
            const bool vecOk = glm::length(gv - glm::vec3(1.5f, -2.5f, 3.5f)) < 1e-6f;
            const bool bigOk = (gl == 9007199254740993LL);   // a double round-trip would give ...992
            const bool ok = applied && vecOk && bigOk;
            printf("[kdoc]   vec3 param + int64 precision: vec3-exact=%d int64-exact(%lld==2^53+1)=%d  %s\n",
                   int(vecOk), gl, int(bigOk), ok ? "PASS" : "FAIL");
            allOk = allOk && ok;
        } else { printf("[kdoc]   gen_sine(#3): createNode null -- FAIL\n"); allOk = false; }
    }

    // ---- (4) twin_property: selectNamedOption rebuilds ports; restored on load BEFORE literals ----
    {
        auto a = F.createNode("twin_property");
        if (a) {
            a->selectNamedOption("orientation");                 // Quat layout -> a "Quaternion" output appears
            const bool aHasQuat = hasPort(*a, "Quaternion");
            const QJsonObject j = nodeToJson(*a, "twin_property");
            auto b = F.createNode("twin_property");
            const bool freshHasQuat = hasPort(*b, "Quaternion"); // default Vec3 layout -> no Quaternion
            QString err; const bool applied = applyJsonToNode(*b, j, &err);
            const bool restoredHasQuat = hasPort(*b, "Quaternion");
            const bool ok = applied && aHasQuat && !freshHasQuat && restoredHasQuat;
            printf("[kdoc]   twin_property reconfigure: saved-has-Quaternion=%d fresh-lacks=%d restored-has=%d  %s\n",
                   int(aHasQuat), int(!freshHasQuat), int(restoredHasQuat), ok ? "PASS" : "FAIL");
            allOk = allOk && ok;
        } else { printf("[kdoc]   twin_property: createNode null -- FAIL\n"); allOk = false; }
    }

    // ---- NEG-CTRL: an unknown type tag FAILS LOUD (not silently defaulted) ----
    {
        auto b = F.createNode("gen_sine");
        QJsonObject bad;
        QJsonObject badParam; badParam["k"] = "freq"; badParam["t"] = "?"; badParam["v"] = 3.0;
        bad["params"] = QJsonArray{ badParam };
        QString err; const bool applied = b ? applyJsonToNode(*b, bad, &err) : true;
        const bool negOk = !applied;
        printf("[kdoc]   NEG-CTRL unknown type tag rejected=%d (\"%s\")  %s\n",
               int(negOk), err.toUtf8().constData(), negOk ? "REJECTS(non-vacuous)" : "VACUOUS!");
        allOk = allOk && negOk;
    }

    printf("[kdoc] %s\n", allOk ? "ALL PASS (tagged std::any round-trip over the closed union; int64 string-encoded; reconfigure selection restored before literals; unknown-type fail-loud)"
                                : "FAILURES PRESENT");
    std::fflush(stdout);
    return allOk;
}

} // namespace krs::kdoc
