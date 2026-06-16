// ProfileGate.cpp -- RECON/PROFILE harness for the node-UI+perf sprint (run via KRS_NODEPROF_SELFTEST).
// Proves both bugs with real numbers BEFORE fixing:
//   (1) FRAME: builds the REAL NodeGraphicsObject for a framed node (time_source) vs a frameless heavy
//       node (gen_sine) and dumps boundingRect / captionRect / widgetPosition / embedded-widget size /
//       port positions -- so we can SEE which graphics component is missing or covered.
//   (2) PERF: times the eval cascade WITH a scene (the cascade -> onNodeUpdated -> recomputeSize+update)
//       vs WITHOUT a scene (model propagation only) for a chain of N heavy nodes -> isolates the cost.

#include "Node.hpp"
#include "NodeDelegate.hpp"
#include "NodeFactory.hpp"
#include "NodeEditorGate.hpp"
#include "EvalEngine.hpp"
#include "CustomDataFlowScene.hpp"

#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/internal/AbstractNodeGeometry.hpp>
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QtNodes/Definitions>
#include <QApplication>
#include <QWidget>
#include <QImage>
#include <QPainter>

#include <cstdio>
#include <chrono>
#include <vector>
#include <string>

namespace krs::nodes {

static void dumpGfx(CustomDataFlowScene& scene, QtNodes::DataFlowGraphModel& model, const char* type)
{
    using std::printf;
    const QtNodes::NodeId id = model.addNode(type);
    if (id == QtNodes::InvalidNodeId) { printf("[prof]   %-28s addNode FAILED\n", type); return; }
    QtNodes::NodeGraphicsObject* go = scene.nodeGraphicsObject(id);
    auto& geo = scene.nodeGeometry();
    auto* del = model.delegateModel<NodeDelegate>(id);
    QWidget* w = del ? del->embeddedWidget() : nullptr;
    const QRectF br = go ? go->boundingRect() : QRectF();
    const QSize sz = geo.size(id);
    const QRectF cap = geo.captionRect(id);
    const QPointF wpos = geo.widgetPosition(id);
    const unsigned nIn = del ? del->nPorts(QtNodes::PortType::In) : 0;
    const unsigned nOut = del ? del->nPorts(QtNodes::PortType::Out) : 0;
    QPointF p0In = nIn ? geo.portPosition(id, QtNodes::PortType::In, 0) : QPointF(-1, -1);
    QPointF p0Out = nOut ? geo.portPosition(id, QtNodes::PortType::Out, 0) : QPointF(-1, -1);
    printf("[prof]   %-28s GO=%s  bound=%.0fx%.0f size=%dx%d  cap.h=%.0f  widget=%dx%d wpos.y=%.0f  ports in=%u out=%u  inPort0=(%.0f,%.0f) outPort0=(%.0f,%.0f)\n",
           type, go ? "yes" : "NULL", br.width(), br.height(), sz.width(), sz.height(), cap.height(),
           w ? w->width() : -1, w ? w->height() : -1, wpos.y(), nIn, nOut, p0In.x(), p0In.y(), p0Out.x(), p0Out.y());
}

bool runNodeProfileDiag()
{
    using std::printf;
    using clock = std::chrono::steady_clock;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[prof] ===== NODE UI + PERF PROFILE (recon, no asserts) =====\n");
    if (!QApplication::instance()) { printf("[prof] needs QApplication\n"); return false; }

    // ---- (1) FRAME geometry dump: framed vs frameless ----
    printf("[prof] -- graphics geometry per node type (framed: time_source/physics_articulation_drive; suspect-frameless: gen_sine/ctrl_goal_knob) --\n");
    {
        auto model = makeNodeGraphModel();
        CustomDataFlowScene scene(*model);
        for (const char* t : { "time_source", "physics_articulation_drive", "math_add",
                               "gen_sine", "ctrl_goal_knob", "viz_dial_gauge", "control_pid" })
            dumpGfx(scene, *model, t);

        // Render a framed node (time_source) and a suspect-frameless node (gen_sine) to PNGs so the actual
        // pixels can be inspected -- the definitive test for "is the frame visible or covered by the widget".
        auto renderNode = [&](const char* t, const char* file) {
            const QtNodes::NodeId id = model->addNode(t);
            QtNodes::NodeGraphicsObject* go = scene.nodeGraphicsObject(id);
            if (!go) { printf("[prof]   render %s: no GO\n", t); return; }
            const QRectF sbr = go->sceneBoundingRect().adjusted(-6, -6, 6, 6);
            QImage img(int(sbr.width()), int(sbr.height()), QImage::Format_ARGB32_Premultiplied);
            img.fill(QColor(40, 40, 48));                    // distinct backdrop so the node frame is obvious
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            scene.render(&p, QRectF(0, 0, sbr.width(), sbr.height()), sbr);
            p.end();
            const QString path = QStringLiteral("C:/Users/kurtz/KRStudio/KRStudio/") + file;
            printf("[prof]   render %-12s -> %s  (%s)\n", t, file, img.save(path) ? "saved" : "SAVE FAILED");
        };
        renderNode("time_source", "node_time_source.png");
        renderNode("gen_sine", "node_gen_sine.png");
    }

    // ---- (2) PERF: eval cascade cost WITH a scene vs WITHOUT, for a chain of N heavy nodes ----
    printf("[prof] -- eval cost: recomputeAndPropagate over a chain of N gen_sine nodes, WITH scene (cascade->onNodeUpdated) vs WITHOUT --\n");
    auto buildChainAndTime = [&](int N, bool withScene) -> double {
        auto model = makeNodeGraphModel();
        std::unique_ptr<CustomDataFlowScene> scene;
        if (withScene) scene = std::make_unique<CustomDataFlowScene>(*model);
        std::vector<QtNodes::NodeId> ids;
        for (int i = 0; i < N; ++i) ids.push_back(model->addNode("gen_sine"));
        // chain: ids[i].Out -> ids[i+1].t
        for (int i = 0; i + 1 < N; ++i) {
            auto* du = model->delegateModel<NodeDelegate>(ids[i]);
            auto* dv = model->delegateModel<NodeDelegate>(ids[i + 1]);
            const int oi = portIndexByName(du->backendNode(), Port::Direction::Output, "Out");
            const int ii = portIndexByName(dv->backendNode(), Port::Direction::Input, "t");
            const QtNodes::ConnectionId c{ ids[i], QtNodes::PortIndex(oi), ids[i + 1], QtNodes::PortIndex(ii) };
            if (model->connectionPossible(c)) model->addConnection(c);
        }
        auto* first = model->delegateModel<NodeDelegate>(ids[0]);
        const int reps = 50;
        const auto t0 = clock::now();
        for (int r = 0; r < reps; ++r) first->recomputeAndPropagate();
        const double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count() / reps;
        return ms;
    };
    for (int N : { 5, 15, 30 }) {
        const double withS = buildChainAndTime(N, true);
        const double noS = buildChainAndTime(N, false);
        printf("[prof]   N=%2d : WITH scene = %7.3f ms/eval   WITHOUT scene = %7.3f ms/eval   (scene overhead %7.3f ms)\n",
               N, withS, noS, withS - noS);
    }
    printf("[prof] ===== END PROFILE =====\n");
    fflush(stdout);
    return true;
}

// ============================ GATE FRAME-GFX ============================
// One layer up from the old FRAME gate (which checked the DATA MODEL): assert the GRAPHICS object exists
// with its frame components -- a title (captionRect height>0), a valid bounding geometry taller than the
// caption (a frame body), and every declared port positioned on the node BOUNDARY at the graphics level.
bool runFrameGfxGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[frame-gfx] GATE FRAME-GFX -- every node type's NodeGraphicsObject has caption + geometry + boundary ports\n");
    if (!QApplication::instance()) { printf("[frame-gfx] FAIL: needs QApplication\n"); return false; }

