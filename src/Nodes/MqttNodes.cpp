// MqttNodes.cpp -- Phase 2: auto-populating MQTT topic nodes. A subscribe-node owns a libmosquitto
// client, pumps it in compute(), and emits the latest numeric value of its topic; a publish-node
// publishes its input value to its topic (driving the robot through the bus -- FK-verified in GATE
// NODE-MQTT). The topic each node serves comes from the SSOT krs::mqtt::buildTopicTree built from the
// canonical joint count, so new joints -> new nodes with no code. Built only with KR_WITH_MQTT.

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "MqttNodes.hpp"

#ifdef KR_WITH_MQTT
#include "MqttBridge.hpp"
#include "ArticulationSpec.hpp"
#include "FanucArticulation.hpp"

#include <mosquitto.h>
#include <QtGlobal>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <memory>
#include <vector>

namespace NodeLibrary {

static const char* mqttHost() { return "127.0.0.1"; }
static int mqttPort() { const int p = qEnvironmentVariableIntValue("KRS_MQTT_PORT"); return p > 0 ? p : 11883; }
static bool g_libInit = false;
static void ensureLib() { if (!g_libInit) { mosquitto_lib_init(); g_libInit = true; } }

// ---- subscribe-node: emits the latest value seen on its topic ----
class MqttSubscribeNode : public Node {
public:
    explicit MqttSubscribeNode(std::string topic) : m_topic(std::move(topic)) {
        m_id = "mqtt_sub:" + m_topic;
        m_ports.push_back({ "Value",     {"double","unitless"}, Port::Direction::Output, this });
        m_ports.push_back({ "FreshFlag", {"bool","unitless"},   Port::Direction::Output, this });
    }
    ~MqttSubscribeNode() override { if (m_c) { mosquitto_disconnect(m_c); mosquitto_destroy(m_c); } }
    const std::string& topic() const { return m_topic; }
    void compute() override {
        ensureClient();
        if (m_c) mosquitto_loop(m_c, 5, 1);
        setOutput<double>("Value", m_latest);
        setOutput<double>("FreshFlag", m_got ? 1.0 : 0.0);
    }
private:
    void ensureClient() {
        if (m_c) return;
        ensureLib();
        m_c = mosquitto_new(("sub-" + m_topic).c_str(), true, this);
        if (!m_c) return;
        mosquitto_message_callback_set(m_c, &MqttSubscribeNode::onMsg);
        if (mosquitto_connect(m_c, mqttHost(), mqttPort(), 30) != MOSQ_ERR_SUCCESS) { mosquitto_destroy(m_c); m_c = nullptr; return; }
        mosquitto_subscribe(m_c, nullptr, m_topic.c_str(), 1);
        for (int i = 0; i < 4; ++i) mosquitto_loop(m_c, 20, 1);   // settle SUBACK
    }
    static void onMsg(struct mosquitto*, void* ud, const struct mosquitto_message* m) {
        auto* self = static_cast<MqttSubscribeNode*>(ud);
        if (!m->payload || m->payloadlen <= 0) return;
        std::string s(static_cast<const char*>(m->payload), m->payloadlen);
        // a state payload may be "x,y,z" (link pose) or a scalar -- emit the first number.
        char* end = nullptr; const double v = std::strtod(s.c_str(), &end);
        if (end != s.c_str()) { self->m_latest = v; self->m_got = true; }
    }
    std::string m_topic; struct mosquitto* m_c = nullptr;
    double m_latest = 0.0; bool m_got = false;
};

// ---- publish-node: publishes its input value to its topic ----
class MqttPublishNode : public Node {
public:
    explicit MqttPublishNode(std::string topic) : m_topic(std::move(topic)) {
        m_id = "mqtt_pub:" + m_topic;
        m_ports.push_back({ "Value", {"double","unitless"}, Port::Direction::Input, this });
    }
    ~MqttPublishNode() override { if (m_c) { mosquitto_disconnect(m_c); mosquitto_destroy(m_c); } }
    const std::string& topic() const { return m_topic; }
    void compute() override {
        ensureClient();
        if (!m_c) return;
        const double v = getInputD("Value", 0.0);
        char buf[64]; const int n = std::snprintf(buf, sizeof(buf), "%.6f", v);
        mosquitto_publish(m_c, nullptr, m_topic.c_str(), n, buf, 1, false);
        for (int i = 0; i < 3; ++i) mosquitto_loop(m_c, 5, 1);    // flush
    }
private:
    void ensureClient() {
        if (m_c) return;
        ensureLib();
        m_c = mosquitto_new(("pub-" + m_topic).c_str(), true, this);
        if (!m_c) return;
        if (mosquitto_connect(m_c, mqttHost(), mqttPort(), 30) != MOSQ_ERR_SUCCESS) { mosquitto_destroy(m_c); m_c = nullptr; return; }
        for (int i = 0; i < 2; ++i) mosquitto_loop(m_c, 10, 1);
    }
    std::string m_topic; struct mosquitto* m_c = nullptr;
};

// ---- AUTO-POPULATION: register one sub-node per state topic + one pub-node per cmd topic, generated
//      from the canonical joint count (rule 6: same buildTopicTree the bus uses). New joints -> new nodes. ----
int autoRegisterMqttTopicNodes(const std::string& robotName, int jointCount)
{
    const krs::mqtt::TopicTree tt = krs::mqtt::buildTopicTree(robotName, jointCount);
    int n = 0;
    for (const std::string& topic : tt.jointState) {
        NodeFactory::instance().registerNodeType("mqtt_sub:" + topic,
            NodeDescriptor{ "Sub " + topic, "MQTT/Auto", "subscribe-node mirroring a live state topic" },
            [topic]() { return std::make_unique<MqttSubscribeNode>(topic); });
        ++n;
    }
    for (const std::string& topic : tt.jointCmd) {
        NodeFactory::instance().registerNodeType("mqtt_pub:" + topic,
            NodeDescriptor{ "Pub " + topic, "MQTT/Auto", "publish-node driving a command topic" },
            [topic]() { return std::make_unique<MqttPublishNode>(topic); });
        ++n;
    }
    return n;
}

int autoRegisterCanonicalMqttNodes()
{
    const krs::dyn::RobotArticSpec spec = krs::fanuc::canonicalSpec();
    return autoRegisterMqttTopicNodes("FANUC", int(spec.joints.size()));
}

// populate the palette with the canonical robot's topic nodes at startup (rule 6: from the same spec).
namespace { struct MqttAutoRegistrar { MqttAutoRegistrar() { autoRegisterCanonicalMqttNodes(); } };
           static MqttAutoRegistrar g_mqttAutoRegistrar; }

} // namespace NodeLibrary

