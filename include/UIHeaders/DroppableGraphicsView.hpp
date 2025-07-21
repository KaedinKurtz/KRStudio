// DroppableGraphicsView.hpp

#pragma once

#include <QtNodes/GraphicsView> // Include the correct header
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

class DroppableGraphicsView : public QtNodes::GraphicsView
{
    Q_OBJECT
public:
    // The constructor should take a QtNodes::BasicGraphicsScene*
    explicit DroppableGraphicsView(QtNodes::BasicGraphicsScene* scene, QWidget* parent = nullptr)
        : QtNodes::GraphicsView(scene, parent) // Pass the scene to the correct base class
    {
        setAcceptDrops(true);
    }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasText()) {
            event->acceptProposedAction();
        }
    }

    void dragMoveEvent(QDragMoveEvent* event) override {
        event->acceptProposedAction();
    }

    void dropEvent(QDropEvent* event) override;
};