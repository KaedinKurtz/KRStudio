#pragma once
// EvalEngine.hpp -- separate the EVALUATION tick from the UI-UPDATE tick (node-UI+perf sprint, BUG 2).
// The old live tick called NodeDelegate::recomputeAndPropagate, which emits QtNodes dataUpdated -> the
// scene's onNodeUpdated -> recomputeSize + node->update() (repaint the heavy proxy widget) PER downstream
// node PER eval (~50x the pure math, the ~45ms blowup). evaluateGraphQuiet does the math at the control
// rate WITHOUT any QtNodes signals (no scene repaint). Display widgets refresh separately + rate-capped
// (refreshGraphUi -> Node::refreshUi), never per eval.
#include <functional>
#include <cstdint>

namespace QtNodes { class DataFlowGraphModel; }

namespace krs::nodes {

// Topologically process every node + propagate each output packet to its connected downstream inputs
// directly on the backend nodes -- NO QtNodes dataUpdated, so the scene does not repaint. Microseconds.
void evaluateGraphQuiet(QtNodes::DataFlowGraphModel& model);

// Push each node's computed display value into its widget IF it changed (Node::refreshUi). Call at a capped
// UI rate (~30-60Hz), NOT per eval. Returns how many widgets actually repainted (changed).
int refreshGraphUi(QtNodes::DataFlowGraphModel& model);

// Decoupled eval/UI loop (the gateable core of the rate feature): run eval at evalHz (best-effort) for
// durSec, calling uiFn no more than uiCapHz times/sec regardless of evalHz. Returns counts via out-params.
struct EvalLoopStats { long evals = 0; long uiRefreshes = 0; double seconds = 0.0; };
EvalLoopStats runEvalLoop(double evalHz, double uiCapHz, double durSec,
                          const std::function<void()>& evalFn, const std::function<void()>& uiFn);

} // namespace krs::nodes