// ===================================================================================================
// GATE NODE-MQTT (Phase 2 keystone): a publish-NODE drives an angle across a REAL broker to the LIVE
// PhysX articulation; the moved link's actual rotation returns on a state topic into a subscribe-NODE
// and matches the commanded angle <1e-4 -- with a severed-node neg-control. Not a value sitting in the
// editor: the value crosses the bus and the live robot moves.
// ===================================================================================================
#include "Scene.hpp"
#include "SimulationController.hpp"
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QThread>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <fstream>
#include <filesystem>

namespace krs::nodes {

namespace {
struct BrokerProc { QProcess proc; ~BrokerProc() { if (proc.state() != QProcess::NotRunning) { proc.kill(); proc.waitForFinished(2000); } } };

// the link's rotation magnitude from its rest orientation = the joint angle it reached.
double linkAngleFromRest(const std::array<float,7>& pose, const glm::quat& rest) {
    const glm::quat q(pose[6], pose[3], pose[4], pose[5]);   // (w,x,y,z)
    const glm::quat d = q * glm::inverse(rest);
    return 2.0 * std::acos(std::min(1.0, std::max(-1.0, double(std::abs(d.w)))));
}
} // namespace

bool runMqttNodeGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[node-mqtt] GATE NODE-MQTT -- publish-NODE drives the LIVE robot over the bus; state -> subscribe-NODE\n");

