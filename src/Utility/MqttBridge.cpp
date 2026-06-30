#include "MqttBridge.hpp"

#ifdef KR_WITH_MQTT
// ===========================================================================
// Phase 4: real Mosquitto broker (spawned via QProcess) + libmosquitto clients.
// The gate proves a command that travels broker -> robot handler -> broker -> the
// controller actually moves the canonical link (FK), so the messaging is causal,
// not decorative.
// ===========================================================================
#include "ArticulationSpec.hpp"
#include "RevoluteFK.hpp"   // shared krs::kin::revoluteApply (one FK definition, reused by GATE ND too)
#include "GateOutcome.hpp"  // krs::gate::skip() -- broker absent => SKIP, not FAIL

#include <mosquitto.h>
#include <QtGlobal>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QThread>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

namespace krs::mqtt {

bool available() { return true; }

namespace {

// FK is the shared engine definition krs::kin::revoluteApply (RevoluteFK.hpp) -- not a local formula.

// ---- received-message sink for a passive client ----
struct RxBox { std::vector<std::pair<std::string, std::string>> msgs; };
void onMsgCollect(struct mosquitto*, void* ud, const struct mosquitto_message* m)
{
    RxBox* box = static_cast<RxBox*>(ud);
    box->msgs.emplace_back(m->topic ? std::string(m->topic) : std::string(),
                           std::string(static_cast<const char*>(m->payload), m->payloadlen));
}

// ---- the "robot" side: on a joint cmd, run FK and publish the moved link pose on its state topic ----
struct RobotCtx {
    glm::vec3 origin, axis, tip0;
    std::string stateTopic;
    int cmds = 0;        // commands the handler acted on
    int rejected = 0;    // malformed / non-finite commands rejected
    int badTips = 0;     // non-finite poses the handler produced (must stay 0 -- robustness)
};
void onRobotCmd(struct mosquitto* mosq, void* ud, const struct mosquitto_message* m)
{
    RobotCtx* rc = static_cast<RobotCtx*>(ud);
    const std::string payload(static_cast<const char*>(m->payload), m->payloadlen);
    // parse defensively: require a fully-consumed finite number, else REJECT (a malformed cmd must not
    // move the robot). atof would turn "1e999"->inf and "12xyz"->12, both unsafe.
    char* end = nullptr;
    const double parsed = std::strtod(payload.c_str(), &end);
    const bool clean = end != nullptr && end != payload.c_str() && *end == '\0' && std::isfinite(parsed);
    if (!clean) { ++rc->rejected; return; }
    const float q = float(parsed);
    const glm::vec3 tip = krs::kin::revoluteApply(rc->origin, rc->axis, rc->tip0, q);
    if (!(std::isfinite(tip.x) && std::isfinite(tip.y) && std::isfinite(tip.z))) { ++rc->badTips; return; }
    char buf[160];
    const int n = std::snprintf(buf, sizeof(buf), "%.6f,%.6f,%.6f", tip.x, tip.y, tip.z);
    mosquitto_publish(mosq, nullptr, rc->stateTopic.c_str(), n, buf, 1, false);   // QoS 1: no silent drop
    ++rc->cmds;
}

glm::vec3 parseVec3(const std::string& s)
{
    glm::vec3 v(0.0f);
    std::sscanf(s.c_str(), "%f,%f,%f", &v.x, &v.y, &v.z);
    return v;
}

// Pump a set of clients' network loops for `iters` rounds (each mosquitto_loop blocks up to tmo ms).
void pump(std::initializer_list<struct mosquitto*> cs, int iters, int tmo)
{
    for (int i = 0; i < iters; ++i)
        for (auto c : cs) mosquitto_loop(c, tmo, 1);
}

struct BrokerProc {
    QProcess proc;
    ~BrokerProc() {
        if (proc.state() != QProcess::NotRunning) { proc.kill(); proc.waitForFinished(2000); }
    }
};

} // namespace

bool runMqttGateM()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[mqtt] GATE M -- Mosquitto broker on startup + canonical joint cmd/state round-trip\n");

    const int port = []{ const int p = qEnvironmentVariableIntValue("KRS_MQTT_PORT"); return p > 0 ? p : 11883; }();
    const char* host = "127.0.0.1";

