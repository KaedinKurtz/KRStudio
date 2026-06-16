// ThreadGate.cpp -- GATE THREAD: a UI edit must never stall the physics tick. Dial/spinbox edits POST to
// the coalescing NodeEditQueue (drained once per frame) instead of recomputing inline on the sim thread.
// We measure the physics tick rate (a) idle, (b) while hammering edits through the NEW async path, and
// (c) while hammering the SAME edits through the OLD synchronous path (the negative control). The async
// rate stays near the idle baseline; the synchronous path drops because every edit does an O(graph)
// recompute on the tick's critical path.

#include "Node.hpp"
#include "NodeDelegate.hpp"
#include "NodeEditorGate.hpp"
#include "NodeEditQueue.hpp"
#include "RobotGraph.hpp"
#include "Scene.hpp"
#include "SimulationController.hpp"
#include "FanucArticulation.hpp"
#include "ArticulationSpec.hpp"

#include <QtNodes/DataFlowGraphModel>
#include <QApplication>
#include <QWidget>
#include <QAbstractSlider>
#include <QDoubleSpinBox>

#include <cstdio>
#include <chrono>
#include <vector>
#include <memory>

namespace krs::nodes {

bool runThreadGate()
{
    using std::printf;
    using clock = std::chrono::steady_clock;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[thread] GATE THREAD -- UI edits decoupled from physics: tick rate idle vs hammered (async) vs old synchronous path\n");
    if (!QApplication::instance()) { printf("[thread] FAIL: needs QApplication\n"); return false; }

    // a real per-tick physics cost
    Scene scene; SimulationController sim(&scene);
    sim.setRobotArticulationSpec(krs::fanuc::canonicalSpec()); sim.play(); sim.setSceneGravity(0, 0, 0);
    if (sim.articDofCount() != 4) { sim.stop(); printf("[thread] FAIL: no articulation\n"); return false; }

    // a real graph whose recompute (sine -> drive, cascading) is the UI-edit work
    auto model = makeNodeGraphModel();
    BootGraphHandles h = spawnDefaultRobotGraph(*model, &scene, 0, 0.5, 0.5);
    auto* sDel = model->delegateModel<NodeDelegate>(h.sineId);
    Node* sN = sDel ? sDel->backendNode() : nullptr;
    if (!sDel || !sN) { sim.stop(); printf("[thread] FAIL: no graph\n"); return false; }

    const int editsPerFrame = 300;       // a hammered dial: a stress flood of edits per frame
    const double durSec = 0.50;
    enum Mode { IDLE, ASYNC, SYNC };

    // A real render frame steps physics by ~33 ms of wall-clock = ~8 substeps of 1/240 s. In a tight loop
    // sim.tick() accrues no wall-clock and does ZERO substeps, so we step the physics frame explicitly to a
    // realistic per-frame cost -- the edit overhead must be measured against a real frame, not a no-op.
    const int substepsPerFrame = 8;
    auto physicsFrame = [&]() { for (int s = 0; s < substepsPerFrame; ++s) sim.singleStep(); };

    auto& Q = NodeEditQueue::instance();
    auto measure = [&](Mode mode) -> double {
        Q.setDeferred(mode == ASYNC);    // ASYNC defers+coalesces; SYNC applies inline (the old path)
        Q.drain();                       // clear leftovers
        long frames = 0;
        const auto t0 = clock::now();
        while (std::chrono::duration<double>(clock::now() - t0).count() < durSec) {
            if (mode != IDLE)
                for (int e = 0; e < editsPerFrame; ++e)
                    Q.post(sN, "amp", [sDel]{ sDel->recomputeAndPropagate(); });   // the per-edit graph work
            if (mode == ASYNC) Q.drain();    // coalesced: ALL this frame's edits -> ONE recompute
            physicsFrame();
            ++frames;
        }
        Q.setDeferred(false);
        return double(frames) / durSec;  // physics frames per second
    };

    const double idle  = measure(IDLE);
    const double async = measure(ASYNC);
    const double sync  = measure(SYNC);
    sim.stop();

    // Faithfulness: a REAL widget edit routes through the queue when deferred (so it never runs inline).
    bool widgetDefers = false, widgetImmediate = false;
    {
        QWidget* body = sDel->embeddedWidget();
        QAbstractSlider* ampDial = nullptr;
        if (body) for (QWidget* w : body->findChildren<QWidget*>())
            if (w->property("krs_param").toString() == "amp") { ampDial = qobject_cast<QAbstractSlider*>(w); break; }
        if (ampDial) {
            Q.setDeferred(true); Q.drain();
            ampDial->setValue((ampDial->value() + 137) % 1000);     // a dial move
            widgetDefers = (Q.pending() > 0);                       // deferred -> queued, NOT applied inline
            Q.drain();
            Q.setDeferred(false);
            ampDial->setValue((ampDial->value() + 137) % 1000);
            widgetImmediate = (Q.pending() == 0);                   // immediate -> applied inline, nothing queued
        }
    }

    // Use-after-free safety: a node destroyed while edits are queued must cancel them so the next drain()
    // does not call a dangling-pointer closure. Post via both a param dial (keyed by the backend Node*)
    // and an input spinbox (keyed by the delegate), destroy the delegate, then drain.
    bool uafSafe = false;
    {
        Q.setDeferred(true); Q.drain();
        size_t afterPost = 0;
        {
            NodeDelegate tmp("gen_sine");
            QWidget* b = tmp.embeddedWidget();
            if (b) for (QWidget* w : b->findChildren<QWidget*>()) {
                if (w->property("krs_param").toString() == "freq")
                    if (auto* s = qobject_cast<QAbstractSlider*>(w)) s->setValue(s->value() + 50);
                if (w->property("krs_input_port").toString() == "t")
                    if (auto* sb = qobject_cast<QDoubleSpinBox*>(w)) sb->setValue(1.234);
            }
            afterPost = Q.pending();
        }                                   // ~NodeDelegate cancels tmp's queued edits here
        const size_t afterDtor = Q.pending();
        Q.drain();                          // must be safe even though tmp is gone
        Q.setDeferred(false);
        uafSafe = (afterPost > 0) && (afterDtor == 0);
        printf("[thread]   use-after-free safety: queued=%zu before destroy -> %zu after (cancelled), drain safe: %s\n",
               afterPost, afterDtor, uafSafe ? "yes" : "NO");
    }

    const bool asyncOk  = idle > 0 && async > 0.8 * idle;          // async stays within ~20% of baseline
    const bool syncDrop = sync < 0.7 * idle;                       // old synchronous path drops >=30%
    const bool clearGap = idle > 0 && (async - sync) / idle > 0.2; // async is >=20 points better than sync
    const bool widgetOk = widgetDefers && widgetImmediate;
    const bool pass = asyncOk && syncDrop && clearGap && widgetOk && uafSafe;

    printf("[thread]   tick rate: idle=%.0f/s, async(hammered)=%.0f/s (%.0f%% of idle), sync(old path)=%.0f/s (%.0f%% of idle)\n",
           idle, async, idle > 0 ? 100.0 * async / idle : 0.0, sync, idle > 0 ? 100.0 * sync / idle : 0.0);
    printf("[thread]   async within tolerance of baseline=%s; OLD synchronous path drops=%s; async>>sync=%s\n",
           asyncOk ? "yes" : "NO", syncDrop ? "yes" : "NO", clearGap ? "yes" : "NO");
    printf("[thread]   widget faithfulness: deferred dial move queues (pending>0)=%s; immediate applies inline=%s\n",
           widgetDefers ? "yes" : "NO", widgetImmediate ? "yes" : "NO");
    printf("[thread] %s\n", pass ? "ALL PASS (async edits preserve the tick rate; the old synchronous path stalls it; widget edits route through the queue)"
                                  : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
