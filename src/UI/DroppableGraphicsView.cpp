// DroppableGraphicsView.cpp
#include "DroppableGraphicsView.hpp"
#include "NodeEditorGate.hpp"               // krs::nodes::instanceDroppedNode (shared drop instancing)
#include <QtNodes/BasicGraphicsScene>      // Use this specific scene type
#include <QtNodes/AbstractGraphModel>

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