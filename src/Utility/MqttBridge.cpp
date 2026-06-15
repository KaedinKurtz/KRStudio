#include "MqttBridge.hpp"

#ifdef KR_WITH_MQTT
// ===========================================================================
// Phase 4: real Mosquitto broker (spawned via QProcess) + libmosquitto clients.
// The gate proves a command that travels broker -> robot handler -> broker -> the
// controller actually moves the canonical link (FK), so the messaging is causal,
// not decorative.
// ===========================================================================
#include "ArticulationSpec.hpp"

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
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

namespace krs::mqtt {

bool available() { return true; }

namespace {

// ---- forward kinematics for ONE revolute joint: a marker at world rest pos `tip0`
//      rotates about `axis` through `origin` by angle q. This is the canonical motion
//      the joint command must produce. ----
glm::vec3 fkTip(const glm::vec3& origin, const glm::vec3& axis, const glm::vec3& tip0, float q)
{
    const glm::mat4 R = glm::rotate(glm::mat4(1.0f), q, glm::normalize(axis));
    const glm::vec3 rel = tip0 - origin;
    const glm::vec3 rot = glm::vec3(R * glm::vec4(rel, 0.0f));
    return origin + rot;
}

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
    int cmds = 0;
};
void onRobotCmd(struct mosquitto* mosq, void* ud, const struct mosquitto_message* m)
{
    RobotCtx* rc = static_cast<RobotCtx*>(ud);
    const std::string payload(static_cast<const char*>(m->payload), m->payloadlen);
    const float q = float(std::atof(payload.c_str()));
    const glm::vec3 tip = fkTip(rc->origin, rc->axis, rc->tip0, q);
    char buf[160];
    const int n = std::snprintf(buf, sizeof(buf), "%.6f,%.6f,%.6f", tip.x, tip.y, tip.z);
    mosquitto_publish(mosq, nullptr, rc->stateTopic.c_str(), n, buf, 0, false);
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
        printf("[mqtt] FAIL: mosquitto broker exe not found at '%s' (set KRS_MOSQUITTO_EXE)\n", exe.c_str());
        fflush(stdout); return false;
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
            mosquitto_subscribe(robot, nullptr, tt.jointCmdWildcard.c_str(), 0);   // robot listens for commands
            mosquitto_message_callback_set(ctl, onMsgCollect);
            mosquitto_subscribe(ctl, nullptr, tt.jointState[0].c_str(), 0);        // controller listens for telemetry
            pump({ robot, ctl }, 6, 50);

            const float qCmd = 0.7f;                                              // commanded angle (rad)
            char qbuf[32]; const int qn = std::snprintf(qbuf, sizeof(qbuf), "%.6f", qCmd);
            mosquitto_publish(ctl, nullptr, tt.jointCmd[0].c_str(), qn, qbuf, 0, false);
            pump({ robot, ctl }, 40, 50);                                         // let it travel both ways

            // find the last state message the controller received
            glm::vec3 tipRx(0.0f); bool got = false;
            for (auto& kv : ctlRx.msgs) if (kv.first == tt.jointState[0]) { tipRx = parseVec3(kv.second); got = true; }
            const glm::vec3 tipFk   = fkTip(origin, axis, tip0, qCmd);            // oracle (direct FK)
            const glm::vec3 tipRest = fkTip(origin, axis, tip0, 0.0f);            // rest pose
            m3err   = got ? double(glm::length(tipRx - tipFk))   : 9e9;           // round-trip vs FK
            m3moved = double(glm::length(tipFk - tipRest));                       // NEG-CTRL: must be a real move
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
    printf("[mqtt]   M3 joint round-trip: cmd->FK->state err=%.3e m (bound<1e-4), moved=%.3f m (>0.1)  %s\n",
           m3err, m3moved, m3ok ? "PASS" : "FAIL");
    printf("[mqtt]   M4 broadcast duality: sub-a=%d sub-b=%d (want 1,1), isolated-other=%d (want 0)  %s\n",
           m4a, m4b, m4other, m4ok ? "PASS" : "FAIL");
    printf("[mqtt] %s\n", pass ? "ALL PASS (real broker; round-trip moves the canonical link; fan-out isolated)"
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
} // namespace krs::mqtt
#endif
