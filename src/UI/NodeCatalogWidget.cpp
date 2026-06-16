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

NodeCatalogWidget::NodeCatalogWidget(QWidget* parent)
    : QWidget(parent)
    , m_searchBar(new QLineEdit(this))
    , m_tree(new QTreeWidget(this))
{
    m_searchBar->setPlaceholderText("Search nodes...");

    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(false);
    // Handle drag-and-drop ourselves: the tree's own DnD is disabled and we filter the viewport's mouse
    // events (the tree consumes them, so the parent's mouse overrides would never fire).
    m_tree->setDragEnabled(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->viewport()->installEventFilter(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_searchBar);
    layout->addWidget(m_tree);
    setLayout(layout);

    connect(m_searchBar, &QLineEdit::textChanged,
        this, &NodeCatalogWidget::onFilterTextChanged);

    populateTree();
}

bool NodeCatalogWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_tree->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) m_dragStart = me->pos();
        } else if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            if ((me->buttons() & Qt::LeftButton) &&
                (me->pos() - m_dragStart).manhattanLength() >= QApplication::startDragDistance()) {
                startNodeDrag(m_dragStart);
            }
        }
    }
    return QWidget::eventFilter(obj, event);   // don't consume: the tree still does selection
}

void NodeCatalogWidget::startNodeDrag(const QPoint& viewportPos)
{
    QTreeWidgetItem* item = m_tree->itemAt(viewportPos);     // viewport coords -> item
    if (!item) return;
    const QString typeId = item->data(0, Qt::UserRole).toString();
    if (typeId.isEmpty()) return;                            // a category header, not a node

    // MimeData carries the node type id the drop handler instances.
    auto* mimeData = new QMimeData();
    mimeData->setText(typeId);

    // A small drag-preview / ghost: the node's name on a raised panel.
    QLabel dragLabel(item->text(0));
    dragLabel.setAutoFillBackground(true);
    dragLabel.setFrameShape(QFrame::Panel);
    dragLabel.setFrameShadow(QFrame::Raised);
    dragLabel.setMargin(4);
    const QPixmap pixmap = dragLabel.grab();

    auto* drag = new QDrag(this);
    drag->setMimeData(mimeData);
    drag->setPixmap(pixmap);
    drag->setHotSpot({ pixmap.width() / 2, pixmap.height() / 2 });
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