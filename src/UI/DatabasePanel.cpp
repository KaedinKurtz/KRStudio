#include "DatabasePanel.hpp"
#include "UtilityHeaders/DatabaseManager.hpp"
#include "UtilityHeaders/DatabaseIndexManager.hpp"
#include "UtilityHeaders/DatabaseReplicationManager.hpp"
#include <QHeaderView>
#include <QMessageBox>
#include <QSqlQueryModel>
#include <QSqlError>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDebug>
#include <QInputDialog>
#include "Scene.hpp"
#include "IMenu.hpp"
#include "MenuFactory.hpp"
using namespace db;

DatabasePanel::DatabasePanel(Scene* scene, QWidget* parent)
    : QWidget(parent), m_scene(scene)
{
    // The constructor is now responsible for building the UI.
    setupUI();
}

DatabasePanel::~DatabasePanel() = default;

void DatabasePanel::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    auto* topLayout = new QHBoxLayout();
    m_sceneCombo = new QComboBox(this);
    m_refreshButton = new QPushButton("Refresh", this);
    m_saveButton = new QPushButton("Save Scene", this);
    m_loadButton = new QPushButton("Load Scene", this);
    m_backupButton = new QPushButton("Backup", this);
    m_restoreButton = new QPushButton("Restore", this);
    m_showEntitiesButton = new QPushButton("Show Entities", this);
    m_showComponentsButton = new QPushButton("Show Components", this);
    topLayout->addWidget(new QLabel("Scene:"));
    topLayout->addWidget(m_sceneCombo);
    topLayout->addWidget(m_refreshButton);
    topLayout->addWidget(m_saveButton);
    topLayout->addWidget(m_loadButton);
    topLayout->addWidget(m_backupButton);
    topLayout->addWidget(m_restoreButton);
    topLayout->addWidget(m_showEntitiesButton);
    topLayout->addWidget(m_showComponentsButton);
    mainLayout->addLayout(topLayout);

    // --- Index Management UI ---
    auto* indexLayout = new QHBoxLayout();
    m_indexNameEdit = new QLineEdit(this); m_indexNameEdit->setPlaceholderText("Index Name");
    m_indexTableEdit = new QLineEdit(this); m_indexTableEdit->setPlaceholderText("Table Name");
    m_indexColumnsEdit = new QLineEdit(this); m_indexColumnsEdit->setPlaceholderText("Columns (comma-separated)");
    m_listIndexesButton = new QPushButton("List Indexes", this);
    m_createIndexButton = new QPushButton("Create Index", this);
    m_dropIndexButton = new QPushButton("Drop Index", this);
    m_rebuildIndexButton = new QPushButton("Rebuild Index", this);
    m_analyzeIndexButton = new QPushButton("Analyze Index", this);
    m_validateIndexButton = new QPushButton("Validate Index", this);
    indexLayout->addWidget(m_indexNameEdit);
    indexLayout->addWidget(m_indexTableEdit);
    indexLayout->addWidget(m_indexColumnsEdit);
    indexLayout->addWidget(m_listIndexesButton);
    indexLayout->addWidget(m_createIndexButton);
    indexLayout->addWidget(m_dropIndexButton);
    indexLayout->addWidget(m_rebuildIndexButton);
    indexLayout->addWidget(m_analyzeIndexButton);
    indexLayout->addWidget(m_validateIndexButton);
    mainLayout->addLayout(indexLayout);

    // --- Replication Management UI ---
    auto* replLayout = new QHBoxLayout();
    m_replicationMasterEdit = new QLineEdit(this); m_replicationMasterEdit->setPlaceholderText("Master DB Path");
    m_enableReplicationButton = new QPushButton("Enable Replication", this);
    m_disableReplicationButton = new QPushButton("Disable Replication", this);
    m_syncReplicationButton = new QPushButton("Sync", this);
    m_showReplicationStatusButton = new QPushButton("Show Status", this);
    replLayout->addWidget(m_replicationMasterEdit);
    replLayout->addWidget(m_enableReplicationButton);
    replLayout->addWidget(m_disableReplicationButton);
    replLayout->addWidget(m_syncReplicationButton);
    replLayout->addWidget(m_showReplicationStatusButton);
    mainLayout->addLayout(replLayout);

    m_tableView = new QTableView(this);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    mainLayout->addWidget(m_tableView);

    auto* queryLayout = new QHBoxLayout();
    m_queryEdit = new QLineEdit(this);
    m_runQueryButton = new QPushButton("Run Query", this);
    queryLayout->addWidget(new QLabel("SQL:"));
    queryLayout->addWidget(m_queryEdit);
    queryLayout->addWidget(m_runQueryButton);
    mainLayout->addLayout(queryLayout);

    m_queryResult = new QTextEdit(this);
    m_queryResult->setReadOnly(true);
    mainLayout->addWidget(m_queryResult);

    m_statusLabel = new QLabel(this);
    mainLayout->addWidget(m_statusLabel);

    connect(m_refreshButton, &QPushButton::clicked, this, &DatabasePanel::onRefreshScenes);
    connect(m_sceneCombo, &QComboBox::currentTextChanged, this, &DatabasePanel::onSceneSelected);
    connect(m_saveButton, &QPushButton::clicked, this, &DatabasePanel::onSaveScene);
    connect(m_loadButton, &QPushButton::clicked, this, &DatabasePanel::onLoadScene);
    connect(m_backupButton, &QPushButton::clicked, this, &DatabasePanel::onBackup);
    connect(m_restoreButton, &QPushButton::clicked, this, &DatabasePanel::onRestore);
    connect(m_runQueryButton, &QPushButton::clicked, this, &DatabasePanel::onRunQuery);
    connect(m_showEntitiesButton, &QPushButton::clicked, this, &DatabasePanel::onShowEntities);
    connect(m_showComponentsButton, &QPushButton::clicked, this, &DatabasePanel::onShowComponents);
    // Index management connections
    connect(m_listIndexesButton, &QPushButton::clicked, this, &DatabasePanel::onListIndexes);
    connect(m_createIndexButton, &QPushButton::clicked, this, &DatabasePanel::onCreateIndex);
    connect(m_dropIndexButton, &QPushButton::clicked, this, &DatabasePanel::onDropIndex);
    connect(m_rebuildIndexButton, &QPushButton::clicked, this, &DatabasePanel::onRebuildIndex);
    connect(m_analyzeIndexButton, &QPushButton::clicked, this, &DatabasePanel::onAnalyzeIndex);
    connect(m_validateIndexButton, &QPushButton::clicked, this, &DatabasePanel::onValidateIndex);
    // Replication management connections
    connect(m_enableReplicationButton, &QPushButton::clicked, this, &DatabasePanel::onEnableReplication);
    connect(m_disableReplicationButton, &QPushButton::clicked, this, &DatabasePanel::onDisableReplication);
    connect(m_syncReplicationButton, &QPushButton::clicked, this, &DatabasePanel::onSyncReplication);
    connect(m_showReplicationStatusButton, &QPushButton::clicked, this, &DatabasePanel::onShowReplicationStatus);
}

