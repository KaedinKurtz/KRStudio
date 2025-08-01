// include/MainWindow.hpp
#pragma once
#include <QPushButton>
#include <QMainWindow>
#include <QTimer>
#include <QResizeEvent>
#include <memory> // Required for std::unique_ptr
#include <entt/entt.hpp> 
#include <DockWidget.h>   
#include <librealsense2/rs.hpp>
#include <QMenu>
#include <QToolButton>      // for the slot signature
#include "ViewportManagerPopup.hpp"
#include "MenuFactory.hpp"
#include "IMenu.hpp"
#include <QMap>

// Forward declarations
class QWidget;
class StaticToolbar;
class ViewportWidget;
class Scene;
class RenderingSystem;
class QTimer;
class FlowVisualizerMenu; // Forward-declare our new menu
class SlamManager; // Forward-declare SlamManager
class RealSenseManager; // Forward-declare RealSenseManager
class ViewportManagerPopup; // Forward-declare ViewportManagerPopup

namespace ads {
    class CDockManager;
    class CDockWidget;
}

namespace QtNodes {
    class BasicGraphicsScene;
    class GraphicsView;
}


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();
    void updateVisualizerUI();
    entt::entity cameraEntity() const { return m_cameraEntity; }
    void disableFloatingForAllDockWidgets();
    void updateAllDockStyles();
protected:
    // No changes needed here
    void setDockManagerBaseStyle();
    void applyCameraColorToDock(ads::CDockWidget* dock, entt::entity camEntity);

private:

    struct MenuEntry {
        std::shared_ptr<IMenu> menu;
        ads::CDockWidget * dock;
    };

    QMap<MenuType, MenuEntry>   m_menus;

    void handleMenuToggle(MenuType type, bool checked);
    void showMenu(MenuType type);
    void hideMenu(MenuType type);

    // --- Member variables updated for the new layout ---
    QWidget* m_centralContainer;
    StaticToolbar* m_fixedTopToolbar;
    ViewportManagerPopup* m_viewportManagerPopup;

    ads::CDockManager* m_dockManager;
    QString generateCameraColourCss(ads::CDockWidget* dock,
        entt::entity      camEntity,
        const QString& objectName);
    // The MainWindow now owns the single, shared scene.
    std::unique_ptr<Scene> m_scene;

    std::unique_ptr<RenderingSystem> m_renderingSystem;

    // A pointer to our menu widget
    SlamManager* m_slamManager;

    void syncViewportManagerPopup();

    FlowVisualizerMenu* m_flowVisualizerMenu;
    QTimer* m_rsPollTimer;
    std::unique_ptr<RealSenseManager> m_realSenseManager;
    std::unique_ptr<rs2::pointcloud> m_pointCloud;

    entt::entity m_cameraEntity;

    QTimer* m_masterRenderTimer;

    QWidget* m_viewportHangar;                // A hidden parent for our viewports
    QList<ViewportWidget*> m_viewports;         // A list of the REAL viewports
    QList<QWidget*> m_viewportPlaceholders;   // A list of the placeholder widgets
    QList<ads::CDockWidget*> m_dockContainers;

    QtNodes::BasicGraphicsScene* m_nodeScene; // Owned by GraphicsView
    QtNodes::GraphicsView* m_nodeView;

    void destroyCameraRig(entt::entity cameraEntity);

    const QVector<QColor> kPalette = {
        QColor::fromRgb(230,  25,  75),
        QColor::fromRgb(60, 180,  75),
        QColor::fromRgb(255, 225,  25),
        QColor::fromRgb(0, 130, 200),
        QColor::fromRgb(245, 130,  48),
        QColor::fromRgb(145,  30, 180),
        // …add more as needed…
    };

    QVector<bool> m_colorInUse;

    ads::CDockAreaWidget* m_propertiesArea = nullptr;
    ads::CDockWidget* m_propertiesDock = nullptr;

    void ensurePropertiesArea();

public slots:
    void addViewport();
    void removeViewport();

private slots:
    void onTestNewViewport();
    void updateViewportLayouts();

    void onShowViewportRequested(ads::CDockWidget* dock); // Add this
    void onResetViewports(); // Add this
    void onViewportDockClosed(ads::CDockWidget* closedDock); // Add this

protected slots:
    void onLoadRobotClicked();
    void onMasterRender();
    void onFlowVisualizerTransformChanged();
    void onFlowVisualizerSettingsChanged();
    void onSceneReloadRequested(const QString& sceneName);
};