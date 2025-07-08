// include/MainWindow.hpp
#pragma once

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

protected slots:
    void onLoadRobotClicked();
    void onMasterRender();
    void onFlowVisualizerTransformChanged();
    void onFlowVisualizerSettingsChanged();
};