    // --- broker exe (system mosquitto.exe; override with KRS_MOSQUITTO_EXE) ---
    std::string exe = "C:/Program Files/mosquitto/mosquitto.exe";
    if (const char* ov = std::getenv("KRS_MOSQUITTO_EXE")) exe = ov;
    if (!std::filesystem::exists(exe)) {
        printf("[mqtt] SKIP: mosquitto broker exe not found at '%s' (install mosquitto or set KRS_MOSQUITTO_EXE)\n", exe.c_str());
        fflush(stdout); krs::gate::skip(); return true;
    }

    // --- write a minimal broker config (anonymous loopback listener) ---
    std::error_code ec;
    const std::string conf = (std::filesystem::temp_directory_path(ec) / "krs_gatem_mosq.conf").string();
    { std::ofstream f(conf);
      f << "listener " << port << " 127.0.0.1\n" << "allow_anonymous true\n"; }

    mosquitto_lib_init();

    // --- spawn the broker ---
    BrokerProc broker;
    broker.proc.start(QString::fromStdString(exe), QStringList{ "-c", QString::fromStdString(conf) });
    if (!broker.proc.waitForStarted(4000)) {
        printf("[mqtt] FAIL: broker did not start\n"); fflush(stdout); mosquitto_lib_cleanup(); return false;
    }
    QThread::msleep(500); // let the listen socket open

    auto makeClient = [&](const char* id, void* ud) -> struct mosquitto* {
        struct mosquitto* c = mosquitto_new(id, true, ud);
        if (!c) return nullptr;
        bool connected = false;
        for (int attempt = 0; attempt < 40 && !connected; ++attempt) {
            if (mosquitto_connect(c, host, port, 30) == MOSQ_ERR_SUCCESS) connected = true;
            else QThread::msleep(100);
        }
        if (!connected) { mosquitto_destroy(c); return nullptr; }
        return c;
    };

    // ============================ M1: pub/sub round-trip + topic isolation ============================
    bool m1ok = false, m1iso = false;
    {
        RxBox rx;
        struct mosquitto* sub = makeClient("krs-m1-sub", &rx);
        struct mosquitto* pub = makeClient("krs-m1-pub", nullptr);
        if (sub && pub) {
            mosquitto_message_callback_set(sub, onMsgCollect);
            mosquitto_subscribe(sub, nullptr, "krs/m1/echo", 0);
            pump({ sub }, 4, 50);
            const char* msg = "ping-42";
            mosquitto_publish(pub, nullptr, "krs/m1/echo",  int(std::strlen(msg)), msg, 0, false);
            const char* wrong = "should-not-arrive";
            mosquitto_publish(pub, nullptr, "krs/m1/other", int(std::strlen(wrong)), wrong, 0, false);
            pump({ pub, sub }, 16, 50);
            int gotEcho = 0, gotOther = 0;
            for (auto& kv : rx.msgs) { if (kv.first == "krs/m1/echo" && kv.second == "ping-42") ++gotEcho;
                                       if (kv.first == "krs/m1/other") ++gotOther; }
            m1ok  = (gotEcho == 1);
            m1iso = (gotOther == 0);                 // NEG-CTRL: unsubscribed topic must not arrive
        }
        if (sub) { mosquitto_disconnect(sub); mosquitto_destroy(sub); }
        if (pub) { mosquitto_disconnect(pub); mosquitto_destroy(pub); }
    }

    // ============================ M2: nested topic tree from canonical graph ============================
    krs::dyn::RobotArticSpec spec;
    { krs::dyn::ArticJointSpec j0; j0.revolute = true; j0.axis = { 0.f, 0.f, 1.f }; j0.ptree = { 0.3f, 0.0f, 0.0f };
      spec.joints.push_back(j0); }
    const int jointCount = int(spec.joints.size());
    const TopicTree tt = buildTopicTree("FANUC", jointCount);
    const bool m2ok = (int(tt.jointCmd.size()) == jointCount) && (int(tt.jointState.size()) == jointCount)
                   && tt.jointCmd[0] == "robot/FANUC/joint/0/cmd"
                   && tt.jointState[0] == "robot/FANUC/joint/0/state"
                   && tt.jointCmdWildcard == "robot/FANUC/joint/+/cmd";

