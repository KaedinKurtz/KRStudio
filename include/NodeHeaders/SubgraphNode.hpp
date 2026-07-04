#pragma once
// ===========================================================================
// SubgraphNode -- a node whose INTERIOR is a subgraph (a krs::knode::KNodeDoc),
// registered as a first-class palette node "knode:<id>". It instantiates the
// interior's backend nodes, exposes the doc's promoted boundary ports as its own
// outer ports, and on compute() marshals its inputs onto the interior boundary,
// evaluates the interior (topological, pure backend -- no QtNodes model), and
// marshals the interior boundary outputs back out. Nests to any depth (an interior
// node typed "knode:<id>" is itself a SubgraphNode), with a construction-time
// cycle guard so a self-referential definition can never recurse infinitely.
//
// registerDiscoveredKNodes() is the library->factory bridge: it turns every
// discovered .knode into a creatable, categorized, draggable node -- the mechanism
// that makes an authored (or imported/shared) subgraph appear in the Add menu.
// ===========================================================================
#include "Node.hpp"
#include "KNode.hpp"

#include <memory>
#include <vector>
#include <unordered_map>
#include <QString>

namespace krs::parts { class PartLibrary; }

namespace krs::nodes {

class SubgraphNode : public Node {
public:
    explicit SubgraphNode(const krs::knode::KNodeDoc& doc);
    void compute() override;
    bool needsExecutionControls() const override { return false; }
    int  interiorCount() const { return int(m_interior.size()); }   // instantiated (non-skipped) interior nodes

    // Propagate the live scene into every interior node (recursively, since an interior may itself be
    // a SubgraphNode). Without this an interior scene-writing node (e.g. physics_articulation_drive)
    // early-outs on !m_scene and the subgraph cannot actuate.
    void setScene(Scene* s) override {
        Node::setScene(s);
        for (auto& n : m_interior) if (n) n->setScene(s);
    }

private:
    void evalInterior();
    krs::knode::KNodeDoc                 m_doc;
    std::vector<std::unique_ptr<Node>>   m_interior;   // owns the instantiated backend nodes
    std::unordered_map<QString, Node*>   m_byDocId;    // doc node id -> backend (nullptr = skipped/cyclic/unknown)
};

// Register every PartType::Node (.knode) in `lib` as a NodeFactory type "knode:<id>" whose factory
// builds a SubgraphNode from that file. Returns the count registered. Idempotent (last-write-wins).
int registerDiscoveredKNodes(krs::parts::PartLibrary& lib);

// Headless gate (KRS_SUBGRAPH_SELFTEST): a .knode registered as knode:<id> instantiates via
// NodeFactory and evaluates end-to-end (input marshaled through the interior to the output) matching
// the flattened math; a NESTED subgraph (one used inside another) yields the 2-level result; NEG-CTRL:
// a self-referential/cyclic definition is instantiated safely (cyclic interior omitted), never wedged.
bool runSubgraphGate();

// Headless gate (KRS_SUBGRAPHACT_SELFTEST) -- Process & Skills P0, subgraph ACTUATION: a subgraph
// containing a drive node writes the command bus ONLY after setScene propagates into its interior
// (before: nothing -- the !m_scene early-out is real); propagation recurses into a NESTED subgraph;
// the new physics_config_drive fans a whole joint_config onto the ROBOT-KEYED lane (index i -> DOF i
// of the chosen robot); NEG-CTRL: a disconnected Config commands nothing (the DOFs release).
bool runSubgraphActuationGate();

} // namespace krs::nodes