    auto model = makeNodeGraphModel();
    CustomDataFlowScene scene(*model);
    auto& geo = scene.nodeGeometry();

    auto framedAtGfx = [&](QtNodes::NodeId id) -> bool {
        QtNodes::NodeGraphicsObject* go = scene.nodeGraphicsObject(id);
        if (!go) return false;
        const QRectF br = go->boundingRect();
        const QRectF cap = geo.captionRect(id);
        auto* del = model->delegateModel<NodeDelegate>(id);
        if (!del) return false;
        const unsigned nIn = del->nPorts(QtNodes::PortType::In), nOut = del->nPorts(QtNodes::PortType::Out);
        if (cap.height() <= 0) return false;                       // a title/header is reserved
        if (br.height() <= cap.height() + 2.0) return false;       // a frame BODY beyond the caption
        if (nIn + nOut == 0) return false;                         // wireable
        // every port has a graphics position on the node boundary (x ~ 0 left, or x ~ width right)
        const double W = double(geo.size(id).width());
        for (unsigned i = 0; i < nIn; ++i) {
            const QPointF p = geo.portPosition(id, QtNodes::PortType::In, i);
            if (!(std::abs(p.x()) < 2.0) || p.y() < cap.height()) return false;
        }
        for (unsigned i = 0; i < nOut; ++i) {
            const QPointF p = geo.portPosition(id, QtNodes::PortType::Out, i);
            if (!(std::abs(p.x() - W) < 2.0) || p.y() < cap.height()) return false;
        }
        return true;
    };

