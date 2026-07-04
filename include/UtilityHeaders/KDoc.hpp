#pragma once
// ===========================================================================
// KDoc -- the Node <-> JSON codec (krs::kdoc). The keystone of graph + subgraph
// persistence: the ONE place a backend Node's full RESTORABLE state is turned
// into JSON and back, so BOTH .kgraph (whole dataflow graph) and .knode
// (reusable subgraph) serialize node internals through a single, tested path.
//
// WHAT A NODE'S RESTORABLE STATE IS (everything evaluateGraphQuiet does NOT
// recompute): the tunable params (Node::m_params, std::map<string, std::any>),
// the per-input-port literal values (Port::literalValue, the in-node widget
// values), the reconfiguration selection (namedOption(), e.g. the Property
// node's chosen property, which DERIVES the port set), and the update policy
// (Asynchronous/Synchronous/Triggered + trigger edge). Transient state (port
// packets, freshness, perf, live trigger edge, the injected Scene*) is NOT
// serialized -- it is recomputed / re-injected on load.
//
// THE std::any PROBLEM: params + literals are type-erased with no tag at rest.
// We encode each as a tagged {t,v} object over the VERIFIED closed union of
// types nodes actually store: double, float, int, long long, bool, std::string,
// glm::vec3. The 64-bit integer case is STRING-encoded (QJsonValue numbers are
// IEEE doubles -- lossy above 2^53; real entt handles exceed it). An unknown
// type FAILS LOUD (encode emits t="?"; decode returns false) so a new stored
// type surfaces at gate time instead of silently reloading a node at defaults.
//
// RESTORE ORDER (load-bearing): createNode(typeId) [caller] -> restore params
// -> selectNamedOption(namedOption) to rebuild the port set -> restore port
// literals BY NAME (the now-correct ports exist) -> restore update policy.
// ===========================================================================
#include <string>
#include <QJsonObject>

class Node;

namespace krs::kdoc {

// Serialize a live backend Node's full restorable state. `typeId` is the NodeFactory key used to
// recreate it (the Node base does not distinguish type-id from its instance m_id, so the caller --
// which created the node via the factory / delegate -- supplies it). Never throws.
QJsonObject nodeToJson(const Node& n, const std::string& typeId);

// Apply a nodeToJson() object onto a freshly created node (caller has already done
// NodeFactory::createNode(typeId)). Returns false (and, if `err`, a reason) on a malformed doc or an
// unknown type tag -- fail-loud, nothing half-applied past the offending field. Never throws.
bool applyJsonToNode(Node& n, const QJsonObject& o, QString* err = nullptr);

// The type-id stored in a nodeToJson() object (convenience for the graph loader).
std::string typeIdOf(const QJsonObject& o);

// Headless gate (KRS_KDOC_SELFTEST): round-trips the REAL demo nodes -- gen_sine (double params),
// physics_articulation_drive (int port literal), a glm::vec3 param, and a 2^53+1 int64 (proves
// string-encoding) -- asserting bit-equal restoration and a non-vacuous before/after. Reconfigurable
// twin_property: selectNamedOption("orientation") rebuilds its ports (a Quaternion output appears);
// after a fresh createNode + applyJsonToNode the port layout is restored (selectNamedOption ran
// during load, before literals). NEG-CTRLs: an unknown type tag fails loud (not silently defaulted);
// the before-apply node differs from the restored one (the round-trip is doing real work).
bool runKDocGate();

} // namespace krs::kdoc
