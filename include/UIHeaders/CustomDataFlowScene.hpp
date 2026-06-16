#pragma once

#include <QtNodes/DataFlowGraphicsScene>

// This class inherits from the DataFlowGraphicsScene to override
// its right-click menu behavior.
namespace QtNodes { class DataFlowGraphModel; }

class CustomDataFlowScene : public QtNodes::DataFlowGraphicsScene
{
    Q_OBJECT

public:
    // Every node's NodeGraphicsObject is forced to NoCache + no drop-shadow effect on creation (see .cpp):
    // QtNodes' DeviceCoordinateCache + QGraphicsDropShadowEffect render the item to an offscreen device
    // pixmap that overflows for large nodes at zoom -> a blank frame until you zoom out. This direct-paint
    // override makes every node's frame visible at any zoom.
    explicit CustomDataFlowScene(QtNodes::DataFlowGraphModel& model, QObject* parent = nullptr);

protected:
    // Override the context menu event handler.
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;
};