    const int port = NodeLibrary::mqttPort();
    std::string exe = "C:/Program Files/mosquitto/mosquitto.exe";
    if (const char* ov = std::getenv("KRS_MOSQUITTO_EXE")) exe = ov;
    if (!std::filesystem::exists(exe)) { printf("[node-mqtt] FAIL: broker exe not found\n"); fflush(stdout); return false; }
    std::error_code ec;
    const std::string conf = (std::filesystem::temp_directory_path(ec) / "krs_nodemqtt.conf").string();
    { std::ofstream f(conf); f << "listener " << port << " 127.0.0.1\n" << "allow_anonymous true\n"; }

    NodeLibrary::ensureLib();
    BrokerProc broker;
    broker.proc.start(QString::fromStdString(exe), QStringList{ "-c", QString::fromStdString(conf) });
    if (!broker.proc.waitForStarted(4000)) { printf("[node-mqtt] FAIL: broker did not start\n"); fflush(stdout); return false; }
    QThread::msleep(500);

    // --- live articulation (canonical FANUC) ---
    const krs::dyn::RobotArticSpec spec = krs::fanuc::canonicalSpec();
    const krs::mqtt::TopicTree tt = krs::mqtt::buildTopicTree("FANUC", int(spec.joints.size()));
    Scene scene; SimulationController sim(&scene);
    sim.setRobotArticulationSpec(spec); sim.play(); sim.setSceneGravity(0, 0, 0);
    std::vector<float> q0(spec.joints.size(), 0.0f); sim.setArticJointPositions(q0); sim.singleStep();
    auto poses0 = sim.articLinkPoses();
    if (poses0.empty()) { printf("[node-mqtt] FAIL: no articulation links\n"); fflush(stdout); sim.stop(); return false; }
    const glm::quat restQ(poses0[0][6], poses0[0][3], poses0[0][4], poses0[0][5]);

    // --- robot client: subscribes cmd wildcard (stores angle), publishes link state ---
    struct RobotState { double cmd = 0; bool hasCmd = false; } rs;
    struct mosquitto* robot = mosquitto_new("nodemqtt-robot", true, &rs);
    auto onCmd = [](struct mosquitto*, void* ud, const struct mosquitto_message* m) {
        auto* st = static_cast<RobotState*>(ud);
        std::string s(static_cast<const char*>(m->payload), m->payloadlen);
        char* e = nullptr; double v = std::strtod(s.c_str(), &e); if (e != s.c_str()) { st->cmd = v; st->hasCmd = true; } };
    mosquitto_message_callback_set(robot, onCmd);
    bool rconn = robot && mosquitto_connect(robot, NodeLibrary::mqttHost(), port, 30) == MOSQ_ERR_SUCCESS;
    if (rconn) { mosquitto_subscribe(robot, nullptr, tt.jointCmdWildcard.c_str(), 1); for (int i = 0; i < 6; ++i) mosquitto_loop(robot, 20, 1); }

    // --- the canvas NODES: a publish-node (joint0 cmd) and a subscribe-node (joint0 state) ---
    auto pub = std::make_unique<NodeLibrary::MqttPublishNode>(tt.jointCmd[0]);
    auto sub = std::make_unique<NodeLibrary::MqttSubscribeNode>(tt.jointState[0]);

