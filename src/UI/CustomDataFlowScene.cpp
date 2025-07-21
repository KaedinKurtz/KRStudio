#include "CustomDataFlowScene.hpp"
#include "NodeFactory.hpp"

#include <QMenu>
#include <QAction>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsItem> // <-- FIX 1
#include <QtNodes/DataFlowGraphModel>
#include <map>
#include <QTransform>

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