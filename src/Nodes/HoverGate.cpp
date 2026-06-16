// HoverGate.cpp -- GATE HOVER-INTEGRITY (the post-hover-EVENT state layer FRAME-GFX missed). The two hover
// bugs share ONE root cause: WA_TranslucentBackground on the embedded container made the QGraphicsProxyWidget
// composite badly on the hover-triggered repaint (Bug A: the node frame fill was alpha-eaten and not restored
// on leave; Bug B: child combos did not render until a hover repaint). This gate asserts, for every node
// type, that the container does NOT carry that frame-eating attribute and that the exec-mode control is
// visible -- before AND after a synthetic hoverEnter/Leave (hover-independent). NEG-CTRL: a
// WA_TranslucentBackground container and a hidden combo are both caught. Live hover = OPERATOR VISUAL-CONFIRM.

#include "Node.hpp"
#include "NodeDelegate.hpp"
#include "NodeFactory.hpp"
#include "NodeEditorGate.hpp"
#include "CustomDataFlowScene.hpp"

#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QtNodes/Definitions>
#include <QApplication>
#include <QWidget>
#include <QComboBox>
#include <QGraphicsSceneHoverEvent>

#include <cstdio>
#include <vector>
#include <string>

namespace krs::nodes {
namespace {
// The Execution-Mode (policy) combo, identified by its "Continuous" item -- always-visible by design.
QComboBox* policyCombo(QWidget* container) {
    if (!container) return nullptr;
    for (QComboBox* c : container->findChildren<QComboBox*>())
        for (int i = 0; i < c->count(); ++i)
            if (c->itemText(i) == QLatin1String("Continuous")) return c;
    return nullptr;
}
// The integrity predicate at the current (post-event) state: NO widget in the node body (the container OR
// any descendant, e.g. a custom GaugeWidget) may carry the frame-eating WA_TranslucentBackground, and the
// policy combo (if the node has one) must be visible.
bool hoverIntegrityOk(QWidget* container, QComboBox* combo) {
    if (!container) return false;
    if (container->testAttribute(Qt::WA_TranslucentBackground)) return false;   // Bug A root cause (container)
    // ... or any IN-BODY descendant (a custom-paint widget like the gauge). Restrict to widgets in the SAME
    // top-level as the container so Qt-internal detached popups (a combo's drop-shadow popup + its view,
    // legitimately translucent and shown only on click) are not false positives.
    QWidget* topw = container->window();
    for (QWidget* w : container->findChildren<QWidget*>())
        if (w->window() == topw && w->testAttribute(Qt::WA_TranslucentBackground))
            return false;
    if (combo && !combo->isVisible()) return false;                             // Bug B: exec control hidden
    return true;
}
} // namespace

bool runHoverIntegrityGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[hover] GATE HOVER-INTEGRITY -- frame background + exec-mode control survive hover-enter AND hover-leave\n");
    if (!QApplication::instance()) { printf("[hover] FAIL: needs QApplication\n"); return false; }

    auto model = makeNodeGraphModel();
    CustomDataFlowScene scene(*model);

    int M = 0, ok = 0;
    std::vector<std::string> failures;
    for (const auto& kv : NodeFactory::instance().getRegisteredNodeTypes()) {
        ++M;
        const QtNodes::NodeId id = model->addNode(QString::fromStdString(kv.first));
        QtNodes::NodeGraphicsObject* go = id != QtNodes::InvalidNodeId ? scene.nodeGraphicsObject(id) : nullptr;
        auto* del = model->delegateModel<NodeDelegate>(id);
        QWidget* container = del ? del->embeddedWidget() : nullptr;
        QComboBox* combo = policyCombo(container);
        if (!go || !container) { failures.push_back(kv.first + "(no-go/container)"); continue; }

        const bool atRest = hoverIntegrityOk(container, combo);          // (1) at rest
        // (2) synthetic hover-enter (raises z, sets hovered, update()); re-assert
        QGraphicsSceneHoverEvent enter(QEvent::GraphicsSceneHoverEnter);
        scene.sendEvent(go, &enter);
        const bool afterEnter = hoverIntegrityOk(container, combo);
        // (3) synthetic hover-leave; re-assert
        QGraphicsSceneHoverEvent leave(QEvent::GraphicsSceneHoverLeave);
        scene.sendEvent(go, &leave);
        const bool afterLeave = hoverIntegrityOk(container, combo);

        if (atRest && afterEnter && afterLeave) ++ok;
        else failures.push_back(kv.first + "(rest=" + (atRest?"y":"n") + " enter=" + (afterEnter?"y":"n") + " leave=" + (afterLeave?"y":"n") + ")");
    }

    // NEG-CTRL A: a container WITH WA_TranslucentBackground (the frame-eating attribute = the OLD code) is
    // flagged by the predicate. NEG-CTRL B: a hidden policy combo is flagged.
    bool negA = false, negB = false;
    {
        QWidget translucent; translucent.setAttribute(Qt::WA_TranslucentBackground, true);
        negA = !hoverIntegrityOk(&translucent, nullptr);                // translucent -> NOT ok (caught)
        QWidget plain; QComboBox hidden(&plain); hidden.addItem("Continuous"); hidden.setVisible(false);
        negB = !hoverIntegrityOk(&plain, &hidden);                      // hidden combo -> NOT ok (caught)
    }

    const bool pass = (M > 0) && (ok == M) && negA && negB;
    printf("[hover]   coverage: %d/%d node types keep frame-background (no WA_TranslucentBackground) + visible exec control across hover-enter/leave\n", ok, M);
    if (!failures.empty()) { printf("[hover]   FAILURES: "); for (auto& s : failures) printf("%s ", s.c_str()); printf("\n"); }
    printf("[hover]   NEG-CTRL: WA_TranslucentBackground container caught=%s; hidden exec combo caught=%s\n",
           negA ? "yes" : "NO", negB ? "yes" : "NO");
    printf("[hover] %s (live hover persistence = OPERATOR VISUAL-CONFIRM)\n",
           pass ? "ALL PASS (frame background + exec control survive hover-enter AND hover-leave; one root cause fixed)" : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
