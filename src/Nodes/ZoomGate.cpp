// ZoomGate.cpp -- GATE ZOOM-VISIBLE: every node's frame is visible at BOTH terminal-zoom bounds (0.3x..2x).
// Bug A: QtNodes' NodeGraphicsObject sets DeviceCoordinateCache + a drop-shadow QGraphicsEffect, both of
// which render the item to an offscreen DEVICE pixmap (size x dpr x zoom). For large nodes that pixmap
// overflows the max pixmap/GL-texture size -> the FRAME paints blank (only the embedded proxy widget shows)
// until you zoom far out. CustomDataFlowScene now forces every node to NoCache + no effect (direct paint).
// This gate asserts that guarantee for every type AND renders the largest nodes ISOLATED at each terminal
// zoom bound, checking the frame's TITLE BAR is actually painted (the strict, non-overlapped pixel test).

#include "Node.hpp"
#include "NodeDelegate.hpp"
#include "NodeFactory.hpp"
#include "NodeEditorGate.hpp"
#include "CustomDataFlowScene.hpp"

#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QtNodes/Definitions>
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QColor>

#include <cstdio>
#include <vector>
#include <string>
#include <algorithm>

namespace krs::nodes {
namespace {
// Render one node (own scene) at `scale` to a magenta-backed image and report whether the frame's TITLE BAR
// strip (top of the node, which the embedded widget never covers) is painted over the backdrop. Uses the
// given scene so the cache/effect state is exactly the live one. Returns the painted fraction in `frac`.
bool frameTitlePainted(QtNodes::BasicGraphicsScene& scene, QtNodes::NodeGraphicsObject* go, double scale, double& frac)
{
    // Render the node's actual FRAME rect (size, no boundingRect margin) so its title bar sits at the top.
    const QSize sz = scene.nodeGeometry().size(go->nodeId());
    const QRectF frameScene = go->mapRectToScene(QRectF(0, 0, sz.width(), sz.height()));
    const int w = std::max(8, int(frameScene.width() * scale));
    const int h = std::max(8, int(frameScene.height() * scale));
    if (qint64(w) * h > 64ll * 1024 * 1024) { frac = -1.0; return false; }  // guard absurd targets
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(255, 0, 255));                             // magenta backdrop -- nothing in the node is magenta
    { QPainter p(&img); p.setRenderHint(QPainter::Antialiasing); scene.render(&p, QRectF(0, 0, w, h), frameScene); }
    const int stripBot = std::min(h, std::max(3, int(16 * scale)));   // the caption-bar band at the node top
    long total = 0, painted = 0;
    for (int y = 1; y < stripBot; ++y)
        for (int x = w / 4; x < 3 * w / 4; ++x) {
            const QRgb px = img.pixel(x, y); ++total;
            if (qRed(px) < 240 || qGreen(px) > 20 || qBlue(px) < 240) ++painted;   // not the magenta backdrop
        }
    frac = total ? double(painted) / total : 0.0;
    return frac > 0.30;                                         // the title bar is painted over the backdrop
}
} // namespace

bool runZoomVisibilityGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[zoom] GATE ZOOM-VISIBLE -- every node frame visible at BOTH terminal-zoom bounds (no offscreen-pixmap overflow)\n");
    if (!QApplication::instance()) { printf("[zoom] FAIL: needs QApplication\n"); return false; }

    // (1) GUARANTEE: every node (built through CustomDataFlowScene) is NoCache + has NO drop-shadow effect,
    //     so NO offscreen device pixmap exists to overflow at any zoom.
    int M = 0, guaranteed = 0;
    std::vector<std::string> bad;
    {
        auto model = makeNodeGraphModel();
        CustomDataFlowScene scene(*model);
        for (const auto& kv : NodeFactory::instance().getRegisteredNodeTypes()) {
            ++M;
            const QtNodes::NodeId id = model->addNode(QString::fromStdString(kv.first));
            auto* go = id != QtNodes::InvalidNodeId ? scene.nodeGraphicsObject(id) : nullptr;
            if (go && go->cacheMode() == QtNodes::NodeGraphicsObject::NoCache && go->graphicsEffect() == nullptr) ++guaranteed;
            else bad.push_back(kv.first);
        }
    }

    // (2) PIXEL at the terminal-zoom bounds: render the LARGEST nodes isolated at 0.3x and 2.0x and assert
    //     the frame title bar is painted (the strict, non-overlapped pixel measurement the operator asked for).
    int pix = 0, pixN = 0;
    const char* bigNodes[] = { "gen_sine", "control_pid", "ctrl_goal_knob", "viz_dial_gauge", "math_add" };
    const double zooms[] = { 0.3, 2.0 };
    for (const char* t : bigNodes)
        for (double z : zooms) {
            ++pixN;
            auto m = makeNodeGraphModel();
            CustomDataFlowScene sc(*m);
            const QtNodes::NodeId id = m->addNode(t);
            auto* go = sc.nodeGraphicsObject(id);
            double frac = 0.0;
            const bool okPx = go && frameTitlePainted(sc, go, z, frac);
            if (okPx) ++pix;
            printf("[zoom]   %-16s @ %.1fx zoom -> title painted %.0f%%  %s\n", t, z, frac * 100.0, okPx ? "ok" : "FAIL");
        }

    // NEG-CTRL: an UN-FIXED node (a base DataFlowGraphicsScene, the QtNodes default) carries the
    // overflow-prone DeviceCoordinateCache + a drop-shadow effect -- exactly the Bug-A condition the fix removes.
    bool negOk = false;
    {
        auto model = makeNodeGraphModel();
        QtNodes::DataFlowGraphicsScene base(*model);     // NO fix applied
        const QtNodes::NodeId id = model->addNode("gen_sine");
        auto* go = base.nodeGraphicsObject(id);
        const bool hasCache = go && go->cacheMode() == QtNodes::NodeGraphicsObject::DeviceCoordinateCache;
        const bool hasEffect = go && go->graphicsEffect() != nullptr;
        negOk = hasCache && hasEffect;
        printf("[zoom]   NEG-CTRL un-fixed node: DeviceCoordinateCache=%s drop-shadow effect=%s (the Bug-A overflow condition)\n",
               hasCache ? "yes" : "no", hasEffect ? "yes" : "no");
    }

    const bool pass = (M > 0) && (guaranteed == M) && (pix == pixN) && negOk;
    printf("[zoom]   GUARANTEE: %d/%d node types are NoCache + no offscreen effect (no device pixmap to overflow)\n", guaranteed, M);
    if (!bad.empty()) { printf("[zoom]   NOT GUARANTEED: "); for (auto& s : bad) printf("%s ", s.c_str()); printf("\n"); }
    printf("[zoom]   PIXEL: %d/%d (largest nodes' frame title painted at both terminal-zoom bounds 0.3x and 2.0x)\n", pix, pixN);
    printf("[zoom] %s (live on-screen zoom = OPERATOR VISUAL-CONFIRM)\n",
           pass ? "ALL PASS (every node renders its frame directly at any zoom; no offscreen-pixmap overflow)" : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
