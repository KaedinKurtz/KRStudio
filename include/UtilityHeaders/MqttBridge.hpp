#pragma once
// MqttBridge.hpp -- Phase 4 MQTT integration. A real Mosquitto broker is spawned on startup
// (the system mosquitto.exe via QProcess) and the canonical articulation graph is published as a
// nested topic tree: robot/<name>/joint/<n>/cmd (inbound joint commands) and .../state (outbound
// telemetry). The wire client is libmosquitto (vcpkg, KR_WITH_MQTT). buildTopicTree below is the
// SINGLE SOURCE OF TRUTH mapping a robot's joint count to its topic names -- consumed by both the
// runtime bridge and GATE M, so the tree the gate checks is byte-for-byte the tree the app uses.

#include <string>
#include <vector>

namespace krs::mqtt {

// Nested topic tree derived from the canonical graph. One cmd + one state topic per joint, plus a
// per-robot broadcast topic. Hierarchy: robot/<name>/joint/<n>/{cmd,state}, robot/<name>/broadcast.
struct TopicTree {
    std::string root;                       // "robot/<name>"
    std::string broadcast;                  // "robot/<name>/broadcast"
    std::vector<std::string> jointCmd;      // [n] = "robot/<name>/joint/<n>/cmd"
    std::vector<std::string> jointState;    // [n] = "robot/<name>/joint/<n>/state"
    std::string jointCmdWildcard;           // "robot/<name>/joint/+/cmd"
};

// SSOT: build the topic tree for a robot with `jointCount` joints. Pure string logic (no broker,
// no Qt) so it is identical in the app and in the gate.
inline TopicTree buildTopicTree(const std::string& robotName, int jointCount)
{
    TopicTree t;
    t.root      = "robot/" + robotName;
    t.broadcast = t.root + "/broadcast";
    t.jointCmdWildcard = t.root + "/joint/+/cmd";
    for (int i = 0; i < jointCount; ++i) {
        t.jointCmd.push_back(t.root + "/joint/" + std::to_string(i) + "/cmd");
        t.jointState.push_back(t.root + "/joint/" + std::to_string(i) + "/state");
    }
    return t;
}

// True if built with libmosquitto (KR_WITH_MQTT).
bool available();

// Phase 4 GATE M (gated by KRS_MQTT_SELFTEST): spawns the real Mosquitto broker on a test port,
// connects libmosquitto clients, and proves: M1 pub/sub round-trip through the broker; M2 the
// nested topic tree matches the canonical joint count; M3 a REAL joint round-trip (publish an angle
// on .../joint/n/cmd -> robot handler runs FK -> publishes the moved link pose on .../state, which
// matches direct FK <1e-4 AND differs from the rest pose, so the message caused the motion);
// M4 broadcast->receive duality (one publish reaches N subscribers; a wrong-topic subscriber does
// not). Returns true on PASS (vacuous true in the no-MQTT stub). Requires no GL context.
bool runMqttGateM();

} // namespace krs::mqtt