void DatabasePanel::refreshSceneList() {
    m_sceneCombo->clear();
    auto& dbm = DatabaseManager::instance();
    QStringList scenes = dbm.listScenes();
    m_sceneCombo->addItems(scenes);
    if (!scenes.isEmpty()) {
        m_sceneCombo->setCurrentIndex(0);
        m_currentScene = scenes.first();
    }
}

void DatabasePanel::onRefreshScenes() {
    refreshSceneList();
    showStatus("Scene list refreshed.");
}

void DatabasePanel::onSceneSelected(const QString& sceneName) {
    m_currentScene = sceneName;
    refreshEntityList(sceneName);
    showStatus(QString("Selected scene: %1").arg(sceneName));
}

void DatabasePanel::refreshEntityList(const QString& sceneName) {
    auto& dbm = DatabaseManager::instance();
    QSqlDatabase db = dbm.getConnection();
    auto* model = new QSqlTableModel(this, db);
    model->setTable("entities");
    model->setFilter(QString("scene_name='%1'").arg(sceneName));
    model->select();
    m_tableView->setModel(model);
    m_model.reset(model);
    dbm.releaseConnection(db);
}

void DatabasePanel::refreshComponentList(const QString& sceneName, qint64 entityId) {
    auto& dbm = DatabaseManager::instance();
    QSqlDatabase db = dbm.getConnection();
    auto* model = new QSqlTableModel(this, db);
    model->setTable("components");
    model->setFilter(QString("scene_name='%1' AND entity_id=%2").arg(sceneName).arg(entityId));
    model->select();
    m_tableView->setModel(model);
    m_model.reset(model);
    dbm.releaseConnection(db);
}

