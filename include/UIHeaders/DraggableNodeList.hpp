#pragma once

#include <QListWidget>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QApplication>

/**
 * @brief A QListWidget that initiates a drag-and-drop operation
 * containing the unique type_id of a node.
 */
class DraggableNodeList : public QListWidget {
    Q_OBJECT

public:
    explicit DraggableNodeList(QWidget* parent = nullptr) : QListWidget(parent) {}

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            // Store the position where the mouse was pressed.
            m_startDragPos = event->pos();
        }
        QListWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        // Only start a drag if the left mouse button is pressed.
        if (!(event->buttons() & Qt::LeftButton)) {
            return;
        }
        // If the mouse hasn't moved far enough, don't start the drag.
        if ((event->pos() - m_startDragPos).manhattanLength() < QApplication::startDragDistance()) {
            return;
        }

        QListWidgetItem* item = currentItem(); // Get the item being dragged.
        if (!item) {
            return;
        }

        // Retrieve the unique node type ID we stored earlier.
        QString typeId = item->data(Qt::UserRole).toString();
        if (typeId.isEmpty()) {
            return;
        }

        QMimeData* mimeData = new QMimeData;
        // The most important part: set the text of the mime data to our unique ID.
        mimeData->setText(typeId);

        // Create and execute the drag operation.
        QDrag* drag = new QDrag(this);
        drag->setMimeData(mimeData);
        drag->setPixmap(QPixmap(":/icons/node_drag.png")); // Optional: use a custom icon for dragging
        drag->exec(Qt::CopyAction);
    }

private:
    QPoint m_startDragPos; // The position where a drag is initiated.
};