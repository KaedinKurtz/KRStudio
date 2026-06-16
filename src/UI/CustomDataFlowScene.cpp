#include "CustomDataFlowScene.hpp"
#include "NodeFactory.hpp"

#include <QMenu>
#include <QAction>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsItem> // <-- FIX 1
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <map>
#include <QTransform>

CustomDataFlowScene::CustomDataFlowScene(QtNodes::DataFlowGraphModel& model, QObject* parent)
    : QtNodes::DataFlowGraphicsScene(model, parent)
{
    // QtNodes' NodeGraphicsObject ctor sets DeviceCoordinateCache + a drop-shadow QGraphicsEffect. Both
    // render the item to an offscreen DEVICE-coordinate pixmap (size x devicePixelRatio x zoom). For large
    // nodes on a hi-DPI screen that pixmap exceeds the max pixmap/GL-texture size -> the cached/effected
    // paint produces a BLANK frame (only the embedded proxy widget shows) until you zoom far enough out.
    // We connect AFTER the base scene's onNodeCreated has built the graphics object and switch it to a
    // direct, unbounded paint so every node's frame renders at every zoom.
    auto fix = [this](QtNodes::NodeId id) {
        if (QtNodes::NodeGraphicsObject* go = nodeGraphicsObject(id)) {
            go->setCacheMode(QGraphicsItem::NoCache);
            go->setGraphicsEffect(nullptr);
        }
    };
    connect(&model, &QtNodes::AbstractGraphModel::nodeCreated, this, fix);
    // The base scene's constructor already built graphics objects for any nodes PRE-EXISTING in the model
    // (e.g. a loaded/deserialized graph) before this connect was made -- fix those too.
    for (QtNodes::NodeId id : model.allNodeIds()) fix(id);
}

void CustomDataFlowScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
    QMenu menu;
    auto& model = this->graphModel();

    QGraphicsItem* clickedItem = itemAt(event->scenePos(), QTransform());
    if (clickedItem)
    {
        QVariant idVariant = clickedItem->data(0);
        if (idVariant.canConvert<QtNodes::NodeId>())
        {
            QtNodes::NodeId clickedNodeId = idVariant.value<QtNodes::NodeId>();
            auto* deleteAction = menu.addAction("Delete Node");
            connect(deleteAction, &QAction::triggered, [&model, clickedNodeId]() {
                model.deleteNode(clickedNodeId);
                });
            menu.addSeparator();
        }
    }

    std::map<QString, QMenu*> categoryMenus;
    for (const auto& [typeId, desc] : NodeFactory::instance().getRegisteredNodeTypes())
    {
        QString category = QString::fromStdString(desc.category);
        if (categoryMenus.find(category) == categoryMenus.end())
        {
            categoryMenus[category] = menu.addMenu(category);
        }

        auto* addAction = new QAction(QString::fromStdString(desc.aui_name), categoryMenus[category]);
        connect(addAction, &QAction::triggered, [&model, typeId, event]() {
            // FIX 2: Convert std::string to QString here.
            QtNodes::NodeId newNodeId = model.addNode(QString::fromStdString(typeId));
            if (newNodeId != QtNodes::InvalidNodeId)
            {
                model.setNodeData(newNodeId, QtNodes::NodeRole::Position, event->scenePos());
            }
            });
        categoryMenus[category]->addAction(addAction);
    }

    menu.exec(event->screenPos());
}