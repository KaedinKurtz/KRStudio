// DroppableGraphicsView.cpp
#include "DroppableGraphicsView.hpp"
#include <QtNodes/BasicGraphicsScene>      // Use this specific scene type
#include <QtNodes/AbstractGraphModel>

void DroppableGraphicsView::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    const QString typeId = event->mimeData()->text();
    const QPointF posScene = mapToScene(event->pos());

    // CORRECTED: Cast the generic scene() pointer to the specific type
    auto* nodeScene = qobject_cast<QtNodes::BasicGraphicsScene*>(scene());
    if (!nodeScene)
    {
        event->ignore();
        return;
    }

    // Now you can safely access graphModel()
    auto& model = nodeScene->graphModel();

    auto nodeId = model.addNode(typeId);

    if (nodeId != QtNodes::InvalidNodeId)
    {
        model.setNodeData(nodeId, QtNodes::NodeRole::Position, posScene);
        event->acceptProposedAction();
    }
    else
    {
        event->ignore();
    }
}