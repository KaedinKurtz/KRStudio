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

private:
    void setupUI();
    void refreshSceneList();
    void refreshEntityList(const QString& sceneName);
    void refreshComponentList(const QString& sceneName, qint64 entityId);
    void showStatus(const QString& message, bool error = false);

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
    Scene* m_scene = nullptr;
}; 