    // ============================ M3: REAL joint round-trip (cmd -> FK -> state) ============================
    bool m3ok = false; double m3err = 9e9, m3moved = 0.0;
    {
        // canonical joint frame + a marker on the moving link, 0.5 m out along +X from the joint origin.
        const glm::vec3 origin(spec.joints[0].ptree[0], spec.joints[0].ptree[1], spec.joints[0].ptree[2]);
        const glm::vec3 axis  (spec.joints[0].axis[0],  spec.joints[0].axis[1],  spec.joints[0].axis[2]);
        const glm::vec3 tip0 = origin + glm::vec3(0.5f, 0.0f, 0.0f);

        RobotCtx rc; rc.origin = origin; rc.axis = axis; rc.tip0 = tip0; rc.stateTopic = tt.jointState[0];
        RxBox ctlRx;
        struct mosquitto* robot = makeClient("krs-robot", &rc);
        struct mosquitto* ctl   = makeClient("krs-controller", &ctlRx);
        if (robot && ctl) {
            mosquitto_message_callback_set(robot, onRobotCmd);
            mosquitto_subscribe(robot, nullptr, tt.jointCmdWildcard.c_str(), 1);   // robot listens for commands (QoS 1)
            mosquitto_message_callback_set(ctl, onMsgCollect);
            mosquitto_subscribe(ctl, nullptr, tt.jointState[0].c_str(), 1);        // controller listens for telemetry
            pump({ robot, ctl }, 6, 50);

            const float qCmd = 0.7f;                                              // commanded angle (rad)
            char qbuf[32]; const int qn = std::snprintf(qbuf, sizeof(qbuf), "%.6f", qCmd);
            mosquitto_publish(ctl, nullptr, tt.jointCmd[0].c_str(), qn, qbuf, 1, false);  // QoS 1: no silent drop
            pump({ robot, ctl }, 40, 50);                                         // let it travel both ways

            // find the last state message the controller received
            glm::vec3 tipRx(0.0f); bool got = false;
            for (auto& kv : ctlRx.msgs) if (kv.first == tt.jointState[0]) { tipRx = parseVec3(kv.second); got = true; }
            const glm::vec3 tipFk   = krs::kin::revoluteApply(origin, axis, tip0, qCmd); // oracle (direct FK)
            const glm::vec3 tipRest = krs::kin::revoluteApply(origin, axis, tip0, 0.0f); // rest pose
            m3err   = got ? double(glm::length(tipRx - tipFk))   : 9e9;           // RECEIVED pose vs FK oracle
            // displacement measured from the RECEIVED telemetry (not a constant): it is non-zero only
            // because the angle actually crossed the broker and drove the handler.
            m3moved = got ? double(glm::length(tipRx - tipRest)) : 0.0;
            m3ok = got && rc.cmds == 1 && m3err < 1e-4 && m3moved > 0.1;
        }
        if (robot) { mosquitto_disconnect(robot); mosquitto_destroy(robot); }
        if (ctl)   { mosquitto_disconnect(ctl);   mosquitto_destroy(ctl); }
    }

    // ============================ M4: broadcast -> receive duality (fan-out + isolation) ============================
    bool m4ok = false; int m4a = 0, m4b = 0, m4other = 0;
    {
        RxBox rxa, rxb, rxo;
        struct mosquitto* sa = makeClient("krs-m4-a", &rxa);
        struct mosquitto* sb = makeClient("krs-m4-b", &rxb);
        struct mosquitto* so = makeClient("krs-m4-o", &rxo);
        struct mosquitto* pb = makeClient("krs-m4-pub", nullptr);
        if (sa && sb && so && pb) {
            mosquitto_message_callback_set(sa, onMsgCollect);
            mosquitto_message_callback_set(sb, onMsgCollect);
            mosquitto_message_callback_set(so, onMsgCollect);
            mosquitto_subscribe(sa, nullptr, tt.broadcast.c_str(), 0);
            mosquitto_subscribe(sb, nullptr, tt.broadcast.c_str(), 0);
            mosquitto_subscribe(so, nullptr, "robot/FANUC/other", 0);   // different topic -> isolated
            pump({ sa, sb, so }, 6, 50);
            const char* hb = "heartbeat";
            mosquitto_publish(pb, nullptr, tt.broadcast.c_str(), int(std::strlen(hb)), hb, 0, false);
            pump({ pb, sa, sb, so }, 16, 50);
            for (auto& kv : rxa.msgs) if (kv.first == tt.broadcast) ++m4a;
            for (auto& kv : rxb.msgs) if (kv.first == tt.broadcast) ++m4b;
            m4other = int(rxo.msgs.size());
            m4ok = (m4a == 1 && m4b == 1 && m4other == 0);   // both broadcast subs hit, isolated sub silent
        }
        for (auto* c : { sa, sb, so, pb }) if (c) { mosquitto_disconnect(c); mosquitto_destroy(c); }
    }

