// DroppableGraphicsView.hpp

#pragma once

#include <QtNodes/GraphicsView> // Include the correct header
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>

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

    // The base GraphicsView pans the canvas on left-drag (ScrollHandDrag). That silently swallowed
    // clicks on embedded node controls -- combo boxes especially looked "dead". When a left-press lands
    // on an embedded control (a QGraphicsProxyWidget hosting the combo/spinbox), suppress the pan for
    // that interaction so the click reaches the widget; restore panning on release. Empty-canvas drags
    // still pan; node bodies still drag-move.
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    bool m_panSuppressed = false;
};