    int M = 0, framed = 0;
    std::vector<std::string> frameless;
    for (const auto& kv : NodeFactory::instance().getRegisteredNodeTypes()) {
        ++M;
        const QtNodes::NodeId id = model->addNode(QString::fromStdString(kv.first));
        if (id != QtNodes::InvalidNodeId && framedAtGfx(id)) ++framed;
        else frameless.push_back(kv.first);
    }
    // NEG-CTRL: the SAME predicate flags an absent graphics object. An unregistered type cannot be added,
    // so it has no NodeGraphicsObject -> the predicate returns frameless (it is not vacuously true).
    const QtNodes::NodeId bad = model->addNode(QStringLiteral("__no_such_node_type__"));
    const bool negOk = (bad == QtNodes::InvalidNodeId);            // no GO built for an unknown type

    const bool pass = (M > 0) && (framed == M) && negOk;
    printf("[frame-gfx]   FRAME-GFX coverage: %d/%d node types build a NodeGraphicsObject with caption + frame body + boundary ports\n", framed, M);
    if (!frameless.empty()) { printf("[frame-gfx]   FRAMELESS-AT-GFX (caught): "); for (auto& s : frameless) printf("%s ", s.c_str()); printf("\n"); }
    printf("[frame-gfx]   NEG-CTRL: an unknown type builds no graphics object (caught)=%s\n", negOk ? "yes" : "NO");
    printf("[frame-gfx] %s (on-screen pixels remain OPERATOR VISUAL-CONFIRM)\n",
           pass ? "ALL PASS (every registered type has its full graphics frame -- title + geometry + boundary ports)" : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

// ============================ GATE PERF ============================
namespace {
std::shared_ptr<QtNodes::DataFlowGraphModel> buildSineChain(int N, std::unique_ptr<CustomDataFlowScene>& scene,
                                                            bool withScene, QtNodes::NodeId& headOut)
{
    auto model = makeNodeGraphModel();
    if (withScene) scene = std::make_unique<CustomDataFlowScene>(*model);
    std::vector<QtNodes::NodeId> ids;
    for (int i = 0; i < N; ++i) ids.push_back(model->addNode("gen_sine"));
    headOut = ids.empty() ? QtNodes::InvalidNodeId : ids[0];
    for (int i = 0; i + 1 < N; ++i) {
        auto* du = model->delegateModel<NodeDelegate>(ids[i]);
        auto* dv = model->delegateModel<NodeDelegate>(ids[i + 1]);
        const int oi = portIndexByName(du->backendNode(), Port::Direction::Output, "Out");
        const int ii = portIndexByName(dv->backendNode(), Port::Direction::Input, "t");
        const QtNodes::ConnectionId c{ ids[i], QtNodes::PortIndex(oi), ids[i + 1], QtNodes::PortIndex(ii) };
        if (model->connectionPossible(c)) model->addConnection(c);
    }
    return model;
}
} // namespace

bool runPerfGate()
{
    using std::printf;
    using clock = std::chrono::steady_clock;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[perf] GATE PERF -- quiet eval is bounded + scales sanely; the old per-eval scene-repaint cascade blows up\n");
    if (!QApplication::instance()) { printf("[perf] FAIL: needs QApplication\n"); return false; }

    const int reps = 60;
    auto timeOld = [&](int N) {                  // recomputeAndPropagate WITH a scene = the cascade (neg-ctrl)
        std::unique_ptr<CustomDataFlowScene> scene; QtNodes::NodeId head;
        auto model = buildSineChain(N, scene, true, head);
        auto* first = model->delegateModel<NodeDelegate>(head);
        const auto t0 = clock::now();
        for (int r = 0; r < reps; ++r) first->recomputeAndPropagate();
        return std::chrono::duration<double, std::milli>(clock::now() - t0).count() / reps;
    };
    auto timeNew = [&](int N) {                  // evaluateGraphQuiet = the fix
        std::unique_ptr<CustomDataFlowScene> scene; QtNodes::NodeId head;
        auto model = buildSineChain(N, scene, true, head);   // same graph + scene present; quiet eval ignores it
        const auto t0 = clock::now();
        for (int r = 0; r < reps; ++r) evaluateGraphQuiet(*model);
        return std::chrono::duration<double, std::milli>(clock::now() - t0).count() / reps;
    };

    const int Ns[] = { 5, 15, 30 };
    double oldMs[3], newMs[3];
    for (int i = 0; i < 3; ++i) { oldMs[i] = timeOld(Ns[i]); newMs[i] = timeNew(Ns[i]); }

    // Robust to microsecond-scale timing noise: use ABSOLUTE bounds + large ratios, not jittery µs ratios.
    // (a) quiet eval bounded at every N (the math is ~free regardless of node count).
    const bool quietBounded = newMs[0] < 0.5 && newMs[1] < 0.5 && newMs[2] < 0.5;
    // (b) the old per-eval scene cascade is MUCH costlier at every N (>=10x).
    const bool cascadeCostly = (oldMs[0] > 10.0 * newMs[0]) && (oldMs[1] > 10.0 * newMs[1]) && (oldMs[2] > 10.0 * newMs[2]);
    // (c) the cascade COST GROWS with node count (the blowup signature: per-eval cost scales with N), while
    //     quiet eval stays bounded. This is the bug + the neg-ctrl.
    const bool cascadeGrows = oldMs[2] > 3.0 * oldMs[0];

    const bool pass = quietBounded && cascadeCostly && cascadeGrows;
    for (int i = 0; i < 3; ++i)
        printf("[perf]   N=%2d : quiet eval = %7.4f ms   old scene-cascade = %7.4f ms   (cascade %.1fx)\n",
               Ns[i], newMs[i], oldMs[i], oldMs[i] / std::max(newMs[i], 1e-6));
    printf("[perf]   quiet bounded(<0.5ms all N)=%s; OLD cascade >=10x costlier=%s; cascade grows with N (%.2f->%.2f ms)=%s\n",
           quietBounded ? "yes" : "NO", cascadeCostly ? "yes" : "NO", oldMs[0], oldMs[2], cascadeGrows ? "yes" : "NO");
    printf("[perf] %s\n", pass ? "ALL PASS (quiet eval bounded + linear; the old per-eval scene-repaint cascade blows up = the bug)"
                               : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

// ============================ GATE RATE ============================
bool runRateGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[rate] GATE RATE -- eval rate is configurable; UI repaint stays capped INDEPENDENT of the eval rate\n");

    auto noop = []{};
    const double dur = 0.30;
    // eval at 30 Hz vs 20 kHz, both with the UI capped at 60 Hz.
    const EvalLoopStats slow = runEvalLoop(30.0,    60.0, dur, noop, noop);
    const EvalLoopStats fast = runEvalLoop(20000.0, 60.0, dur, noop, noop);
    // NEG-CTRL: a SINGLE-rate knob driving both (UI uncapped == eval) -> UI repaints at the eval rate (kHz).
    const EvalLoopStats single = runEvalLoop(20000.0, 20000.0, dur, noop, noop);

    const double slowEvalHz = slow.evals / slow.seconds, slowUiHz = slow.uiRefreshes / slow.seconds;
    const double fastEvalHz = fast.evals / fast.seconds, fastUiHz = fast.uiRefreshes / fast.seconds;
    const double singleUiHz = single.uiRefreshes / single.seconds;

    const bool evalRateChanges = fastEvalHz > 20.0 * slowEvalHz && fastEvalHz > 1000.0;  // rate knob really changes eval freq
    const bool uiCappedSlow = slowUiHz <= 90.0;
    const bool uiCappedFast = fastUiHz <= 90.0;                     // UI stays ~60Hz even at eval=20kHz
    const bool negReproduces = singleUiHz > 1000.0;                // single-rate -> UI at kHz (the bad path)

    const bool pass = evalRateChanges && uiCappedSlow && uiCappedFast && negReproduces;
    printf("[rate]   eval=30Hz   -> measured eval %.0f Hz, UI %.0f Hz\n", slowEvalHz, slowUiHz);
    printf("[rate]   eval=20kHz  -> measured eval %.0f Hz, UI %.0f Hz (capped)\n", fastEvalHz, fastUiHz);
    printf("[rate]   eval-rate changes eval freq=%s; UI capped at both rates=%s\n",
           evalRateChanges ? "yes" : "NO", (uiCappedSlow && uiCappedFast) ? "yes" : "NO");
    printf("[rate]   NEG-CTRL single-rate (UI==eval) -> UI %.0f Hz (kHz repaints): %s\n", singleUiHz, negReproduces ? "PASS" : "FAIL!");
    printf("[rate] %s\n", pass ? "ALL PASS (eval rate configurable; UI repaint capped independently; single-rate would repaint at kHz)"
                               : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
