#include "ManufacturerPartsPanel.hpp"

#include "Scene.hpp"
#include "KParts.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QFrame>
#include <QPushButton>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>

namespace {
// A tree that starts a part-file drag with the "application/x-krstudio-asset" mime (the viewport +
// joint drop targets already accept it), carrying the selected item's absolute path.
class PartTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
protected:
    void startDrag(Qt::DropActions) override {
        QTreeWidgetItem* it = currentItem();
        if (!it) return;
        const QString path = it->data(0, Qt::UserRole).toString();
        if (path.isEmpty()) return;                    // a category header, not a part
        auto* mime = new QMimeData();
        mime->setData(QStringLiteral("application/x-krstudio-asset"), path.toUtf8());
        mime->setText(path);
        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
    }
};
} // namespace

ManufacturerPartsPanel::ManufacturerPartsPanel(Scene* scene, QWidget* parent)
    : QWidget(parent), m_scene(scene)
{
    setStyleSheet(QStringLiteral(
        "QWidget{background:#2c313a;color:#e6e6e6;}"
        "QGroupBox{border:1px solid #4a5260;margin-top:6px;}"
        "QPushButton{background:#262b33;border:1px solid #4a5260;padding:4px;}"
        "QPushButton:hover{background:#323844;}"
        "QTreeWidget,QLineEdit{background:#262b33;border:1px solid #4a5260;}"));
    // The local content-addressed store lives under the user app-data dir.
    const QString store = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                              .filePath(QStringLiteral("part_repo"));
    m_repo = krs::parts::makeLocalRepository(store);
    initializeUI();
    rescan();
}

ManufacturerPartsPanel::~ManufacturerPartsPanel() = default;

void ManufacturerPartsPanel::initializeUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* title = new QLabel(QStringLiteral("Manufacturer Parts"), this);
    title->setStyleSheet(QStringLiteral("font-weight:bold;padding:2px;"));
    layout->addWidget(title);

    m_status = new QLabel(QStringLiteral("Scanning..."), this);
    m_status->setObjectName(QStringLiteral("mpStatusLabel"));
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color:#9fb4cc;padding:2px;"));
    layout->addWidget(m_status);

    m_tree = new PartTree(this);
    m_tree->setObjectName(QStringLiteral("mpPartsTree"));
    m_tree->setHeaderLabels({ QStringLiteral("Part"), QStringLiteral("Manufacturer") });
    m_tree->header()->setStretchLastSection(true);
    m_tree->setDragEnabled(true);
    m_tree->setDragDropMode(QAbstractItemView::DragOnly);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setToolTip(QStringLiteral("Drag a part onto a joint (actuator) or the scene. "
                                      "Right column is the manufacturer."));
    layout->addWidget(m_tree, 1);

    m_rescanBtn = new QPushButton(QStringLiteral("Rescan Library"), this);
    m_rescanBtn->setObjectName(QStringLiteral("mpRescanButton"));
    layout->addWidget(m_rescanBtn);
    connect(m_rescanBtn, &QPushButton::clicked, this, &ManufacturerPartsPanel::rescan);

    // --- Repository / cloud contribution ---
    auto* repoBox = new QGroupBox(QStringLiteral("Repository (local cache + cloud)"), this);
    auto* rl = new QVBoxLayout(repoBox);
    m_repoStatus = new QLabel(this);
    m_repoStatus->setObjectName(QStringLiteral("mpRepoStatus"));
    m_repoStatus->setWordWrap(true);
    m_repoStatus->setStyleSheet(QStringLiteral("color:#9fb4cc;"));
    rl->addWidget(m_repoStatus);
    auto* form = new QFormLayout();
    m_remoteUrl = new QLineEdit(this);
    m_remoteUrl->setObjectName(QStringLiteral("mpRemoteUrl"));
    m_remoteUrl->setPlaceholderText(QStringLiteral("https://parts.krstudio.io"));
    m_remoteKey = new QLineEdit(this);
    m_remoteKey->setObjectName(QStringLiteral("mpRemoteKey"));
    m_remoteKey->setEchoMode(QLineEdit::Password);
    m_remoteKey->setPlaceholderText(QStringLiteral("API key"));
    form->addRow(QStringLiteral("Server"), m_remoteUrl);
    form->addRow(QStringLiteral("API key"), m_remoteKey);
    rl->addLayout(form);
    auto* btns = new QHBoxLayout();
    m_publishBtn = new QPushButton(QStringLiteral("Publish Selected"), this);
    m_publishBtn->setObjectName(QStringLiteral("mpPublishButton"));
    m_syncBtn = new QPushButton(QStringLiteral("Sync"), this);
    m_syncBtn->setObjectName(QStringLiteral("mpSyncButton"));
    btns->addWidget(m_publishBtn);
    btns->addWidget(m_syncBtn);
    rl->addLayout(btns);
    layout->addWidget(repoBox);
    connect(m_publishBtn, &QPushButton::clicked, this, &ManufacturerPartsPanel::onPublishSelected);
    connect(m_syncBtn,    &QPushButton::clicked, this, &ManufacturerPartsPanel::onSync);

    refreshRepoStatus();
}