void DatabasePanel::onSaveScene() {
    if (!m_scene) return; // Can't save a null scene

    // Pass the scene object by dereferencing the pointer
    if (db::DatabaseManager::instance().saveScene(*m_scene, "current")) {
        showStatus("Scene saved successfully.");
    }
    else {
        showStatus("Failed to save scene.", true);
    }
}


void DatabasePanel::onLoadScene() {
    if (m_sceneCombo->count() == 0) { // Check if the combo box has any items.
        return; // Nothing to do if it's empty.
    }
    QString sceneName = m_sceneCombo->currentText(); // Get the name of the scene to load.

    // Emit the signal to notify the MainWindow. Do not perform the load here.
    emit requestSceneReload(sceneName); // Let the MainWindow handle the loading request.
}



void DatabasePanel::onBackup() {
    showStatus("Backup not implemented in this panel.", true);
}

void DatabasePanel::onRestore() {
    showStatus("Restore not implemented in this panel.", true);
}

void DatabasePanel::onRunQuery() {
    QString sql = m_queryEdit->text();
    if (sql.trimmed().isEmpty()) {
        showStatus("No query entered.", true);
        return;
    }
    auto& dbm = DatabaseManager::instance();
    QSqlDatabase db = dbm.getConnection();
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        m_queryResult->setText(query.lastError().text());
        showStatus("Query failed.", true);
    }
    else {
        QString result;
        int cols = query.record().count();
        for (int i = 0; i < cols; ++i) {
            result += query.record().fieldName(i) + "\t";
        }
        result += "\n";
        while (query.next()) {
            for (int i = 0; i < cols; ++i) {
                result += query.value(i).toString() + "\t";
            }
            result += "\n";
        }
        m_queryResult->setText(result);
        showStatus("Query executed.");
    }
    dbm.releaseConnection(db);
}

void DatabasePanel::onShowEntities() {
    if (m_currentScene.isEmpty()) {
        showStatus("No scene selected.", true);
        return;
    }
    refreshEntityList(m_currentScene);
    showStatus("Entities listed.");
}

void DatabasePanel::onShowComponents() {
    if (m_currentScene.isEmpty()) {
        showStatus("No scene selected.", true);
        return;
    }
    auto idx = m_tableView->currentIndex();
    if (!idx.isValid()) {
        showStatus("No entity selected.", true);
        return;
    }
    qint64 entityId = m_model->data(m_model->index(idx.row(), m_model->fieldIndex("entity_id"))).toLongLong();
    m_currentEntityId = entityId;
    refreshComponentList(m_currentScene, entityId);
    showStatus(QString("Components for entity %1 listed.").arg(entityId));
}

void DatabasePanel::showStatus(const QString& message, bool error) {
    m_statusLabel->setText(message);
    m_statusLabel->setStyleSheet(error ? "color: red;" : "color: green;");
}

void DatabasePanel::onListIndexes() {
    db::DatabaseIndexManager idxMgr;
    auto indexes = idxMgr.listIndexes(m_indexTableEdit->text());
    QString result;
    for (const auto& idx : indexes) {
        result += QString("%1 on %2 (%3) [%4]\n").arg(idx.indexName, idx.tableName, idx.columns.join(", "), idx.unique ? "UNIQUE" : "NON-UNIQUE");
    }
    m_queryResult->setText(result.isEmpty() ? "No indexes found." : result);
    showStatus("Indexes listed.");
}

