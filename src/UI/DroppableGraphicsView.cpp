// DroppableGraphicsView.cpp
#include "DroppableGraphicsView.hpp"
#include "NodeEditorGate.hpp"               // krs::nodes::instanceDroppedNode (shared drop instancing)
#include <QtNodes/BasicGraphicsScene>      // Use this specific scene type
#include <QtNodes/AbstractGraphModel>
#include <QGraphicsProxyWidget>

namespace krs::nodes {
// The shared catalog-drop instancing path -- also exercised headless by GATE DRAGDROP.
QtNodes::NodeId instanceDroppedNode(QtNodes::AbstractGraphModel& model, const QString& typeId,
                                    double sceneX, double sceneY)
{
    if (typeId.isEmpty()) return QtNodes::InvalidNodeId;
    const QtNodes::NodeId id = model.addNode(typeId);
    if (id != QtNodes::InvalidNodeId)
        model.setNodeData(id, QtNodes::NodeRole::Position, QPointF(sceneX, sceneY));
    return id;
}
} // namespace krs::nodes

void DroppableGraphicsView::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasText()) { event->ignore(); return; }

    auto* nodeScene = qobject_cast<QtNodes::BasicGraphicsScene*>(scene());
    if (!nodeScene) { event->ignore(); return; }

    const QPointF posScene = mapToScene(event->pos());
    const QtNodes::NodeId id = krs::nodes::instanceDroppedNode(
        nodeScene->graphModel(), event->mimeData()->text(), posScene.x(), posScene.y());

    if (id != QtNodes::InvalidNodeId) event->acceptProposedAction();
    else                             event->ignore();
}

void DroppableGraphicsView::mousePressEvent(QMouseEvent* event)
{
    // If the left-press lands on an embedded control (a proxy widget hosting a node's combo/spinbox),
    // suppress the ScrollHandDrag pan for this interaction so the click reaches the widget -- otherwise
    // the base view starts hand-scrolling and the combo never opens. itemAt() returns the topmost item
    // under the cursor; embedded controls are QGraphicsProxyWidget children stacked above the node frame.
    if (event->button() == Qt::LeftButton && dragMode() == QGraphicsView::ScrollHandDrag
        && dynamic_cast<QGraphicsProxyWidget*>(itemAt(event->pos()))) {
        setDragMode(QGraphicsView::NoDrag);
        m_panSuppressed = true;
    }
    QtNodes::GraphicsView::mousePressEvent(event);
}

void DroppableGraphicsView::mouseReleaseEvent(QMouseEvent* event)
{
    QtNodes::GraphicsView::mouseReleaseEvent(event);
    if (m_panSuppressed) {              // restore canvas panning after the widget interaction
        setDragMode(QGraphicsView::ScrollHandDrag);
        m_panSuppressed = false;
    }
}