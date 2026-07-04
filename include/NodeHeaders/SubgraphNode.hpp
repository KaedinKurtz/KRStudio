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

} // namespace krs::nodes