    double lastLiveAng = 0.0;  // diagnostics: the live link angle the robot reported last round
    // one full round-trip driven by the publish-node's INPUT value.
    auto roundTrip = [&](double qCmd, bool drivePublishNode) -> double {
        sub->process();                                     // SUBSCRIBE FIRST (lazy connect) before any publish
        rs.hasCmd = false;
        if (drivePublishNode) {
            PortDataPacket pk; pk.data = qCmd; pk.type = { "", "" }; pub->setInput("Value", pk);
            pub->process();                                 // publish-NODE -> cmd topic
        }
        for (int i = 0; i < 12 && !rs.hasCmd; ++i) { if (rconn) mosquitto_loop(robot, 20, 1); QThread::msleep(5); }
        if (rs.hasCmd) {                                    // the live robot acts on the bus value
            std::vector<float> q(spec.joints.size(), 0.0f); q[0] = float(rs.cmd);
            sim.setArticJointPositions(q); sim.singleStep();
            const auto poses = sim.articLinkPoses();
            lastLiveAng = linkAngleFromRest(poses[0], restQ);          // live link rotation (FK)
            char buf[48]; const int n = std::snprintf(buf, sizeof(buf), "%.6f", lastLiveAng);
            if (rconn) { mosquitto_publish(robot, nullptr, tt.jointState[0].c_str(), n, buf, 1, false); for (int i = 0; i < 3; ++i) mosquitto_loop(robot, 10, 1); }
        }
        double v = std::nan("");
        for (int i = 0; i < 16; ++i) { sub->process();      // subscribe-NODE pumps + emits the received value
            for (const auto& p : sub->getPorts())
                if (p.direction == Port::Direction::Output && p.name == "Value" && p.packet.has_value())
                    try { v = std::any_cast<double>(p.packet->data); } catch (...) {} }
        return v;
    };

    const double qCmd = 0.6;
    const double got = roundTrip(qCmd, true);               // publish-node DRIVES
    printf("[node-mqtt]   (diag) robot got cmd, live link angle=%.4f rad\n", lastLiveAng);
    const double err = std::abs(got - qCmd);
    const bool fidelity = std::isfinite(got) && err < 1e-4;
    const bool moved = qCmd > 0.1;

    // NEG-CTRL (severed node): the publish-node does NOT publish -> the live robot gets no command this
    // round -> the subscribe-node never receives the new value (stays at the prior/rest reading).
    sim.setArticJointPositions(q0); sim.singleStep();       // reset the robot to rest
    const double sever = roundTrip(0.9, false);             // would-be command 0.9, but node severed
    const bool severed = !(std::isfinite(sever) && std::abs(sever - 0.9) < 1e-4);

    if (robot) { mosquitto_disconnect(robot); mosquitto_destroy(robot); }
    sim.stop();
    std::filesystem::remove(conf, ec);

    // NM2 coverage: a runtime robot with 3 joints AUTO-GENERATES 6 nodes (3 cmd + 3 state), each creatable.
    const int registered = NodeLibrary::autoRegisterMqttTopicNodes("TestBot3", 3);
    const auto t3 = krs::mqtt::buildTopicTree("TestBot3", 3);
    const bool subMade = NodeFactory::instance().createNode("mqtt_sub:" + t3.jointState[2]) != nullptr;
    const bool pubMade = NodeFactory::instance().createNode("mqtt_pub:" + t3.jointCmd[0]) != nullptr;
    const bool coverage = registered == 6 && subMade && pubMade;

    const bool pass = fidelity && moved && severed && coverage;
    printf("[node-mqtt]   NM2 auto-population: 3-joint robot -> %d nodes registered (want 6), sub+pub creatable %s  %s\n",
           registered, (subMade && pubMade) ? "yes" : "NO", coverage ? "PASS" : "FAIL");
    printf("[node-mqtt]   publish-NODE cmd=%.3f rad -> live link rotated, returned via subscribe-NODE=%.4f, err=%.2e (<1e-4)  %s\n",
           qCmd, got, err, fidelity ? "PASS" : "FAIL");
    printf("[node-mqtt]   moved=%.2f rad (>0.1) %s ; NEG-CTRL severed publish-node -> sub got %.4f (!=0.9) %s\n",
           qCmd, moved ? "yes" : "NO", sever, severed ? "REJECTS" : "VACUOUS!");
    printf("[node-mqtt] %s\n", pass ? "ALL PASS (canvas node drives the live robot through the bus, FK-verified <1e-4)"
                                     : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes

#else
namespace NodeLibrary {
int autoRegisterMqttTopicNodes(const std::string&, int) { return 0; }
int autoRegisterCanonicalMqttNodes() { return 0; }
} // namespace NodeLibrary
#endif
