// include/MainWindow.hpp
#pragma once
#include <QPushButton>
#include <QMainWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <QVector>
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
class Camera;
class RenderingSystem;
class QTimer;
class FlowVisualizerMenu; // Forward-declare our new menu
class SlamManager; // Forward-declare SlamManager
class RealSenseManager; // Forward-declare RealSenseManager
class ViewportManagerPopup; // Forward-declare ViewportManagerPopup
class GizmoSystem;
class SimulationController;
class PhysicsPropertiesWidget;
class RobotBuilderPanel;
class TextureBrowserWidget;

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

    ViewportWidget* primaryViewport() const;
    void refreshGizmoAndProperties(ViewportWidget* vp = nullptr);

public:
    enum class SimSource { WaterTap, WaterSink, SmokeEmitter, FireEmitter };
    /// Spawn a small marker entity carrying the simulation-source component
    /// (emitter / sink / smoke / fire) at a world position.
    void spawnSimSourceAt(SimSource kind, const glm::vec3& worldPos);

public slots:
    /// Load a mesh asset and spawn it at a world position (asset-browser
    /// button and viewport drag-and-drop both land here).
    void spawnMeshAssetAt(const QString& path, const glm::vec3& worldPos);
    /// Right-click scene menu (delete/duplicate/focus/add-at-point).
    void onViewportContextMenu(const QPoint& globalPos, const glm::vec3& worldPos,
                               entt::entity hit);

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

    FlowVisualizerMenu* m_flowVisualizerMenu = nullptr;  // set when the menu opens; null when closed
    QTimer* m_rsPollTimer;
    std::unique_ptr<RealSenseManager> m_realSenseManager;
    std::unique_ptr<rs2::pointcloud> m_pointCloud;
    std::unique_ptr<GizmoSystem> m_gizmoSystem;
    std::unique_ptr<SimulationController> m_simulation;
    PhysicsPropertiesWidget* m_physicsPanel = nullptr;
    RobotBuilderPanel*       m_robotBuilderPanel = nullptr;
    TextureBrowserWidget* m_textureBrowser = nullptr;

    void buildMenuBar();
    entt::entity addObjectFromMenu(int primitive, const QString& baseName,
                                   const glm::vec3& pos, const glm::vec3& scale);
    entt::entity duplicateEntity(entt::entity src);

    // --- Engineering toolbar (Phase 4) ---
    void buildEngineeringToolbar();   // top QToolBar: Import CAD, viz mode, injectors, inspector
    entt::entity selectedEntity() const; // entity tagged SelectedComponent, or entt::null
    void assignMaterialToSelection(); // Assign Material dialog -> MaterialComponent + mass
    void addHeatSourceToSelection();  // Add/tune HeatSourceComponent on the selection
    void inspectSelection();          // dialog exposing Material + HeatSource of the selection
    void importStepFile();            // Import CAD (STEP) -> OCCT ingestion

    entt::entity m_cameraEntity;

    QTimer* m_masterRenderTimer;

    // Frame-rate cap (Settings: perf/maxFps). m_minFrameMs > 0 throttles renders;
    // engineFrame() renders when the free-running clock passes m_nextRenderMs, then
    // advances the deadline by exactly m_minFrameMs (carry => average rate == target,
    // no downward drift from discrete tick granularity). 0 = uncapped.
    QElapsedTimer m_frameClock;
    double        m_minFrameMs   = 0.0;
    double        m_nextRenderMs = 0.0;

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
        // �add more as needed�
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
    void onSelectionChanged(const QVector<entt::entity>& selectedEntities, const Camera& camera); // <-- ADD THIS NEW SLOT

protected slots:
    void onLoadRobotClicked();
    void onFlowVisualizerTransformChanged();
    void onFlowVisualizerSettingsChanged();
    void onSceneReloadRequested(const QString& sceneName);
};