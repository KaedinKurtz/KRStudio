// include/MainWindow.hpp
#pragma once
#include <QPushButton>
#include <QMainWindow>
#include <QTimer>
#include <QResizeEvent>
#include <memory> // Required for std::unique_ptr

// Forward declarations
class QWidget;
class StaticToolbar;
class ViewportWidget;
class Scene;
class RenderingSystem;
class QTimer;
class FlowVisualizerMenu; // Forward-declare our new menu

namespace ads {
    class CDockManager;
    class CDockWidget;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();
    void updateVisualizerUI();

    void disableFloatingForAllDockWidgets();

protected:
    // No changes needed here

private:
    // --- Member variables updated for the new layout ---
    QWidget* m_centralContainer;
    StaticToolbar* m_fixedTopToolbar;
    ads::CDockManager* m_dockManager;

    // The MainWindow now owns the single, shared scene.
    std::unique_ptr<Scene> m_scene;

    std::unique_ptr<RenderingSystem> m_renderingSystem;

    // A pointer to our menu widget
    FlowVisualizerMenu* m_flowVisualizerMenu;


    QTimer* m_masterRenderTimer;

    QWidget* m_viewportHangar;                // A hidden parent for our viewports
    QList<ViewportWidget*> m_viewports;         // A list of the REAL viewports
    QList<QWidget*> m_viewportPlaceholders;   // A list of the placeholder widgets
    QList<ads::CDockWidget*> m_dockContainers;

public slots:
    void addViewport();
    void removeViewport();

private slots:
    void onTestNewViewport();
    void updateViewportLayouts();

protected slots:
    void onLoadRobotClicked();
    void onMasterRender();
    void onFlowVisualizerTransformChanged();
    void onFlowVisualizerSettingsChanged();
};
