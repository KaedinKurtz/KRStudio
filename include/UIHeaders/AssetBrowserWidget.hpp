#pragma once

#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QLabel;
class QSlider;

/**
 * @brief Mesh asset browser over the runtime assets directory: import
 * external meshes (copied into assets/), delete, spawn via button, or drag
 * an item into a viewport to spawn it at the cursor (the viewport ray-casts
 * the drop point and emits assetDropped; MainWindow does the spawning so
 * collision cooking and selection flow through the usual path).
 */
class AssetBrowserWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AssetBrowserWidget(const QString& assetsRoot, QWidget* parent = nullptr);

signals:
    /// "Add to Scene" was pressed for this absolute mesh path.
    void spawnRequested(const QString& path);

public slots:
    void refresh();

private:
    void importMesh();
    void deleteSelected();
    QString selectedPath() const;

    QString m_root;
    QListWidget* m_list = nullptr;
    QLabel* m_info = nullptr;
};
