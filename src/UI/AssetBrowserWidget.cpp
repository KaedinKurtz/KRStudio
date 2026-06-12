#include "AssetBrowserWidget.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QToolBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QDirIterator>
#include <QMimeData>
#include <QDrag>
#include <QApplication>
#include <QStyle>
#include <QFileInfo>

namespace {
const QStringList kMeshGlobs = { "*.stl", "*.obj", "*.fbx", "*.dae", "*.ply",
                                 "*.gltf", "*.glb", "*.3ds" };

// QListWidget that packs the asset path into our drag mime type.
class AssetListWidget : public QListWidget
{
public:
    using QListWidget::QListWidget;

protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override
    {
        auto* mime = new QMimeData();
        if (!items.isEmpty()) {
            const QString path = items.first()->data(Qt::UserRole).toString();
            mime->setData(QStringLiteral("application/x-krstudio-asset"), path.toUtf8());
            mime->setText(path);
        }
        return mime;
    }
};
} // namespace

AssetBrowserWidget::AssetBrowserWidget(const QString& assetsRoot, QWidget* parent)
    : QWidget(parent), m_root(assetsRoot)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* row = new QHBoxLayout();
    auto* importBtn = new QPushButton(QStringLiteral("Import…"), this);
    importBtn->setToolTip(QStringLiteral("Copy an external mesh into the assets directory"));
    auto* spawnBtn = new QPushButton(QStringLiteral("Add to Scene"), this);
    auto* deleteBtn = new QPushButton(QStringLiteral("Delete"), this);
    auto* refreshBtn = new QPushButton(QStringLiteral("Refresh"), this);
    row->addWidget(importBtn);
    row->addWidget(spawnBtn);
    row->addWidget(deleteBtn);
    row->addWidget(refreshBtn);
    row->addStretch();
    layout->addLayout(row);

    m_list = new AssetListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(48, 48));
    m_list->setGridSize(QSize(96, 84));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setWordWrap(true);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    layout->addWidget(m_list, 1);

    m_info = new QLabel(this);
    m_info->setStyleSheet("color: palette(mid); font-size: 8pt;");
    layout->addWidget(m_info);

    connect(importBtn, &QPushButton::clicked, this, [this]() { importMesh(); });
    connect(deleteBtn, &QPushButton::clicked, this, [this]() { deleteSelected(); });
    connect(refreshBtn, &QPushButton::clicked, this, &AssetBrowserWidget::refresh);
    connect(spawnBtn, &QPushButton::clicked, this, [this]() {
        const QString path = selectedPath();
        if (!path.isEmpty()) emit spawnRequested(path);
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        emit spawnRequested(item->data(Qt::UserRole).toString());
    });
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() {
        const QString path = selectedPath();
        if (path.isEmpty()) { m_info->clear(); return; }
        const QFileInfo fi(path);
        m_info->setText(QStringLiteral("%1 — %2 MB. Drag into the viewport or Add to Scene.")
                            .arg(fi.fileName())
                            .arg(fi.size() / (1024.0 * 1024.0), 0, 'f', 1));
    });

    refresh();
}

QString AssetBrowserWidget::selectedPath() const
{
    const auto items = m_list->selectedItems();
    return items.isEmpty() ? QString() : items.first()->data(Qt::UserRole).toString();
}

void AssetBrowserWidget::refresh()
{
    m_list->clear();
    const QIcon icon = style()->standardIcon(QStyle::SP_FileIcon);
    QDirIterator it(m_root, kMeshGlobs, QDir::Files);
    int count = 0;
    while (it.hasNext()) {
        const QString path = it.next();
        auto* item = new QListWidgetItem(icon, QFileInfo(path).fileName(), m_list);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        ++count;
    }
    m_info->setText(QStringLiteral("%1 mesh asset(s) in %2").arg(count).arg(m_root));
}

void AssetBrowserWidget::importMesh()
{
    const QString src = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import Mesh Asset"), QString(),
        QStringLiteral("Meshes (*.stl *.obj *.fbx *.dae *.ply *.gltf *.glb *.3ds);;All files (*)"));
    if (src.isEmpty()) return;
    const QString dst = m_root + QLatin1Char('/') + QFileInfo(src).fileName();
    if (QFileInfo::exists(dst)
        && QMessageBox::question(this, QStringLiteral("Overwrite?"),
                                 QStringLiteral("%1 already exists. Overwrite?")
                                     .arg(QFileInfo(dst).fileName()))
               != QMessageBox::Yes)
        return;
    QFile::remove(dst);
    if (!QFile::copy(src, dst)) {
        QMessageBox::warning(this, QStringLiteral("Import failed"),
                             QStringLiteral("Could not copy to %1").arg(dst));
        return;
    }
    refresh();
}

void AssetBrowserWidget::deleteSelected()
{
    const QString path = selectedPath();
    if (path.isEmpty()) return;
    if (QMessageBox::question(this, QStringLiteral("Delete asset?"),
                              QStringLiteral("Delete %1 from the assets directory? Objects "
                                             "already in the scene keep their mesh.")
                                  .arg(QFileInfo(path).fileName()))
        != QMessageBox::Yes)
        return;
    QFile::remove(path);
    refresh();
}
