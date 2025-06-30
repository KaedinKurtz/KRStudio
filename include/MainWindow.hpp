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
class Scene; // Forward-declare Scene
class RenderingSystem;
class QTimer;

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

    // We no longer need pointers to the individual viewports here,
    // as the dock manager handles their lifecycle. We will create them
    // as local variables in the constructor.
    std::vector<ViewportWidget*> m_viewports;

    QTimer* m_masterRenderTimer;

protected slots:
    void onLoadRobotClicked();
    void onMasterRender();

};

