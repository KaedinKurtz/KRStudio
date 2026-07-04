#pragma once
// ===========================================================================
// KGraph -- the live QtNodes DataFlowGraphModel <-> .kgraph document bridge
// (krs::kgraph). A .kgraph is the SAME structured document as a .knode (shared
// krs::knode::writeDoc/readDoc + krs::kdoc node codec) with NO exposed boundary
// ports -- i.e. a whole top-level dataflow graph persisted as one content-hashed,
// diffable, shareable file, so the boot-ephemeral node wiring survives a restart.
//
//   harvestGraph:   live model -> KNodeDoc (nodes with kdoc state, wiring BY NAME,
//                   canvas positions; exposed[] empty).
//   materializeGraph: KNodeDoc -> a fresh live model (addNode + applyJsonToNode +
//                   position, then re-wire by resolving port NAMES -> indices).
//                   Reconfigurable interior nodes rebuild their port set via the
//                   codec's selectNamedOption step BEFORE wiring. Unknown factory
//                   ids / unresolvable ports are skipped + reported, never crash.
// ===========================================================================
#include <QString>

namespace QtNodes { class DataFlowGraphModel; }
namespace krs::knode { struct KNodeDoc; }
class Scene;

namespace krs::kgraph {

krs::knode::KNodeDoc harvestGraph(QtNodes::DataFlowGraphModel& model);

bool materializeGraph(const krs::knode::KNodeDoc& doc, QtNodes::DataFlowGraphModel& model,
                      Scene* scene = nullptr, QString* err = nullptr);

// .kgraph/1 file I/O (content-addressed like the rest of the .k* family).
QString saveKGraph(QtNodes::DataFlowGraphModel& model, const QString& absPath, const QString& name = {});
bool    loadKGraph(const QString& absPath, QtNodes::DataFlowGraphModel& model, Scene* scene = nullptr, QString* err = nullptr);

// Headless gate (KRS_KGRAPH_SELFTEST): build the real boot graph (time->sine->drive) + a reconfigured
// twin_property into model A, save a .kgraph, load into a FRESH model B, and assert node/param/literal/
// wiring/position fidelity AND that the reconfigured interior node materialized correctly through the
// live delegate (the port layout re-derives on load). Content hash is deterministic + changes on an
// interior edit. NEG-CTRLs: an unknown format major is refused; an unknown factory id is skipped +
// reported (the rest of the graph still materializes), not crashed.
bool runKGraphGate();

} // namespace krs::kgraph
