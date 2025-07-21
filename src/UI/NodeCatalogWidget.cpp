#include "NodeCatalogWidget.hpp"
#include "NodeFactory.hpp"

#include <QVBoxLayout>
#include <QTreeWidgetItem>
#include <QMouseEvent>      // Required for mouse events
#include <QDrag>            // Required for QDrag
#include <QMimeData>        // Required for QMimeData
#include <QApplication>     // Required for startDragDistance()
#include <QLabel>           // Used to create the drag visual
#include <QPixmap>          // Used for the drag visual

// Store the starting position of a potential drag
static QPoint startPos;

NodeCatalogWidget::NodeCatalogWidget(QWidget* parent)
    : QWidget(parent)
    , m_searchBar(new QLineEdit(this))
    , m_tree(new QTreeWidget(this))
{
    m_searchBar->setPlaceholderText("Search nodes...");

    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(false);
    //
    // 1. DISABLE the default drag-and-drop. We will handle it ourselves.
    //
    m_tree->setDragEnabled(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_searchBar);
    layout->addWidget(m_tree);
    setLayout(layout);

    connect(m_searchBar, &QLineEdit::textChanged,
        this, &NodeCatalogWidget::onFilterTextChanged);

    populateTree();
}

//
// 2. Override mouse events to manually control the drag process.
//
void NodeCatalogWidget::mousePressEvent(QMouseEvent* event)
{
    // We only care about the left mouse button to start a drag.
    if (event->button() == Qt::LeftButton) {
        startPos = event->pos();
    }
    QWidget::mousePressEvent(event);
}

void NodeCatalogWidget::mouseMoveEvent(QMouseEvent* event)
{
    // Check if the left button is pressed and if the mouse has moved
    // far enough to be considered a drag.
    if (!(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    if ((event->pos() - startPos).manhattanLength() < QApplication::startDragDistance()) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    // Get the tree item under the cursor.
    QTreeWidgetItem* item = m_tree->itemAt(startPos - m_tree->pos());
    if (!item) {
        return; // Not on an item.
    }

    // Get the unique ID from the item's data.
    QString typeId = item->data(0, Qt::UserRole).toString();
    if (typeId.isEmpty()) {
        return; // This is a category header, not a draggable node.
    }

    qDebug() << "Attempting to start drag for typeId:" << typeId;
    // A. Create the MimeData with the correct typeId.
    auto* mimeData = new QMimeData();
    mimeData->setText(typeId);

    // B. Create the visual element for the drag (The Answer to your 2nd Question!)
    QLabel* dragLabel = new QLabel(item->text(0)); // Create a label with the node's name.
    dragLabel->setAutoFillBackground(true);
    dragLabel->setFrameShape(QFrame::Panel);
    dragLabel->setFrameShadow(QFrame::Raised);
    dragLabel->setMargin(4);
    QPixmap pixmap = dragLabel->grab(); // Render the label to a pixmap.
    delete dragLabel; // Clean up the temporary label.


    // 3. Create and execute the QDrag object.
    auto* drag = new QDrag(this);
    drag->setMimeData(mimeData);
    drag->setPixmap(pixmap); // Set our custom visual.
    drag->setHotSpot({ pixmap.width() / 2, pixmap.height() / 2 }); // Set cursor to the center of the pixmap.

    // Start the drag!
    drag->exec(Qt::CopyAction);
}


// --- The rest of the file is unchanged ---
void NodeCatalogWidget::populateTree()
{
    m_tree->clear();

    // 1) fetch your descriptors
    auto types = NodeFactory::instance().getRegisteredNodeTypes();

    // 2) group by category
    std::map<QString, QTreeWidgetItem*> catHeaders;
    for (auto& [typeId, desc] : types)
    {
        QString cat = QString::fromStdString(desc.category);
        if (!catHeaders.count(cat))
        {
            auto* hdr = new QTreeWidgetItem(m_tree);
            hdr->setText(0, cat);
            hdr->setFlags(Qt::NoItemFlags);
            QFont f = hdr->font(0);
            f.setBold(true);
            hdr->setFont(0, f);
            catHeaders[cat] = hdr;
        }

        auto* item = new QTreeWidgetItem(catHeaders[cat]);
        item->setText(0, QString::fromStdString(desc.aui_name));
        item->setToolTip(0, QString::fromStdString(desc.description));
        item->setData(0, Qt::UserRole, QString::fromStdString(typeId));
        item->setFlags(Qt::ItemIsEnabled |
            Qt::ItemIsSelectable); // Remove Qt::ItemIsDragEnabled
    }

    m_tree->expandAll();
}

void NodeCatalogWidget::onFilterTextChanged(const QString& txt)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
    {
        auto* hdr = m_tree->topLevelItem(i);
        bool anyVisible = false;
        for (int j = 0; j < hdr->childCount(); ++j)
        {
            auto* child = hdr->child(j);
            bool match = child->text(0).contains(txt, Qt::CaseInsensitive)
                || hdr->text(0).contains(txt, Qt::CaseInsensitive);
            child->setHidden(!match);
            anyVisible |= match;
        }
        hdr->setHidden(!anyVisible);
    }
}