void ManufacturerPartsPanel::setSceneDir(const QString& sceneDir)
{
    m_sceneDir = sceneDir;
    rescan();
}

void ManufacturerPartsPanel::rescan()
{
    auto& lib = krs::parts::library();
    lib.setSearchRoots(krs::parts::standardSearchRoots(m_sceneDir));
    const int n = lib.rescan();

    m_tree->clear();
    // Group by type; a category header per non-empty type.
    const krs::parts::PartType order[] = {
        krs::parts::PartType::Actuator, krs::parts::PartType::Motor, krs::parts::PartType::Gearbox,
        krs::parts::PartType::Encoder, krs::parts::PartType::Camera, krs::parts::PartType::Imu,
        krs::parts::PartType::Material };
    for (krs::parts::PartType t : order) {
        auto parts = lib.byType(t);
        if (parts.empty()) continue;
        auto* cat = new QTreeWidgetItem(m_tree, { QString::fromLatin1(krs::parts::displayName(t)) + QStringLiteral("s") });
        cat->setFirstColumnSpanned(true);
        cat->setFlags(cat->flags() & ~Qt::ItemIsDragEnabled);
        for (const auto& e : parts) {
            auto* it = new QTreeWidgetItem(cat, { e.name, e.manufacturer });
            it->setData(0, Qt::UserRole, e.absPath);   // the drag payload
            it->setToolTip(0, e.absPath + QStringLiteral("\nsource: ") + e.source);
        }
        cat->setExpanded(true);
    }
    if (m_status) {
        m_status->setText(n == 0
            ? QStringLiteral("No parts found. Search path: <scene>/parts, <project>/parts, <user>/parts. "
                             "Drop .kmotor/.kactuator/.kcamera/.kimu files there.")
            : QStringLiteral("%1 part(s) indexed. Drag onto a joint or the scene.").arg(n));
    }
    refreshRepoStatus();
}

void ManufacturerPartsPanel::refreshRepoStatus()
{
    if (!m_repoStatus || !m_repo) return;
    const krs::parts::RepoStatus s = m_repo->status();
    m_repoStatus->setText(QStringLiteral("Local store: %1 part(s). Cloud: %2")
        .arg(s.localParts)
        .arg(m_remoteUrl && !m_remoteUrl->text().isEmpty()
                 ? QStringLiteral("not built in (offline stub) -- %1").arg(m_remoteUrl->text())
                 : QStringLiteral("no server configured")));
}

void ManufacturerPartsPanel::onPublishSelected()
{
    QTreeWidgetItem* it = m_tree ? m_tree->currentItem() : nullptr;
    const QString path = it ? it->data(0, Qt::UserRole).toString() : QString();
    if (path.isEmpty()) { if (m_status) m_status->setText(QStringLiteral("Select a part to publish.")); return; }
    const krs::parts::PartEntry* e = krs::parts::library().findByPath(path);
    if (!e) { if (m_status) m_status->setText(QStringLiteral("Part not in the index -- rescan.")); return; }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (m_status) m_status->setText(QStringLiteral("Cannot read the part file.")); return; }
    krs::parts::PartBlob blob; blob.meta = *e; blob.bytes = f.readAll();

    // Publish to the LOCAL store always (the offline cache); the remote stub reports offline.
    QString err;
    const bool localOk = m_repo->publish(blob, &err);
    QString msg = localOk ? QStringLiteral("Published \"%1\" to the local store.").arg(e->name)
                          : QStringLiteral("Local publish failed: %1").arg(err);
    if (m_remoteUrl && !m_remoteUrl->text().isEmpty()) {
        auto remote = krs::parts::makeRemoteRepository(m_remoteUrl->text(), m_remoteKey ? m_remoteKey->text() : QString());
        QString rerr;
        remote->publish(blob, &rerr);
        msg += QStringLiteral("  Cloud: %1").arg(rerr);   // stub: honest offline message
    }
    if (m_status) m_status->setText(msg);
    refreshRepoStatus();
}

void ManufacturerPartsPanel::onSync()
{
    QString msg;
    if (m_remoteUrl && !m_remoteUrl->text().isEmpty()) {
        auto remote = krs::parts::makeRemoteRepository(m_remoteUrl->text(), m_remoteKey ? m_remoteKey->text() : QString());
        QString err;
        remote->sync(&err);
        msg = QStringLiteral("Sync: %1").arg(err);
    } else {
        msg = QStringLiteral("Local store only (%1 parts). Set a server URL to sync to the cloud.")
                  .arg(m_repo ? m_repo->status().localParts : 0);
    }
    if (m_status) m_status->setText(msg);
    refreshRepoStatus();
}
