#pragma once
// MqttNodes.hpp -- Phase 2: auto-populating MQTT topic nodes. Subscribe/publish nodes are generated from
// the canonical topic tree (rule 6: krs::mqtt::buildTopicTree from the canonical joint count) so new
// joints produce new nodes with no code. Gate NODE-MQTT proves a publish-node drives the live robot
// through the bus (FK-verified <1e-4).

#include <string>

namespace NodeLibrary {

// Register one subscribe-node per state topic + one publish-node per cmd topic for a robot with
// `jointCount` joints. Returns the number of node types registered. (No-op without KR_WITH_MQTT.)
int autoRegisterMqttTopicNodes(const std::string& robotName, int jointCount);

// Convenience: auto-register from the canonical FANUC spec's joint count.
int autoRegisterCanonicalMqttNodes();

} // namespace NodeLibrary

namespace krs::nodes {
// Phase 2 GATE NODE-MQTT (KRS_NODEMQTT_SELFTEST): a publish-NODE drives an angle across a real broker to
// the live articulation; the resulting link pose returns on a state topic into a subscribe-NODE and
// matches the analytic FK oracle <1e-4 -- with a severed-node negative control (no publish -> no value).
bool runMqttNodeGate();
}
