#pragma once
// ===========================================================================
// KNODE -- multi-layer nodes: a SUBGRAPH saved as a reusable custom node (.knode).
//
// A subgraph node is a node whose interior is itself a node graph, with declared input/output ports
// that map to interior boundary nodes -- encapsulation + reuse, like a function, nesting to any
// depth. This module is the headless DATA MODEL + serializer: the KNodeDoc (interior nodes,
// connections, exposed-port mapping, identity) and its versioned JSON round-trip. It is
// content-addressed exactly like the .kscene/.krobot/.kmotor family (relative ref + id +
// contentHash; recognize-as-different on a hash change) and indexes into the krs::parts library as
// PartType::Node, so users build + share reusable node blocks. The Qt collapse-to-subgraph /
// double-click-to-enter UI and the interior evaluator (evaluateGraphQuiet over the interior) are
// built ON this model; see docs/NODE_SUBGRAPH.md. Connections address ports by NAME (not index) so
// they survive port reordering.
// ===========================================================================
#include <string>
#include <vector>
#include <QString>
#include <QJsonObject>

namespace krs::knode {

// One interior node: its factory type-id + its FULL restorable state as a structured record (the
// krs::kdoc node object: typed params/literals + reconfigure selection + update policy -- lossless,
// human-diffable, NOT an opaque blob). Geometry (canvas x/y) is UI-only, kept for faithful rebuild.
struct InteriorNode {
    QString id;                 // stable-within-doc node id
    QString typeId;             // NodeFactory type id (e.g. "gen_sine", or "knode:<uuid>" for a nested subgraph)
    QJsonObject state;          // krs::kdoc::nodeToJson record (params/literals/namedOption/policy)
    double  x = 0.0, y = 0.0;   // canvas position (UI)
};

// A connection: (fromNode.fromPort) -> (toNode.toPort), ports addressed by NAME.
struct Connection {
    QString fromNode, fromPort;
    QString toNode,   toPort;
};

// An exposed boundary port of the subgraph: the outer port `name` maps to an interior node's port.
struct ExposedPort {
    QString name;               // the outer-facing port name
    QString interiorNode;       // interior node id it connects to
    QString interiorPort;       // that node's port name
    bool    isInput = true;     // true = subgraph input (feeds interior), false = output (from interior)
    QString dataType;           // canonical port type name (PortTypes) so the outer port keeps its type
};

// A nested subgraph dependency: an interior node typed "knode:<uuid>" whose definition is itself a
// .knode. Referenced by the content-addressed triple; Phase 5 resolves these + a pack bundles them.
struct NestedRef { QString ref; QString id; QString contentHash; };

struct KNodeDoc {
    QString id;                 // stable uuid (minted on save if empty)
    QString name;               // "PID + Clamp"
    QString category;           // palette grouping, e.g. "Control"
    int     revision = 1;       // schema revision within the knode major (in-place schema evolution)
    std::vector<InteriorNode> nodes;
    std::vector<Connection>   connections;
    std::vector<ExposedPort>  ports;        // ordered; inputs then outputs as authored
    std::vector<NestedRef>    nested;       // nested subgraph deps (knode:<uuid> interiors), content-addressed

    std::vector<ExposedPort> inputs()  const;   // ports with isInput==true, in order
    std::vector<ExposedPort> outputs() const;   // ports with isInput==false, in order
    // Structural validity: every connection + exposed port references an existing interior node;
    // ids are unique; no exposed-port name collides within its direction. (Cycle detection across
    // layers is the evaluator's job, not the document's.)
    bool valid(QString* why = nullptr) const;
};

// Save `doc` to `absPath` as versioned JSON ("format":"knode/1"); mints an id if empty. Returns the
// doc id, or "" on I/O failure.
QString saveKNode(KNodeDoc& doc, const QString& absPath);

// Load a .knode document. Returns false (doc untouched) on missing/corrupt file, unknown format
// major, or a structurally-invalid document (nothing partially applied).
bool loadKNode(const QString& absPath, KNodeDoc& out, QString* err = nullptr);

// Headless gate (KRS_KNODE_SELFTEST): a doc with interior nodes + connections + exposed in/out
// ports round-trips through save/load bit-faithfully (ids, params, port mapping); an edited file has
// a different content hash (recognize-as-different); the parts library indexes a .knode as
// PartType::Node; NEG-CTRLs: a dangling connection is rejected by valid(); a corrupt/unknown-format
// file is refused by loadKNode without crashing. Returns true iff all pass.
bool runKNodeGate();

} // namespace krs::knode
