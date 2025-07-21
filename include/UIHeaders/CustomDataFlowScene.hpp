#pragma once

#include <QtNodes/DataFlowGraphicsScene>

// This class inherits from the DataFlowGraphicsScene to override
// its right-click menu behavior.
class CustomDataFlowScene : public QtNodes::DataFlowGraphicsScene
{
    Q_OBJECT

public:
    // Inherit the constructor from the base class.
    using QtNodes::DataFlowGraphicsScene::DataFlowGraphicsScene;

protected:
    // Override the context menu event handler.
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;
};