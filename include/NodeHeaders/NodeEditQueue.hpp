#pragma once
// NodeEditQueue.hpp -- decouple UI edits from the physics thread (node-ecosystem sprint). A dial/spinbox
// edit used to run setParam/setPortLiteral + a full graph recompute SYNCHRONOUSLY in the Qt signal
// handler, on the same thread that ticks physics -- so a rapid drag starved the sim tick (the stall).
//
// Instead, edits POST a closure here. When deferred (the live app sets this), posts are COALESCED by
// (object,name) and applied once per frame by drain() -- so N edits in a frame cost N cheap enqueues + 1
// recompute, not N recomputes, and a UI event never does O(graph) work inline on the sim's critical path.
// When NOT deferred (the default -- headless gates that read a node's output immediately after an edit),
// post() applies the closure inline, exactly as before. The deferred flag is also the old-sync vs new-async
// toggle GATE THREAD measures.
#include <functional>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <string>
#include <cstddef>

namespace krs::nodes {

class NodeEditQueue {
public:
    static NodeEditQueue& instance();

    void setDeferred(bool d);
    bool deferred() const;

    // Post an edit. key = (obj,name) so rapid edits to the SAME control coalesce to the latest. When not
    // deferred, runs applyAndRecompute() immediately (legacy inline behavior).
    void post(const void* obj, const std::string& name, std::function<void()> applyAndRecompute);

    std::size_t drain();          // apply all pending (coalesced) closures; returns how many ran
    std::size_t pending() const;

private:
    NodeEditQueue() = default;
    mutable std::mutex m_mtx;
    std::unordered_map<std::string, std::function<void()>> m_latest;
    std::atomic<bool> m_deferred{false};
};

} // namespace krs::nodes
