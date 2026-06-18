#pragma once
// ProxyComboBox -- a QComboBox usable inside a QtNodes node body.
//
// A node's embedded controls live on a SINGLE QGraphicsProxyWidget (NodeGraphicsObject::embedQWidget). The
// default QComboBox::showPopup() spawns its dropdown as a separate TOP-LEVEL QWidget whose geometry is computed
// from the combo's native screen rect -- which is meaningless under the QGraphicsView's pan/zoom transform, so
// the popup mis-positions / closes instantly and the operator just sees the flat-hand pan cursor with no menu.
//
// The fix (already proven on-screen for ObjectCombo/PropertyCombo in TwinNodes.cpp) is to override showPopup()
// to run a QMenu at mapToGlobal() -- which routes through the proxy + view, so it lands right under the box and
// QMenu runs its own robust top-level popup. Selecting an action calls setCurrentIndex(i), so currentIndexChanged
// still fires and every existing binding (the node-input literal seed, the exec-mode policy/edge handlers) is
// unchanged. This class centralizes that behaviour for the GENERIC combos (enum input ports + the
// execution-mode "Continuous/Triggered" + edge combos).
#include <QComboBox>
#include <QMenu>
#include <QAction>
#include <QPoint>

class ProxyComboBox : public QComboBox {
public:
    using QComboBox::QComboBox;
    void showPopup() override {
        if (count() == 0) return;
        auto* menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->setMinimumWidth(width());
        for (int i = 0; i < count(); ++i) {
            QAction* a = menu->addAction(itemText(i));
            QObject::connect(a, &QAction::triggered, this, [this, i] { setCurrentIndex(i); });
        }
        menu->popup(mapToGlobal(QPoint(0, height())));   // non-blocking; appears beneath the combo
    }
};