void DatabasePanel::onCreateIndex() {
    db::DatabaseIndexManager idxMgr;
    QString table = m_indexTableEdit->text();
    QString name = m_indexNameEdit->text();
    QStringList columns = m_indexColumnsEdit->text().split(',', Qt::SkipEmptyParts);
    bool unique = QMessageBox::question(this, "Unique Index?", "Should the index be UNIQUE?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
    if (idxMgr.createIndex(table, name, columns, unique)) {
        showStatus("Index created.");
    }
    else {
        showStatus("Failed to create index.", true);
    }
}

void DatabasePanel::onDropIndex() {
    db::DatabaseIndexManager idxMgr;
    QString name = m_indexNameEdit->text();
    if (idxMgr.dropIndex(name)) {
        showStatus("Index dropped.");
    }
    else {
        showStatus("Failed to drop index.", true);
    }
}

void DatabasePanel::onRebuildIndex() {
    db::DatabaseIndexManager idxMgr;
    QString name = m_indexNameEdit->text();
    if (idxMgr.rebuildIndex(name)) {
        showStatus("Index rebuilt.");
    }
    else {
        showStatus("Failed to rebuild index.", true);
    }
}

void DatabasePanel::onAnalyzeIndex() {
    db::DatabaseIndexManager idxMgr;
    QString name = m_indexNameEdit->text();
    if (idxMgr.analyzeIndex(name)) {
        showStatus("Index analyzed.");
    }
    else {
        showStatus("Failed to analyze index.", true);
    }
}

void DatabasePanel::onValidateIndex() {
    db::DatabaseIndexManager idxMgr;
    QString name = m_indexNameEdit->text();
    if (idxMgr.validateIndex(name)) {
        showStatus("Index is valid.");
    }
    else {
        showStatus("Index is NOT valid.", true);
    }
}

void DatabasePanel::onEnableReplication() {
    db::DatabaseReplicationManager replMgr;
    QString master = m_replicationMasterEdit->text();
    if (replMgr.enableReplication(master)) {
        showStatus("Replication enabled.");
    }
    else {
        showStatus("Failed to enable replication.", true);
    }
}

void DatabasePanel::onDisableReplication() {
    db::DatabaseReplicationManager replMgr;
    if (replMgr.disableReplication()) {
        showStatus("Replication disabled.");
    }
    else {
        showStatus("Failed to disable replication.", true);
    }
}

void DatabasePanel::onSyncReplication() {
    db::DatabaseReplicationManager replMgr;
    if (replMgr.syncWithMaster()) {
        showStatus("Replication sync complete.");
    }
    else {
        showStatus("Replication sync failed.", true);
    }
}

void DatabasePanel::onShowReplicationStatus() {
    db::DatabaseReplicationManager replMgr;
    auto status = replMgr.getStatus();
    QString msg = QString("Enabled: %1\nLast Sync: %2\nLast Error: %3\nMode: %4")
        .arg(status.enabled ? "Yes" : "No")
        .arg(status.lastSyncTime)
        .arg(status.lastError)
        .arg(status.mode);
    m_queryResult->setText(msg);
    showStatus("Replication status shown.");
}

void DatabasePanel::initializeFresh()
{
    // This function now only populates the data, since the UI is built in the constructor.
    refreshSceneList();
}

void DatabasePanel::initializeFromDatabase()
{
    auto blob = db::DatabaseManager::instance().loadMenuState("Database");
    if (blob.isEmpty()) {
        initializeFresh();
    }
    else {
        // TODO: Parse blob and set UI state
    }
}

void DatabasePanel::shutdownAndSave()
{
    // serialize current filters, selected scene, etc.
    QString blob;
    db::DatabaseManager::instance()
        .saveMenuState(db::DatabaseManager::menuTypeToString(MenuType::Database), blob);
}
