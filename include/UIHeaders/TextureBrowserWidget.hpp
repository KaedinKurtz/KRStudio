#pragma once

#include <QWidget>

#include <entt/entt.hpp>

class Scene;
class QTreeView;
class QFileSystemModel;
class QLabel;
class QPushButton;
class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QHBoxLayout;

/**
 * @brief Texture/material pack browser: tree navigation of the
 * assets/materials directory, map thumbnails for the selected pack, and
 * one-click hot-swap onto the selected entity. Applying a pack sets
 * MaterialDirectoryTag + MaterialReloadRequest (the renderer loads textures
 * on the engine GL context next frame) and toggles ParallaxMaterialTag
 * based on whether the pack ships a height map — so emissive and the
 * parallax "fake depth" both follow the pack automatically.
 */
class TextureBrowserWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TextureBrowserWidget(Scene* scene, const QString& materialsRoot,
                                  QWidget* parent = nullptr);

    /// Directory of the pack currently highlighted in the tree (empty if none).
    QString currentPackDir() const;

public slots:
    void applyToSelection();

private:
    void onTreeSelectionChanged();
    void rebuildThumbnails(const QString& dir);
    static QStringList mapFilesIn(const QString& dir);
    static bool hasHeightMap(const QString& dir);

    Scene* m_scene = nullptr;
    QString m_root;
    QFileSystemModel* m_model = nullptr;
    QTreeView* m_tree = nullptr;
    QLineEdit* m_filter = nullptr;
    QWidget* m_thumbStrip = nullptr;
    QHBoxLayout* m_thumbLayout = nullptr;
    QLabel* m_packInfo = nullptr;
    QPushButton* m_applyButton = nullptr;
    QCheckBox* m_autoApply = nullptr;
    QDoubleSpinBox* m_heightScale = nullptr;
    QDoubleSpinBox* m_tiling = nullptr;
};
