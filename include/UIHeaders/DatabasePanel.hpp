#pragma once

#include <QWidget>
#include <QTableView>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QTextEdit>
#include <QSqlTableModel>
#include <memory>
#include "Scene.hpp"

class Scene;

namespace db {
    class DatabaseManager;
    
}

class DatabasePanel : public QWidget {
    Q_OBJECT
public:
    explicit DatabasePanel(Scene* scene, QWidget* parent = nullptr);
    ~DatabasePanel();

signals:
    void requestSceneReload(const QString& sceneName);

private slots:
    void onRefreshScenes();
    void onSceneSelected(const QString& sceneName);
    void onSaveScene();
    void onLoadScene();
    void onBackup();
    void onRestore();
    void onRunQuery();
    void onShowEntities();
    void onShowComponents();
    // Index management slots
    void onListIndexes();
    void onCreateIndex();
    void onDropIndex();
    void onRebuildIndex();
    void onAnalyzeIndex();
    void onValidateIndex();
    // Replication management slots
    void onEnableReplication();
    void onDisableReplication();
    void onSyncReplication();
    void onShowReplicationStatus();

private:
    void setupUI();
    void refreshSceneList();
    void refreshEntityList(const QString& sceneName);
    void refreshComponentList(const QString& sceneName, qint64 entityId);
    void showStatus(const QString& message, bool error = false);
    // Index management UI
    QPushButton* m_listIndexesButton;
    QPushButton* m_createIndexButton;
    QPushButton* m_dropIndexButton;
    QPushButton* m_rebuildIndexButton;
    QPushButton* m_analyzeIndexButton;
    QPushButton* m_validateIndexButton;
    QLineEdit* m_indexNameEdit;
    QLineEdit* m_indexTableEdit;
    QLineEdit* m_indexColumnsEdit;
    // Replication management UI
    QPushButton* m_enableReplicationButton;
    QPushButton* m_disableReplicationButton;
    QPushButton* m_syncReplicationButton;
    QPushButton* m_showReplicationStatusButton;
    QLineEdit* m_replicationMasterEdit;

    QComboBox* m_sceneCombo;
    QPushButton* m_refreshButton;
    QPushButton* m_saveButton;
    QPushButton* m_loadButton;
    QPushButton* m_backupButton;
    QPushButton* m_restoreButton;
    QPushButton* m_showEntitiesButton;
    QPushButton* m_showComponentsButton;
    QTableView* m_tableView;
    QLineEdit* m_queryEdit;
    QPushButton* m_runQueryButton;
    QTextEdit* m_queryResult;
    QLabel* m_statusLabel;

    std::unique_ptr<QSqlTableModel> m_model;
    QString m_currentScene;
    qint64 m_currentEntityId = -1;
    std::unique_ptr<Scene> m_scene;
}; 