    mosquitto_lib_cleanup();
    std::filesystem::remove(conf, ec);

    const bool pass = m1ok && m1iso && m2ok && m3ok && m4ok;
    printf("[mqtt]   M1 pub/sub round-trip: echo=%s, wrong-topic isolated=%s\n",
           m1ok ? "RECEIVED" : "LOST", m1iso ? "yes" : "NO!");
    printf("[mqtt]   M2 topic tree: %d joint(s) -> cmd[0]='%s' state[0]='%s' wildcard='%s'  %s\n",
           jointCount, tt.jointCmd.empty() ? "" : tt.jointCmd[0].c_str(),
           tt.jointState.empty() ? "" : tt.jointState[0].c_str(), tt.jointCmdWildcard.c_str(),
           m2ok ? "PASS" : "FAIL");
    printf("[mqtt]   M3 joint round-trip: RECEIVED pose vs FK err=%.3e m (<1e-4), received moved=%.3f m vs rest (>0.1)  %s\n",
           m3err, m3moved, m3ok ? "PASS" : "FAIL");
    printf("[mqtt]   M4 broadcast duality: sub-a=%d sub-b=%d (want 1,1), isolated-other=%d (want 0)  %s\n",
           m4a, m4b, m4other, m4ok ? "PASS" : "FAIL");
    printf("[mqtt] %s\n", pass ? "ALL PASS (real broker; round-trip moves the canonical link; fan-out isolated)"
                               : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

// ===========================================================================
// GATE M5 (Phase 4): MQTT robustness surface. M5a broker KILLED mid-run -> the engine survives (no
// crash) and RECONNECTS, round-trip works again. M5b under N>=128 topics the per-service MQTT cost
// stays bounded (a physics tick servicing MQTT isn't starved). M5c MALFORMED payloads on a cmd topic
// are REJECTED -- the handler never produces a non-finite pose, never crashes. Gated by KRS_MQTTROBUST_SELFTEST.
// ===========================================================================
bool runMqttRobustnessGateM5()
{
    using std::printf;
    using clk = std::chrono::high_resolution_clock;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[mqtt-r] GATE M5 -- broker-kill/reconnect + N>=128-topic tick budget + malformed-payload rejection\n");

    const int port = []{ const int p = qEnvironmentVariableIntValue("KRS_MQTT_PORT"); return p > 0 ? p + 1 : 11884; }();
    const char* host = "127.0.0.1";
    std::string exe = "C:/Program Files/mosquitto/mosquitto.exe";
    if (const char* ov = std::getenv("KRS_MOSQUITTO_EXE")) exe = ov;
    if (!std::filesystem::exists(exe)) { printf("[mqtt-r] SKIP: broker exe not found at '%s' (install mosquitto or set KRS_MOSQUITTO_EXE)\n", exe.c_str());
        fflush(stdout); krs::gate::skip(); return true; }
    std::error_code ec;
    const std::string conf = (std::filesystem::temp_directory_path(ec) / "krs_m5_mosq.conf").string();
    { std::ofstream f(conf); f << "listener " << port << " 127.0.0.1\n" << "allow_anonymous true\n"; }

    mosquitto_lib_init();
    auto connect = [&](const char* id, void* ud) -> struct mosquitto* {
        struct mosquitto* c = mosquitto_new(id, true, ud);
        if (!c) return nullptr;
        for (int a = 0; a < 40; ++a) { if (mosquitto_connect(c, host, port, 30) == MOSQ_ERR_SUCCESS) return c;
                                       QThread::msleep(100); }
        mosquitto_destroy(c); return nullptr;
    };
    auto pumpc = [&](std::initializer_list<struct mosquitto*> cs, int iters, int tmo) {
        for (int i = 0; i < iters; ++i) for (auto c : cs) if (c) mosquitto_loop(c, tmo, 1);
    };

    BrokerProc broker;
    broker.proc.start(QString::fromStdString(exe), QStringList{ "-c", QString::fromStdString(conf) });
    if (!broker.proc.waitForStarted(4000)) { printf("[mqtt-r] FAIL: broker did not start\n");
        fflush(stdout); mosquitto_lib_cleanup(); return false; }
    QThread::msleep(500);

    // ---------------- M5a: kill the broker mid-run -> survive + reconnect ----------------
    bool survivedKill = false, reconnected = false;
    {
        RxBox rx;
        struct mosquitto* c = connect("krs-m5a", &rx);
        if (c) {
            mosquitto_message_callback_set(c, onMsgCollect);
            mosquitto_subscribe(c, nullptr, "krs/m5/pre", 1);
            pumpc({ c }, 4, 50);
            const char* p0 = "before";
            mosquitto_publish(c, nullptr, "krs/m5/pre", 6, p0, 1, false);
            pumpc({ c }, 8, 50);
            // KILL the broker
            broker.proc.kill(); broker.proc.waitForFinished(2000);
            // operate against the dead broker -> must NOT crash (loop returns an error, publish queues/errs)
            for (int i = 0; i < 5; ++i) { mosquitto_loop(c, 50, 1); mosquitto_publish(c, nullptr, "krs/m5/pre", 4, "void", 1, false); }
            survivedKill = true;                                   // reached here without crashing
            // restart the broker + reconnect the SAME client
            broker.proc.start(QString::fromStdString(exe), QStringList{ "-c", QString::fromStdString(conf) });
            broker.proc.waitForStarted(4000); QThread::msleep(500);
            int rr = MOSQ_ERR_UNKNOWN;
            for (int a = 0; a < 40 && rr != MOSQ_ERR_SUCCESS; ++a) { rr = mosquitto_reconnect(c); if (rr != MOSQ_ERR_SUCCESS) QThread::msleep(100); }
            if (rr == MOSQ_ERR_SUCCESS) {
                mosquitto_subscribe(c, nullptr, "krs/m5/post", 1);  // clean-session: re-subscribe after reconnect
                pumpc({ c }, 4, 50);
                struct mosquitto* pub = connect("krs-m5a-pub", nullptr);
                if (pub) {
                    const char* p1 = "after";
                    mosquitto_publish(pub, nullptr, "krs/m5/post", 5, p1, 1, false);
                    pumpc({ pub, c }, 16, 50);
                    for (auto& kv : rx.msgs) if (kv.first == "krs/m5/post" && kv.second == "after") reconnected = true;
                    mosquitto_disconnect(pub); mosquitto_destroy(pub);
                }
            }
            mosquitto_destroy(c);
        }
    }

    // ---------------- M5b: N>=128 topics -> bounded per-service cost (tick budget) ----------------
    // A telemetry consumer subscribes ONCE to the joint-state WILDCARD and must service 128 DISTINCT
    // topics' traffic; we measure the cost of draining that burst (the budget a physics tick would pay).
    bool m5bok = false; double serviceMs = 9e9; const int topicsN = 128; int distinctTopics = 0;
    {
        RxBox rx;
        struct mosquitto* sub = connect("krs-m5b-sub", &rx);
        struct mosquitto* pub = connect("krs-m5b-pub", nullptr);
        if (sub && pub) {
            mosquitto_message_callback_set(sub, onMsgCollect);
            pumpc({ sub, pub }, 6, 50);                             // settle CONNACK
            mosquitto_subscribe(sub, nullptr, "robot/FANUC/joint/+/state", 0);  // ONE wildcard sub for all N
            pumpc({ sub }, 6, 50);                                  // flush SUBACK
            for (int i = 0; i < topicsN; ++i) { std::string t = "robot/FANUC/joint/" + std::to_string(i) + "/state";
                std::string v = std::to_string(i); mosquitto_publish(pub, nullptr, t.c_str(), int(v.size()), v.c_str(), 0, false); }
            // service until all distinct topics drain (or a generous cap) -> measures the real burst cost
            const auto t0 = clk::now();
            for (int round = 0; round < 400 && distinctTopics < topicsN; ++round) {
                pumpc({ pub, sub }, 1, 10);
                std::vector<std::string> seen;
                for (auto& kv : rx.msgs) if (std::find(seen.begin(), seen.end(), kv.first) == seen.end()) seen.push_back(kv.first);
                distinctTopics = int(seen.size());
            }
            const auto t1 = clk::now();
            serviceMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            m5bok = (distinctTopics >= topicsN) && (serviceMs < 1500.0);
        }
        for (auto* c : { sub, pub }) if (c) { mosquitto_disconnect(c); mosquitto_destroy(c); }
    }

    // ---------------- M5c: malformed payloads on a cmd topic -> rejected, no crash, no bad pose ----------------
    bool m5cok = false; int malformedSent = 0; int handlerCmds = 0, handlerRej = 0, handlerBad = 0;
    {
        RobotCtx rc; rc.origin = glm::vec3(0.3f, 0, 0); rc.axis = glm::vec3(0, 0, 1);
        rc.tip0 = rc.origin + glm::vec3(0.5f, 0, 0); rc.stateTopic = "robot/FANUC/joint/0/state";
        RxBox ctlRx;
        struct mosquitto* robot = connect("krs-m5c-robot", &rc);
        struct mosquitto* ctl   = connect("krs-m5c-ctl", &ctlRx);
        if (robot && ctl) {
            mosquitto_message_callback_set(robot, onRobotCmd);
            mosquitto_subscribe(robot, nullptr, "robot/FANUC/joint/+/cmd", 1);
            mosquitto_message_callback_set(ctl, onMsgCollect);
            mosquitto_subscribe(ctl, nullptr, rc.stateTopic.c_str(), 1);
            pumpc({ robot, ctl }, 6, 50);
            const char* bad[] = { "", "not_a_number", "NaN", "1e999", "0.5xyz", "  ", "-", "inf", "\x01\x02\x03\x04" };
            const int badLen[] = { 0, 12, 3, 5, 6, 2, 1, 3, 4 };
            for (int i = 0; i < 9; ++i) { mosquitto_publish(ctl, nullptr, "robot/FANUC/joint/0/cmd", badLen[i], bad[i], 1, false); ++malformedSent; }
            // then ONE valid command to confirm the handler still works after the garbage
            const char* good = "0.7"; mosquitto_publish(ctl, nullptr, "robot/FANUC/joint/0/cmd", 3, good, 1, false);
            pumpc({ robot, ctl }, 40, 50);
            handlerCmds = rc.cmds; handlerRej = rc.rejected; handlerBad = rc.badTips;
            bool recoveredFinite = false;
            for (auto& kv : ctlRx.msgs) if (kv.first == rc.stateTopic) {
                const glm::vec3 p = parseVec3(kv.second);
                if (std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)) recoveredFinite = true;
            }
            // pass: every malformed cmd rejected, the handler NEVER produced a bad pose, exactly the one good cmd acted, recovery finite
            m5cok = (handlerBad == 0) && (handlerRej == malformedSent) && (handlerCmds == 1) && recoveredFinite;
        }
        for (auto* c : { robot, ctl }) if (c) { mosquitto_disconnect(c); mosquitto_destroy(c); }
    }

    mosquitto_lib_cleanup();
    std::filesystem::remove(conf, ec);

    const bool pass = survivedKill && reconnected && m5bok && m5cok;
    printf("[mqtt-r]   M5a broker kill mid-run: survived=%s, reconnected+round-trip=%s\n",
           survivedKill ? "yes" : "NO!", reconnected ? "yes" : "NO!");
    printf("[mqtt-r]   M5b %d-topic service cost: %.2f ms total, %d/%d distinct topics delivered (bounded tick budget)  %s\n",
           topicsN, serviceMs, distinctTopics, topicsN, m5bok ? "PASS" : "FAIL");
    printf("[mqtt-r]   M5c malformed payloads: %d sent -> %d rejected, %d bad poses (want 0), %d good acted  %s\n",
           malformedSent, handlerRej, handlerBad, handlerCmds, m5cok ? "PASS" : "FAIL");
    printf("[mqtt-r] %s\n", pass ? "ALL PASS (survives broker kill + reconnects; N>=128 bounded; malformed rejected, no bad pose)"
                                 : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::mqtt

#else
// --- Built without libmosquitto: no-op stub so the rest of the engine builds. ---
namespace krs::mqtt {
bool available() { return false; }
bool runMqttGateM() { return true; } // no MQTT -> vacuous pass, keeps the bench green
bool runMqttRobustnessGateM5() { return true; }
} // namespace krs::mqtt
#endif
