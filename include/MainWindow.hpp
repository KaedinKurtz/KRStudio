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

    // We keep explicit pointers to the viewports so we can control
    // their destruction order relative to the OpenGL context.
    ViewportWidget* m_viewport1 = nullptr;
    ViewportWidget* m_viewport2 = nullptr;

private slots:
    void onLoadRobotClicked();

};

